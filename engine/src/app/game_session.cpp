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

#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/spawn_calculator.hpp"

namespace voxels {

namespace {
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
    const Vec3I initialPlayerBlock{static_cast<int>(std::floor(m_player.state.position.x)),
                                   static_cast<int>(std::floor(m_player.state.position.y)),
                                   static_cast<int>(std::floor(m_player.state.position.z))};
    m_wasInWater = m_world->GetBlock(initialPlayerBlock) == static_cast<BlockId>(BlockType::Water);
    m_lastSelectedStack = m_player.state.inventory.GetSelectedStack();
    m_camera.position = glm::vec3(m_player.state.position.x,
                                  m_player.state.position.y + 0.72f,
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
        gameplay::Physics::Step(*m_world, m_player, deltaSeconds, m_registry);
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
        }
        const bool inWater = m_world->GetBlock(playerBlock) == static_cast<BlockId>(BlockType::Water);
        if (inWater != m_wasInWater) {
            m_soundEvents.push_back({GameplaySoundEventType::Splash, playerBlock, static_cast<BlockId>(BlockType::Water)});
            m_wasInWater = inWater;
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
                            for (const BlockDrop& drop : definition->drops) {
                                const BlockDefinition* dropDefinition = m_registry->GetDefinition(drop.item);
                                if (dropDefinition != nullptr) {
                                    const int overflow = m_player.state.inventory.AddItem(dropDefinition->id, drop.count);
                                    (void)overflow;
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
                const gameplay::InteractionResult result = m_blockInteraction.PlaceBlock(*m_world, m_player, m_target);
                if (result.success) {
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
        m_breakProgress = 0.0f;
        m_hasBreakTarget = false;
    }

    m_camera.position = glm::vec3(m_player.state.position.x,
                                  m_player.state.position.y + 0.72f,
                                  m_player.state.position.z);
    m_camera.yaw = m_player.state.yaw;
    m_camera.pitch = m_player.state.pitch;
    if (m_networkClient != nullptr && m_networkClient->HasReceivedConnectAck()) {
        m_networkClient->SendPlayerMove({m_player.state.position,
                                         {m_player.state.yaw, m_player.state.pitch, 0.0f},
                                         m_player.state.velocity});
    }
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
    m_playerStateRestored = false;
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
