/**
 * @file game_session.cpp
 * @brief Implementation of the GameSession gameplay simulation façade.
 *
 * @details Drives one player's frame for the InGame state: ring-ordered chunk streaming
 *          through the JobSystem (bounded in-flight jobs, hysteresis-based eviction), camera
 *          and physics stepping, block break/place with hardness timing and inventory
 *          updates, sound/particle/edited-block event buffers, achievement unlocks via
 *          IPlatformServices, and replication through the optional network client. Remote
 *          worlds (SetRemoteWorld) disable local generation and eviction — chunks arrive
 *          from the host server instead.
 */

#include "voxels/app/game_session.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>

#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/spawn_calculator.hpp"

namespace voxels {

namespace {
/// Splash-event hysteresis/cooldown: without these, bodyFraction hovering right at the tread
/// ceiling or a swim-climb step edge crosses the bare `>0` boundary almost every tick, spamming
/// the splash sound dozens of times a second instead of once per real entry/exit.
constexpr float kSplashEnterFraction = 0.05f;
constexpr float kSplashExitFraction = 0.01f;
constexpr float kSplashCooldownSeconds = 0.35f;
constexpr float kDamageInvulnerabilitySeconds = 0.65f;
constexpr float kDrowningGraceSeconds = 10.0f;
constexpr float kDrowningDamageIntervalSeconds = 1.0f;
// A six-block fall reaches roughly 16 blocks/s; routine drops should remain safe.
constexpr float kFallDamageVelocityThreshold = 18.0f;

/// Counts orthogonally-adjacent solid blocks around `position` (6-connectivity, excluding
/// `position` itself). Used as a cheap, real-geometry proxy for "this placement joined an
/// existing structure" rather than a bare placed-block counter.
int CountSolidNeighbors(const World& world, const Vec3I& position) {
    static constexpr Vec3I kOffsets[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int count = 0;
    for (const Vec3I& offset : kOffsets) {
        const Vec3I neighbor{position.x + offset.x, position.y + offset.y, position.z + offset.z};
        if (gameplay::Physics::IsSolidBlock(world.GetBlock(neighbor))) ++count;
    }
    return count;
}
} // namespace

GameSession::GameSession() : m_ownedWorld(std::make_unique<World>()) , m_world(m_ownedWorld.get()) {
    m_worldOptions.renderDistanceChunks = 4;
    m_worldOptions.simulationDistanceChunks = 3;
}

GameSession::GameSession(World* world) : m_world(world) {
    if (m_world == nullptr) {
        m_ownedWorld = std::make_unique<World>();
        m_world = m_ownedWorld.get();
    }
    m_worldOptions.renderDistanceChunks = 4;
    m_worldOptions.simulationDistanceChunks = 3;
}

void GameSession::SetWorld(World* world) noexcept {
    m_world = world == nullptr ? m_ownedWorld.get() : world;
    if (m_world == nullptr) {
        m_ownedWorld = std::make_unique<World>();
        m_world = m_ownedWorld.get();
    }
}

void GameSession::AdoptWorld(std::unique_ptr<World> world) noexcept {
    m_ownedWorld = std::move(world);
    if (m_ownedWorld == nullptr) m_ownedWorld = std::make_unique<World>();
    m_world = m_ownedWorld.get();
}

void GameSession::SetWorldOptions(const WorldOptions& options) noexcept {
    m_worldOptions = options;
}

void GameSession::SetInputManager(InputManager* inputManager) noexcept {
    m_input = inputManager;
}

void GameSession::SetAuthoritativeItemDrops(gameplay::ItemDropSimulation* itemDrops) noexcept {
    m_itemDrops = itemDrops == nullptr ? &m_ownedItemDrops : itemDrops;
    m_itemDropsAdvancedExternally = itemDrops != nullptr;
}

void GameSession::SetBlockRegistry(const BlockRegistry* registry) noexcept {
    m_registry = registry;
}

std::vector<ChunkCoordinate> GameSession::ConsumeArrivedChunks() {
    std::vector<ChunkCoordinate> arrived;
    arrived.swap(m_arrivedChunks);
    return arrived;
}

std::vector<ChunkCoordinate> GameSession::ConsumeRemovedChunks() {
    std::vector<ChunkCoordinate> removed;
    removed.swap(m_removedChunks);
    return removed;
}

void GameSession::SetPlayerSpawn(const Vec3& spawn) {
    m_spawnPosition = spawn;
    m_spawnExplicitlySet = true;
    m_player.state.position = spawn;
    m_player.state.velocity = Vec3{0.0f};
    m_player.state.onGround = false;
}

void GameSession::SetPlayerSpawn(const Vec3I& spawn) {
    m_spawnPosition = Vec3{static_cast<float>(spawn.x), static_cast<float>(spawn.y), static_cast<float>(spawn.z)};
    m_spawnExplicitlySet = true;
    m_player.state.position = m_spawnPosition;
    m_player.state.velocity = Vec3{0.0f};
    m_player.state.onGround = false;
}

void GameSession::RestorePlayerState(const PlayerState& state) noexcept {
    m_player.state = state;
    m_player.state.health = std::min(m_player.state.health, kMaximumHealth);
    m_spawnPosition = state.position;
    m_playerStateRestored = true;
}

void GameSession::Initialize() {
    if (m_world == nullptr) {
        m_ownedWorld = std::make_unique<World>();
        m_world = m_ownedWorld.get();
    }
    if (!m_world->Initialize(m_worldOptions)) {
        return;
    }

    if (m_world->LoadedChunkCount() == 0 && !m_remoteWorld) {
        WorldGenerator generator(m_worldOptions);
        for (int cz = -1; cz <= 1; ++cz) {
            for (int cx = -1; cx <= 1; ++cx) {
                for (int cy = 0; cy < kTerrainSectionCount; ++cy) {
                    const ChunkCoordinate coord{cx, cy, cz};
                    m_world->GetOrCreateChunk(coord) = generator.GenerateChunk(coord);
                }
                m_world->GetOrCreateChunk({cx, kTerrainSectionCount, cz});
            }
        }
    }

    if (!m_spawnExplicitlySet) {
        const int spawnBlockX = 8;
        const int spawnBlockZ = 8;
        int surfaceY = 1;
        for (int y = 47; y >= 0; --y) {
            const BlockId block = m_world->GetBlock(Vec3I{spawnBlockX, y, spawnBlockZ});
            if (gameplay::Physics::IsSolidBlock(block)) {
                surfaceY = y;
                break;
            }
        }
        m_spawnPosition = Vec3{static_cast<float>(spawnBlockX) + 0.5f,
                               static_cast<float>(surfaceY + 1) + gameplay::Physics::kPlayerHalfHeight,
                               static_cast<float>(spawnBlockZ) + 0.5f};
    }

    if (!m_playerStateRestored) {
        m_player.state.position = m_spawnPosition;
        m_player.state.velocity = Vec3{0.0f};
        m_player.state.onGround = true;
    }
    const gameplay::SubmersionInfo initialSubmersion = gameplay::Physics::SampleSubmersion(*m_world, m_player, m_registry);
    m_wasInWater = initialSubmersion.bodyFraction > 0.0f;
    m_eyeSubmerged = initialSubmersion.eyeSubmerged;
    m_submersionFraction = initialSubmersion.bodyFraction;
    m_lastSelectedStack = m_player.state.inventory.GetSelectedStack();
    m_camera.position = glm::vec3(m_player.state.position.x,
                                  m_player.state.position.y + gameplay::Physics::kEyeOffsetFromCenter,
                                  m_player.state.position.z);
    m_camera.yaw = m_player.state.yaw;
    m_camera.pitch = m_player.state.pitch;
    m_camera.fovY = glm::radians(70.0f);
    m_camera.aspect = 1280.0f / 720.0f;
    m_camera.nearPlane = 0.1f;
    m_camera.farPlane = 256.0f;
    m_initialized = true;
}

void GameSession::EnsureChunkResidentAroundPlayer() {
    if (m_world == nullptr || m_remoteWorld) {
        return;
    }
    ApplyCompletedChunkJobs();
    const int cx = static_cast<int>(std::floor(m_player.state.position.x / 16.0f));
    const int cz = static_cast<int>(std::floor(m_player.state.position.z / 16.0f));
    const std::size_t maxQueued = m_jobSystem == nullptr ? 0 : kMaxQueuedGenerationJobs;
    const int loadRadius = std::max(1, m_worldOptions.renderDistanceChunks);
    for (int ring = 0; ring <= loadRadius && m_pendingChunkJobs.size() < maxQueued; ++ring) {
        for (int z = -ring; z <= ring && m_pendingChunkJobs.size() < maxQueued; ++z) {
            for (int x = -ring; x <= ring && m_pendingChunkJobs.size() < maxQueued; ++x) {
                if (std::max(std::abs(x), std::abs(z)) != ring) continue;
                for (int y = 0; y < kTerrainSectionCount && m_pendingChunkJobs.size() < maxQueued; ++y) {
                    const ChunkCoordinate coord{cx + x, y, cz + z};
                const bool queued = std::any_of(m_pendingChunkJobs.begin(), m_pendingChunkJobs.end(), [&coord](const PendingChunkJob& pending) {
                    return pending.coordinate == coord;
                });
                if (!m_world->HasChunk(coord) && !queued && m_jobSystem != nullptr && m_pendingChunkJobs.size() < maxQueued) {
                    const WorldOptions options = m_worldOptions;
                    m_pendingChunkJobs.push_back({coord, m_jobSystem->EnqueueWithResult([options, coord] {
                        return WorldGenerator(options).GenerateChunk(coord);
                    })});
                }
            }
            }
        }
    }
    (void)m_world->UnloadCleanChunksOutsideRadius({cx, 0, cz}, loadRadius + kStreamingHysteresisChunks,
                                                   &m_removedChunks);
}

void GameSession::ApplyCompletedChunkJobs() {
    if (m_world == nullptr) return;
    std::size_t applied = 0;
    auto pending = m_pendingChunkJobs.begin();
    while (pending != m_pendingChunkJobs.end() && applied < kMaxCompletedChunksPerFrame) {
        if (pending->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++pending;
            continue;
        }
        if (!m_world->HasChunk(pending->coordinate)) {
            Chunk generatedChunk = pending->result.get();
            generatedChunk.ClearDirty();
            m_world->GetOrCreateChunk(pending->coordinate) = std::move(generatedChunk);
            m_arrivedChunks.push_back(pending->coordinate);
            if (pending->coordinate.y == kTerrainSectionCount - 1) {
                const ChunkCoordinate cap{pending->coordinate.x, kTerrainSectionCount, pending->coordinate.z};
                m_world->GetOrCreateChunk(cap);
                m_arrivedChunks.push_back(cap);
            }
        } else {
            pending->result.get();
        }
        pending = m_pendingChunkJobs.erase(pending);
        ++applied;
    }
}

void GameSession::Update(float deltaSeconds) {
    if (!m_initialized || m_world == nullptr) {
        return;
    }
    EnsureChunkResidentAroundPlayer();
    m_splashCooldownSeconds = std::max(0.0f, m_splashCooldownSeconds - deltaSeconds);
    m_damageInvulnerabilitySeconds = std::max(0.0f, m_damageInvulnerabilitySeconds - deltaSeconds);

    if (m_networkClient != nullptr) {
        for (const networking::ItemPickup& pickup : m_networkClient->TakeReceivedItemPickups()) {
            const int overflow = m_player.state.inventory.AddItem(pickup.blockId, static_cast<int>(pickup.count));
            if (overflow > 0) {
                m_itemDrops->Spawn({pickup.blockId, overflow}, m_player.state.position, {}, 0.25f);
            }
        }
    }

    if (m_input != nullptr) {
        const InputState input = m_input->GetInputState();
        const bool wasGrounded = m_player.state.onGround;
        gameplay::CameraController controller;
        controller.Update(m_player, input, m_preferences, deltaSeconds);
        if (input.jump && wasGrounded && !m_player.state.onGround) {
            m_soundEvents.push_back({GameplaySoundEventType::Jump,
                                     {static_cast<int>(std::floor(m_player.state.position.x)), static_cast<int>(std::floor(m_player.state.position.y)), static_cast<int>(std::floor(m_player.state.position.z))}});
        }
        const float fallVelocity = m_player.state.velocity.y;
        gameplay::Physics::Step(*m_world, m_player, deltaSeconds, m_registry, input.jump);
        const gameplay::SubmersionInfo submersion = gameplay::Physics::SampleSubmersion(*m_world, m_player, m_registry);
        m_eyeSubmerged = submersion.eyeSubmerged;
        m_submersionFraction = submersion.bodyFraction;
        const Vec3I playerBlock{static_cast<int>(std::floor(m_player.state.position.x)),
                                static_cast<int>(std::floor(m_player.state.position.y)),
                                static_cast<int>(std::floor(m_player.state.position.z))};
        const BlockId standingBlock = m_world->GetBlock({playerBlock.x, static_cast<int>(std::floor(m_player.state.position.y - gameplay::Physics::kPlayerHalfHeight - 0.05f)), playerBlock.z});
        const float horizontalSpeed = std::sqrt(m_player.state.velocity.x * m_player.state.velocity.x + m_player.state.velocity.z * m_player.state.velocity.z);
        if (m_player.state.onGround && horizontalSpeed > 0.25f) {
            m_footstepSeconds += deltaSeconds;
            if (m_footstepSeconds >= std::clamp(0.46f / horizontalSpeed, 0.22f, 0.55f)) {
                m_soundEvents.push_back({GameplaySoundEventType::Footstep, playerBlock, standingBlock});
                m_footstepSeconds = 0.0f;
            }
        } else {
            m_footstepSeconds = 0.0f;
        }
        if (!wasGrounded && m_player.state.onGround && fallVelocity < -4.0f) {
            m_soundEvents.push_back({GameplaySoundEventType::Land, playerBlock, standingBlock});
            if (!submersion.feetSubmerged && fallVelocity < -kFallDamageVelocityThreshold) {
                const float excessVelocity = -fallVelocity - kFallDamageVelocityThreshold;
                (void)ApplyDamage(static_cast<std::uint8_t>(std::clamp(std::ceil(excessVelocity), 1.0f, 15.0f)),
                                  DamageSource::Fall);
            }
        }
        if (submersion.eyeSubmerged) {
            m_drowningSeconds += deltaSeconds;
            if (m_drowningSeconds >= kDrowningGraceSeconds) {
                m_drowningDamageSeconds += deltaSeconds;
                if (m_drowningDamageSeconds >= kDrowningDamageIntervalSeconds) {
                    (void)ApplyDamage(1, DamageSource::Drowning);
                    m_drowningDamageSeconds = 0.0f;
                }
            }
        } else {
            m_drowningSeconds = 0.0f;
            m_drowningDamageSeconds = 0.0f;
        }
        const bool inWater = m_wasInWater ? submersion.bodyFraction > kSplashExitFraction
                                          : submersion.bodyFraction > kSplashEnterFraction;
        if (inWater != m_wasInWater && m_splashCooldownSeconds <= 0.0f) {
            m_soundEvents.push_back({GameplaySoundEventType::Splash, playerBlock, static_cast<BlockId>(BlockType::Water)});
            m_wasInWater = inWater;
            m_splashCooldownSeconds = kSplashCooldownSeconds;
        }
        // Zero sky light with a solid roof overhead means no line to the open sky: a real,
        // world-data-driven signal for "the player is underground/in a cave", not a Y threshold.
        if (m_platformServices != nullptr && m_world->GetSkyLight(playerBlock) == 0 &&
            gameplay::Physics::IsSolidBlock(m_world->GetBlock({playerBlock.x, playerBlock.y + 2, playerBlock.z}))) {
            m_platformServices->UnlockAchievement(Achievement::FirstCaveEntered);
        }
        if (m_registry != nullptr) {
            m_target = m_blockInteraction.Target(*m_world, m_player, *m_registry);
            if (input.hotbarSlot >= 0) m_player.state.inventory.SetSelectedSlot(input.hotbarSlot);
            if (input.mouseWheelY != 0) m_player.state.inventory.CycleSelectedSlot(input.mouseWheelY > 0 ? -1 : 1);
            m_placeCooldown = std::max(0.0f, m_placeCooldown - deltaSeconds);
            if (input.destroyBlock && m_target.hit) {
                const BlockDefinition* definition = m_registry->GetDefinition(m_world->GetBlock(m_target.blockPosition));
                if (definition != nullptr && !definition->isLiquid && definition->hardness >= 0.0f) {
                    if (!m_hasBreakTarget || m_breakTarget != m_target.blockPosition ||
                        m_breakTargetBlockId != definition->id) {
                        m_breakTarget = m_target.blockPosition;
                        m_breakTargetBlockId = definition->id;
                        m_hasBreakTarget = true;
                        m_breakProgress = 0.0f;
                    }
                    m_breakProgress += m_worldOptions.sandboxMode ? 1.0f : deltaSeconds / std::max(0.05f, definition->hardness);
                    if (m_breakProgress >= 1.0f) {
                        const gameplay::InteractionResult result = m_blockInteraction.BreakBlock(*m_world, m_target, *m_registry);
                        if (result.success) {
                            m_itemDrops->ResolveAfterBlockEdit(*m_world, m_registry, result.targetPosition);
                            static std::mt19937 scatterRng{std::random_device{}()};
                            std::uniform_real_distribution<float> scatter(-1.2f, 1.2f);
                            const Vec3 dropOrigin{static_cast<float>(result.targetPosition.x) + 0.5f,
                                                  static_cast<float>(result.targetPosition.y) + 0.5f,
                                                  static_cast<float>(result.targetPosition.z) + 0.5f};
                            for (const BlockDrop& drop : definition->drops) {
                                const BlockDefinition* dropDefinition = m_registry->GetDefinition(drop.item);
                                if (dropDefinition != nullptr) {
                                    if (!m_remoteWorld) {
                                        m_itemDrops->Spawn({dropDefinition->id, drop.count}, dropOrigin,
                                                           {scatter(scatterRng), 2.2f, scatter(scatterRng)});
                                    }
                                }
                            }
                            m_editedBlocks.push_back(result.targetPosition);
                            m_soundEvents.push_back({GameplaySoundEventType::Break, result.targetPosition, result.blockId});
                            if (m_platformServices != nullptr) m_platformServices->UnlockAchievement(Achievement::FirstBlockBroken);
                            if (m_preferences.particles) m_particleBursts.push_back(result.targetPosition);
                            if (m_networkClient != nullptr && m_networkClient->HasReceivedConnectAck()) {
                                m_networkClient->SendBlockModify(
                                    {result.targetPosition, static_cast<BlockId>(BlockType::Air)});
                            }
                        }
                        m_breakProgress = 0.0f;
                        m_hasBreakTarget = false;
                    }
                } else {
                    m_breakProgress = 0.0f;
                    m_hasBreakTarget = false;
                }
            } else {
                m_breakProgress = 0.0f;
                m_hasBreakTarget = false;
            }
            if (input.placeBlock && m_target.hit && m_placeCooldown <= 0.0f) {
                const gameplay::InteractionResult result = m_blockInteraction.PlaceBlock(*m_world, m_player, m_target, *m_registry);
                if (result.success) {
                    m_itemDrops->ResolveAfterBlockEdit(*m_world, m_registry, result.adjacentPosition);
                    if (!m_worldOptions.sandboxMode) {
                        const bool removed = m_player.state.inventory.RemoveItem(
                            static_cast<std::size_t>(m_player.state.inventory.GetSelectedSlot()), 1);
                        (void)removed;
                    }
                    m_editedBlocks.push_back(result.adjacentPosition);
                    m_soundEvents.push_back({GameplaySoundEventType::Place, result.adjacentPosition, result.blockId});
                    if (m_platformServices != nullptr && CountSolidNeighbors(*m_world, result.adjacentPosition) >= 3) {
                        m_platformServices->UnlockAchievement(Achievement::FirstStructureBuilt);
                    }
                    m_placeCooldown = 0.16f;
                    if (m_networkClient != nullptr && m_networkClient->HasReceivedConnectAck()) {
                        m_networkClient->SendBlockModify({result.adjacentPosition, result.blockId});
                    }
                }
            }
            if (input.dropItem && !m_dropItemHeldLastFrame) {
                DropInventorySlot(static_cast<std::size_t>(m_player.state.inventory.GetSelectedSlot()), 1);
            }
            m_dropItemHeldLastFrame = input.dropItem;
            const gameplay::ItemStack& selectedStack = m_player.state.inventory.GetSelectedStack();
            if (selectedStack != m_lastSelectedStack) {
                m_lastSelectedStack = selectedStack;
                m_selectedItemLabelAge = 0.0f;
                const BlockDefinition* definition = m_registry->GetDefinition(selectedStack.blockId);
                m_selectedItemLabel = selectedStack.IsEmpty() || definition == nullptr ? "" : definition->displayName;
            } else if (!m_selectedItemLabel.empty()) {
                m_selectedItemLabelAge += deltaSeconds;
            }
        }
        m_input->Update();
    } else {
        gameplay::Physics::Step(*m_world, m_player, deltaSeconds, m_registry);
        const gameplay::SubmersionInfo submersion = gameplay::Physics::SampleSubmersion(*m_world, m_player, m_registry);
        m_eyeSubmerged = submersion.eyeSubmerged;
        m_submersionFraction = submersion.bodyFraction;
        m_breakProgress = 0.0f;
        m_hasBreakTarget = false;
    }

    if (!m_itemDropsAdvancedExternally) {
        m_itemDrops->Update(*m_world, m_registry, deltaSeconds, &m_player.state.position);
        (void)m_itemDrops->CollectPickups(m_player.state.position, m_player.state.inventory);
    }

    m_camera.position = glm::vec3(m_player.state.position.x,
                                  m_player.state.position.y + gameplay::Physics::kEyeOffsetFromCenter,
                                  m_player.state.position.z);
    m_camera.yaw = m_player.state.yaw;
    m_camera.pitch = m_player.state.pitch;
    if (m_networkClient != nullptr && m_networkClient->HasReceivedConnectAck()) {
        networking::InventoryState inventoryState;
        for (std::size_t slot = 0; slot < gameplay::Inventory::kSlotCount; ++slot) {
            const gameplay::ItemStack& stack = m_player.state.inventory.GetSlot(slot);
            inventoryState.slots[slot] = {stack.blockId, static_cast<std::uint16_t>(std::max(0, stack.count))};
        }
        m_networkClient->SendInventoryState(inventoryState);
        m_networkClient->SendPlayerMove({m_player.state.position,
                                         {m_player.state.yaw, m_player.state.pitch, 0.0f},
                                         m_player.state.velocity});
    }
}

bool GameSession::ApplyDamage(std::uint8_t amount, DamageSource source, std::uint32_t instigator) {
    if (amount == 0 || m_player.state.health == 0 || m_damageInvulnerabilitySeconds > 0.0f) return false;
    const std::uint8_t applied = std::min(amount, m_player.state.health);
    m_player.state.health = static_cast<std::uint8_t>(m_player.state.health - applied);
    HealthEvent event{source, applied, instigator};
    if (m_player.state.health == 0) {
        event.died = true;
        // Death preserves the current inventory, then restores a verified session spawn.
        m_player.state.position = m_spawnPosition;
        m_player.state.velocity = Vec3{0.0f};
        m_player.state.onGround = false;
        m_player.state.health = kMaximumHealth;
        event.respawned = true;
    }
    m_damageInvulnerabilitySeconds = kDamageInvulnerabilitySeconds;
    m_healthEvents.push_back(event);
    return true;
}

bool GameSession::ApplyHealing(std::uint8_t amount, DamageSource source, std::uint32_t instigator) {
    if (amount == 0 || m_player.state.health >= kMaximumHealth) return false;
    const std::uint8_t applied = std::min<std::uint8_t>(amount, kMaximumHealth - m_player.state.health);
    m_player.state.health = static_cast<std::uint8_t>(m_player.state.health + applied);
    m_healthEvents.push_back({source, applied, instigator});
    return true;
}

int GameSession::DropInventorySlot(std::size_t slot, int count) {
    if (m_remoteWorld || slot >= gameplay::Inventory::kSlotCount || count <= 0) return 0;
    gameplay::ItemStack& stack = m_player.state.inventory.GetSlot(slot);
    if (stack.IsEmpty()) return 0;
    const BlockId droppedBlockId = stack.blockId;
    const int toDrop = std::min(count, stack.count);
    if (!m_player.state.inventory.RemoveItem(slot, toDrop)) return 0;

    const Vec3 forward{-std::sin(m_player.state.yaw), 0.0f, -std::cos(m_player.state.yaw)};
    const Vec3 tossOrigin{m_player.state.position.x + forward.x * 0.4f,
                          m_player.state.position.y + 0.2f,
                          m_player.state.position.z + forward.z * 0.4f};
    m_itemDrops->Spawn({droppedBlockId, toDrop}, tossOrigin,
                       {forward.x * 0.6f, 2.0f, forward.z * 0.6f}, 0.75f);
    return toDrop;
}

void GameSession::Shutdown() noexcept {
    for (PendingChunkJob& pending : m_pendingChunkJobs) {
        if (pending.result.valid()) pending.result.wait();
    }
    m_pendingChunkJobs.clear();
    m_initialized = false;
    m_input = nullptr;
    m_networkClient = nullptr;
    m_remoteWorld = false;
    m_jobSystem = nullptr;
    m_world = nullptr;
    m_ownedWorld.reset();
    m_player = Player{};
    m_itemDrops->Clear();
    m_ownedItemDrops.Clear();
    m_itemDrops = &m_ownedItemDrops;
    m_itemDropsAdvancedExternally = false;
    m_playerStateRestored = false;
}

void GameSession::ResolveItemDropsAfterBlockEdit(const Vec3I& editedBlock) {
    if (m_world != nullptr) m_itemDrops->ResolveAfterBlockEdit(*m_world, m_registry, editedBlock);
}

World& GameSession::GetWorld() noexcept {
    if (m_world == nullptr) {
        m_ownedWorld = std::make_unique<World>();
        m_world = m_ownedWorld.get();
    }
    return *m_world;
}

const World& GameSession::GetWorld() const noexcept {
    if (m_world == nullptr) {
        return *m_ownedWorld;
    }
    return *m_world;
}

Player& GameSession::GetPlayer() noexcept {
    return m_player;
}

const Player& GameSession::GetPlayer() const noexcept {
    return m_player;
}

Camera& GameSession::GetCamera() noexcept {
    return m_camera;
}

const Camera& GameSession::GetCamera() const noexcept {
    return m_camera;
}

} // namespace voxels
