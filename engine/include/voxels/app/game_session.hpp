#pragma once

#include <memory>

#include "voxels/core/game_types.hpp"
#include "voxels/gameplay/camera_controller.hpp"
#include "voxels/gameplay/physics.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/world/world.hpp"

namespace voxels {

class GameSession {
public:
    GameSession();
    explicit GameSession(World* world);

    void SetWorld(World* world) noexcept;
    void SetWorldOptions(const WorldOptions& options) noexcept;
    void SetInputManager(InputManager* inputManager) noexcept;
    void SetPlayerSpawn(const Vec3& spawn);
    void SetPlayerSpawn(const Vec3I& spawn);

    void Initialize();
    void Update(float deltaSeconds);
    void Shutdown() noexcept;

    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;
    [[nodiscard]] Player& GetPlayer() noexcept;
    [[nodiscard]] const Player& GetPlayer() const noexcept;
    [[nodiscard]] Camera& GetCamera() noexcept;
    [[nodiscard]] const Camera& GetCamera() const noexcept;
    [[nodiscard]] const GamePreferences& GetPreferences() const noexcept { return m_preferences; }
    void SetPreferences(const GamePreferences& preferences) noexcept { m_preferences = preferences; }

private:
    void EnsureChunkResidentAroundPlayer();

    std::unique_ptr<World> m_ownedWorld;
    World* m_world = nullptr;
    InputManager* m_input = nullptr;
    Player m_player{};
    Camera m_camera{};
    GamePreferences m_preferences{};
    WorldOptions m_worldOptions{};
    Vec3 m_spawnPosition{0.0f, 1.9f, 0.0f};
    bool m_spawnExplicitlySet = false;
    bool m_initialized = false;
};

} // namespace voxels
