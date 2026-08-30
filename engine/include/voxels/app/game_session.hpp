#pragma once

#include <memory>
#include <string>

#include "voxels/core/game_types.hpp"
#include "voxels/gameplay/camera_controller.hpp"
#include "voxels/gameplay/block_interaction.hpp"
#include "voxels/gameplay/physics.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/world/world.hpp"

namespace voxels {

enum class GameplaySoundEventType { Break, Place, Footstep, Jump, Land, Splash };

struct GameplaySoundEvent {
    GameplaySoundEventType type = GameplaySoundEventType::Break;
    Vec3I position{};
    BlockId blockId = static_cast<BlockId>(BlockType::Air);
};

class GameSession {
public:
    GameSession();
    explicit GameSession(World* world);

    void SetWorld(World* world) noexcept;
    void AdoptWorld(std::unique_ptr<World> world) noexcept;
    void SetWorldOptions(const WorldOptions& options) noexcept;
    void SetInputManager(InputManager* inputManager) noexcept;
    void SetBlockRegistry(const BlockRegistry* registry) noexcept;
    void SetPlayerSpawn(const Vec3& spawn);
    void SetPlayerSpawn(const Vec3I& spawn);
    void RestorePlayerState(const PlayerState& state) noexcept;

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
    [[nodiscard]] const RaycastHit& GetTarget() const noexcept { return m_target; }
    [[nodiscard]] float GetBreakProgress() const noexcept { return m_breakProgress; }
    [[nodiscard]] const std::vector<Vec3I>& GetEditedBlocks() const noexcept { return m_editedBlocks; }
    void ClearEditedBlocks() noexcept { m_editedBlocks.clear(); }
    [[nodiscard]] const std::vector<GameplaySoundEvent>& GetSoundEvents() const noexcept { return m_soundEvents; }
    void ClearSoundEvents() noexcept { m_soundEvents.clear(); }
    [[nodiscard]] const std::vector<Vec3I>& GetParticleBursts() const noexcept { return m_particleBursts; }
    void ClearParticleBursts() noexcept { m_particleBursts.clear(); }
    [[nodiscard]] const std::string& GetSelectedItemLabel() const noexcept { return m_selectedItemLabel; }
    [[nodiscard]] float GetSelectedItemLabelAge() const noexcept { return m_selectedItemLabelAge; }
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
    bool m_playerStateRestored = false;
    bool m_initialized = false;
    const BlockRegistry* m_registry = nullptr;
    gameplay::BlockInteraction m_blockInteraction{};
    RaycastHit m_target{};
    Vec3I m_breakTarget{};
    BlockId m_breakTargetBlockId = static_cast<BlockId>(BlockType::Air);
    bool m_hasBreakTarget = false;
    float m_breakProgress = 0.0f;
    float m_placeCooldown = 0.0f;
    float m_footstepSeconds = 0.0f;
    bool m_wasInWater = false;
    std::vector<Vec3I> m_editedBlocks;
    std::vector<GameplaySoundEvent> m_soundEvents;
    std::vector<Vec3I> m_particleBursts;
    gameplay::ItemStack m_lastSelectedStack{};
    std::string m_selectedItemLabel;
    float m_selectedItemLabelAge = 0.0f;
};

} // namespace voxels
