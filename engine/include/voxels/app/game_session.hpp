#pragma once

/**
 * @file game_session.hpp
 * @brief Gameplay simulation façade driven by the InGame app state.
 *
 * @details Owns or borrows the active `World` and steps one frame of gameplay per
 *          `Update`: player movement/physics, block targeting/break/place, chunk
 *          streaming through the `JobSystem`, achievement unlocks via
 *          `IPlatformServices`, and replication through the optional network client.
 *          Emits per-frame event buffers (sound events, particle bursts, edited blocks,
 *          arrived/removed chunks) that the render/audio layers consume and clear.
 */

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "voxels/core/job_system.hpp"
#include "voxels/core/game_types.hpp"
#include "voxels/gameplay/camera_controller.hpp"
#include "voxels/gameplay/block_interaction.hpp"
#include "voxels/gameplay/item_drop.hpp"
#include "voxels/gameplay/physics.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/platform/platform_services.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/world/world.hpp"

namespace voxels {

/// Category of a gameplay-triggered sound; the audio layer maps these to loaded clips.
enum class GameplaySoundEventType { Break, Place, Footstep, Jump, Land, Splash };

/// One frame-local sound trigger; consumed via `GetSoundEvents`/`ClearSoundEvents`.
struct GameplaySoundEvent {
    GameplaySoundEventType type = GameplaySoundEventType::Break;
    Vec3I position{};                                  ///< Block coordinates of the event.
    BlockId blockId = static_cast<BlockId>(BlockType::Air); ///< Block involved (surface material for footsteps).
};

/// Single-player-perspective simulation session. All collaborator pointers are borrowed
/// (never owned) except a `World` adopted via `AdoptWorld`. Not thread-safe; drive it from
/// the main loop only — its own background work goes through the injected `JobSystem`.
class GameSession {
public:
    /// Creates a session that owns a fresh empty `World`.
    GameSession();
    /// Wraps an externally owned world; falls back to an owned empty world if null.
    explicit GameSession(World* world);

    /// Points the session at an externally owned world (non-owning); null reverts to the
    /// internally owned world, creating one if needed.
    void SetWorld(World* world) noexcept;
    /// Takes ownership of `world` and makes it active; null adopts a fresh empty world.
    void AdoptWorld(std::unique_ptr<World> world) noexcept;
    void SetWorldOptions(const WorldOptions& options) noexcept;
    void SetInputManager(InputManager* inputManager) noexcept;
    /// When connected, block edits and player movement are replicated to this client.
    void SetNetworkClient(networking::GameClient* client) noexcept { m_networkClient = client; }
    /// Borrows the hosted server's authoritative drop set. Null restores session-local ownership.
    void SetAuthoritativeItemDrops(gameplay::ItemDropSimulation* itemDrops) noexcept;
    /// Remote worlds are streamed from the host server: no local generation or eviction.
    void SetRemoteWorld(bool remote) noexcept { m_remoteWorld = remote; }
    void SetBlockRegistry(const BlockRegistry* registry) noexcept;
    /// Chunk generation jobs are queued here; without one, no new chunks are generated.
    void SetJobSystem(JobSystem* jobSystem) noexcept { m_jobSystem = jobSystem; }
    /// Optional: when set, real break/place/exploration events unlock the matching launch
    /// achievement (work_items/18_packaging_distribution_and_platform_services.md §7).
    void SetPlatformServices(IPlatformServices* platformServices) noexcept { m_platformServices = platformServices; }
    /// Teleports the player to `spawn` (world-space, feet at capsule center), zeroing
    /// velocity and suppressing the automatic surface-scan spawn in `Initialize`.
    void SetPlayerSpawn(const Vec3& spawn);
    void SetPlayerSpawn(const Vec3I& spawn);
    /// Restores a previously saved player state verbatim; `Initialize` then skips spawn placement.
    void RestorePlayerState(const PlayerState& state) noexcept;

    /// Initializes the world, generates a starter 3x3-column area (local worlds only),
    /// places the player on the surface unless a spawn/restore was provided, and aims the camera.
    void Initialize();
    /// Advances one frame: streaming, input-driven movement, physics, block interaction,
    /// achievements, event-buffer emission, and network replication. No-op before `Initialize`.
    void Update(float deltaSeconds);
    /// Blocks until pending generation jobs finish, then releases the world and all borrowed pointers.
    void Shutdown() noexcept;

    /// Never null: lazily creates an owned world if none is attached.
    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;
    [[nodiscard]] Player& GetPlayer() noexcept;
    [[nodiscard]] const Player& GetPlayer() const noexcept;
    [[nodiscard]] Camera& GetCamera() noexcept;
    [[nodiscard]] const Camera& GetCamera() const noexcept;
    [[nodiscard]] const GamePreferences& GetPreferences() const noexcept { return m_preferences; }
    /// Raycast result for the block the player is currently looking at (updated each frame).
    [[nodiscard]] const RaycastHit& GetTarget() const noexcept { return m_target; }
    /// 0..1 fraction of the current block-break hold; resets when the target changes.
    [[nodiscard]] float GetBreakProgress() const noexcept { return m_breakProgress; }
    /// True when the camera eye (not just the feet) is inside a liquid block this frame; drives
    /// the underwater render overlay without granting the UI layer any physics authority.
    [[nodiscard]] bool IsEyeSubmerged() const noexcept { return m_eyeSubmerged; }
    /// 0..1 fraction of the player's AABB height currently occupied by liquid blocks.
    [[nodiscard]] float GetSubmersionFraction() const noexcept { return m_submersionFraction; }
    /// Block positions edited since the caller last cleared; drives chunk remeshing.
    [[nodiscard]] const std::vector<Vec3I>& GetEditedBlocks() const noexcept { return m_editedBlocks; }
    void ClearEditedBlocks() noexcept { m_editedBlocks.clear(); }
    [[nodiscard]] const std::vector<GameplaySoundEvent>& GetSoundEvents() const noexcept { return m_soundEvents; }
    void ClearSoundEvents() noexcept { m_soundEvents.clear(); }
    /// Block positions that should spawn a break-particle burst (only when particles are enabled).
    [[nodiscard]] const std::vector<Vec3I>& GetParticleBursts() const noexcept { return m_particleBursts; }
    void ClearParticleBursts() noexcept { m_particleBursts.clear(); }
    /// Display name of the newly selected hotbar item; empty when nothing is selected.
    [[nodiscard]] const std::string& GetSelectedItemLabel() const noexcept { return m_selectedItemLabel; }
    /// Seconds since the hotbar selection changed; the HUD uses this to fade the label.
    [[nodiscard]] float GetSelectedItemLabelAge() const noexcept { return m_selectedItemLabelAge; }
    /// Returns and clears the chunks that finished streaming in since the last call.
    [[nodiscard]] std::vector<ChunkCoordinate> ConsumeArrivedChunks();
    /// Returns and clears the chunks evicted by streaming since the last call.
    [[nodiscard]] std::vector<ChunkCoordinate> ConsumeRemovedChunks();
    void SetPreferences(const GamePreferences& preferences) noexcept { m_preferences = preferences; }
    /// Ground item entities (spawned by block breaks or manual drops); the render layer draws
    /// these as small bobbing cubes and the HUD does not otherwise track them.
    [[nodiscard]] const std::vector<gameplay::ItemDrop>& GetItemDrops() const noexcept { return m_itemDrops->Drops(); }
    [[nodiscard]] const gameplay::ItemDropSimulationMetrics& GetItemDropMetrics() const noexcept {
        return m_itemDrops->GetMetrics();
    }
    /// Applies an authoritative terrain edit to the bounded item-drop recovery query.
    void ResolveItemDropsAfterBlockEdit(const Vec3I& editedBlock);
    /// Removes up to `count` items from inventory `slot` and spawns them as a ground item drop
    /// tossed a short distance in front of the player. Returns the count actually dropped (0 when
    /// the slot is empty, out of range, or `count` is not positive).
    int DropInventorySlot(std::size_t slot, int count);

private:
    void EnsureChunkResidentAroundPlayer();
    void ApplyCompletedChunkJobs();

    static constexpr std::size_t kMaxQueuedGenerationJobs = 2;
    static constexpr std::size_t kMaxCompletedChunksPerFrame = 1;
    static constexpr int kTerrainSectionCount = 8;
    static constexpr int kStreamingHysteresisChunks = 2;

    struct PendingChunkJob {
        ChunkCoordinate coordinate{};
        std::future<Chunk> result;
    };

    std::unique_ptr<World> m_ownedWorld;
    World* m_world = nullptr;
    InputManager* m_input = nullptr;
    networking::GameClient* m_networkClient = nullptr;
    bool m_remoteWorld = false;
    JobSystem* m_jobSystem = nullptr;
    Player m_player{};
    Camera m_camera{};
    GamePreferences m_preferences{};
    WorldOptions m_worldOptions{};
    Vec3 m_spawnPosition{0.0f, 1.9f, 0.0f};
    bool m_spawnExplicitlySet = false;
    bool m_playerStateRestored = false;
    bool m_initialized = false;
    const BlockRegistry* m_registry = nullptr;
    IPlatformServices* m_platformServices = nullptr;
    gameplay::BlockInteraction m_blockInteraction{};
    RaycastHit m_target{};
    Vec3I m_breakTarget{};
    BlockId m_breakTargetBlockId = static_cast<BlockId>(BlockType::Air);
    bool m_hasBreakTarget = false;
    float m_breakProgress = 0.0f;
    float m_placeCooldown = 0.0f;
    float m_footstepSeconds = 0.0f;
    bool m_wasInWater = false;
    bool m_eyeSubmerged = false;
    float m_submersionFraction = 0.0f;
    std::vector<Vec3I> m_editedBlocks;
    std::vector<GameplaySoundEvent> m_soundEvents;
    std::vector<Vec3I> m_particleBursts;
    gameplay::ItemDropSimulation m_ownedItemDrops;
    gameplay::ItemDropSimulation* m_itemDrops = &m_ownedItemDrops;
    bool m_itemDropsAdvancedExternally = false;
    bool m_dropItemHeldLastFrame = false;
    gameplay::ItemStack m_lastSelectedStack{};
    std::string m_selectedItemLabel;
    float m_selectedItemLabelAge = 0.0f;
    std::vector<PendingChunkJob> m_pendingChunkJobs;
    std::vector<ChunkCoordinate> m_arrivedChunks;
    std::vector<ChunkCoordinate> m_removedChunks;
};

} // namespace voxels
