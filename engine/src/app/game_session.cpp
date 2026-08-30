#include "voxels/app/game_session.hpp"

#include <algorithm>
#include <cmath>

#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/spawn_calculator.hpp"

namespace voxels {

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

void GameSession::SetWorldOptions(const WorldOptions& options) noexcept {
    m_worldOptions = options;
}

void GameSession::SetInputManager(InputManager* inputManager) noexcept {
    m_input = inputManager;
}

void GameSession::SetBlockRegistry(const BlockRegistry* registry) noexcept {
    m_registry = registry;
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

void GameSession::Initialize() {
    if (m_world == nullptr) {
        m_ownedWorld = std::make_unique<World>();
        m_world = m_ownedWorld.get();
    }
    if (!m_world->Initialize(m_worldOptions)) {
        return;
    }

    if (m_world->LoadedChunkCount() == 0) {
        WorldGenerator generator(m_worldOptions);
        for (int cz = -1; cz <= 1; ++cz) {
            for (int cx = -1; cx <= 1; ++cx) {
                for (int cy = 0; cy < 3; ++cy) {
                    const ChunkCoordinate coord{cx, cy, cz};
                    m_world->GetOrCreateChunk(coord) = generator.GenerateChunk(coord);
                }
                m_world->GetOrCreateChunk({cx, 3, cz});
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

    m_player.state.position = m_spawnPosition;
    m_player.state.velocity = Vec3{0.0f};
    m_player.state.onGround = true;
    m_lastSelectedStack = m_player.state.inventory.GetSelectedStack();
    m_camera.position = glm::vec3(m_player.state.position.x,
                                  m_player.state.position.y + 0.72f,
                                  m_player.state.position.z);
    m_camera.yaw = 0.0f;
    m_camera.pitch = 0.0f;
    m_camera.fovY = glm::radians(70.0f);
    m_camera.aspect = 1280.0f / 720.0f;
    m_camera.nearPlane = 0.1f;
    m_camera.farPlane = 256.0f;
    m_initialized = true;
}

void GameSession::EnsureChunkResidentAroundPlayer() {
    if (m_world == nullptr) {
        return;
    }
    const int cx = static_cast<int>(std::floor(m_player.state.position.x / 16.0f));
    const int cz = static_cast<int>(std::floor(m_player.state.position.z / 16.0f));
    WorldGenerator generator(m_worldOptions);
    for (int z = -1; z <= 1; ++z) {
        for (int x = -1; x <= 1; ++x) {
            for (int y = 0; y < 3; ++y) {
                const ChunkCoordinate coord{cx + x, y, cz + z};
                if (!m_world->HasChunk(coord)) {
                    m_world->GetOrCreateChunk(coord) = generator.GenerateChunk(coord);
                }
            }
            const ChunkCoordinate capCoord{cx + x, 3, cz + z};
            if (!m_world->HasChunk(capCoord)) {
                m_world->GetOrCreateChunk(capCoord);
            }
        }
    }
}

void GameSession::Update(float deltaSeconds) {
    if (!m_initialized || m_world == nullptr) {
        return;
    }
    EnsureChunkResidentAroundPlayer();

    if (m_input != nullptr) {
        const InputState input = m_input->GetInputState();
        gameplay::CameraController controller;
        controller.Update(m_player, input, m_preferences, deltaSeconds);
        gameplay::Physics::Step(*m_world, m_player, deltaSeconds);
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
                            if (m_preferences.particles) m_particleBursts.push_back(result.targetPosition);
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
                    m_placeCooldown = 0.16f;
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
        gameplay::Physics::Step(*m_world, m_player, deltaSeconds);
        m_breakProgress = 0.0f;
        m_hasBreakTarget = false;
    }

    m_camera.position = glm::vec3(m_player.state.position.x,
                                  m_player.state.position.y + 0.72f,
                                  m_player.state.position.z);
    m_camera.yaw = m_player.state.yaw;
    m_camera.pitch = m_player.state.pitch;
}

void GameSession::Shutdown() noexcept {
    m_initialized = false;
    m_input = nullptr;
    m_world = nullptr;
    m_ownedWorld.reset();
    m_player = Player{};
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
