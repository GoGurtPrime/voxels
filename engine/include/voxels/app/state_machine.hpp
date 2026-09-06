#pragma once

/**
 * @file state_machine.hpp
 * @brief Application-level state machine driving the boot -> menu -> gameplay flow.
 *
 * @details States are plain polymorphic objects with `OnEnter`/`OnExit`/`Update`/`Render`
 *          hooks so the loading, menu, and gameplay flow described in ARCHITECTURE.md can
 *          be composed and tested without rendering or platform code. The machine also
 *          supports an overlay stack (pause menu, settings, controls card) drawn above the
 *          base state. The app entry point drives it using menu selections from the UI
 *          layer and world generation progress.
 */

#include <memory>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "voxels/app/game_session.hpp"
#include "voxels/audio/audio_engine.hpp"
#include "voxels/app/menus.hpp"
#include "voxels/app/network_sync.hpp"
#include "voxels/app/save_manager.hpp"
#include "voxels/core/job_system.hpp"
#include "voxels/graphics/renderer.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/platform/platform_services.hpp"
#include "voxels/render/chunk_renderer.hpp"
#include "voxels/render/gameplay_hud.hpp"
#include "voxels/render/remote_player_renderer.hpp"
#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/world.hpp"
#include "voxels/world/world_options.hpp"

namespace voxels {

enum class AppStateId {
    Boot,
    MainMenu,
    WorldSelect,
    WorldCreation,
    LoadingScreen,
    JoinGame,
    JoinLoading,
    InGame,
    PauseMenu,
    Settings,
    ControlsCard,
    Error
};

class WorldCreationController;
class PauseMenuController;
class LoadingScreenModel;
class IPlatform;
class IPlayerUI;
class SaveManager;
class InGameState;
namespace networking { class GameClient; }
namespace networking { class GameServer; }

/// Shared service bundle handed to every state. All pointers are borrowed from the app
/// entry point (never owned); the request* callbacks defer machine mutations until the
/// current Update pass finishes.
struct AppContext {
    IPlatform* platform = nullptr;
    graphics::IGraphicsRenderer* renderer = nullptr;
    IPlayerUI* ui = nullptr;
    InputManager* input = nullptr;
    BlockRegistry* blockRegistry = nullptr;
    TextureAtlas* textureAtlas = nullptr;
    SaveManager* saveManager = nullptr;
    GamePreferences* preferences = nullptr;
    AudioEngine* audio = nullptr;
    IPlatformServices* platformServices = nullptr;
    networking::GameClient* networkClient = nullptr;
    networking::GameServer* networkServer = nullptr;
    std::unordered_map<std::string, SoundHandle> soundBank;
    InGameState* activeGame = nullptr;
    /// True on the very first launch (no settings.json present yet); MainMenuState uses this to
    /// present the dismissible controls card once (work_items/18 §4).
    bool firstRun = false;
    std::function<void(std::unique_ptr<class IAppState>)> requestTransition;
    std::function<void(std::unique_ptr<class IAppState>)> requestPushOverlay;
    std::function<void()> requestPopOverlay;
    std::function<void()> requestQuit;
    /// Reconnects the client to a remote host (disconnecting from the in-process server).
    std::function<bool(const std::string&, std::uint16_t)> connectRemote;
    /// Restores the loopback connection to the in-process server after a remote session ends.
    std::function<void()> resetNetworkToLocal;
};

/// Stable state name for logs and test assertions.
[[nodiscard]] std::string_view ToString(AppStateId id) noexcept;

/// Base class for every state managed by `AppStateMachine`. Concrete states override the
/// hooks they need; all hooks are optional no-ops by default.
class IAppState {
public:
    explicit IAppState(AppContext* context = nullptr) : m_context(context) {}
    virtual ~IAppState() = default;
    [[nodiscard]] virtual AppStateId GetId() const noexcept = 0;
    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void Update(double deltaSeconds) { (void)deltaSeconds; }
    virtual void Render() {}

protected:
    AppContext* m_context = nullptr;
};

/// Inert initial state; the app transitions out of it once services are wired up.
class BootState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::Boot; }
};

/// Title screen: Play/Join/Settings/Quit over a slowly orbiting background camera. On
/// first run it pushes the `ControlsCardState` overlay automatically.
class MainMenuState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::MainMenu; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    float m_elapsedSeconds = 0.0f;
    Camera m_camera{};
};

/// Save-slot browser: lists saves from the `SaveManager`, loads the selection, and deletes
/// saves behind a type-the-name confirmation prompt.
class WorldSelectState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::WorldSelect; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    std::vector<SaveSlot> m_saves;
    int m_selectedSave = -1;
    bool m_confirmDelete = false;
    std::string m_deleteConfirmation;
};

/// New-world form (name, seed text, gameplay toggles) backed by `WorldCreationController`;
/// on confirm it persists the new save and hands off to `LoadingScreenState`.
class WorldCreationState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::WorldCreation; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    WorldCreationController* GetController() noexcept;
    std::unique_ptr<WorldCreationController> m_controller;
    std::string m_seedText;
    std::string m_error;
};

/// Generates (or restores) the world for a named save while showing phase-based progress.
/// Chunk generation runs on a private `JobSystem` and is integrated a couple of chunks per
/// frame; saves from a newer generator version fail into `ErrorState` instead of loading.
class LoadingScreenState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::LoadingScreen; }

    void SetSaveManager(ISaveManager& manager) noexcept { m_saveManager = &manager; }
    void SetSaveName(std::string saveName) noexcept { m_saveName = std::move(saveName); }
    void SetWorldOptions(WorldOptions options) noexcept { m_options = std::move(options); }
    /// Transfers ownership of the generated world to the caller (typically `InGameState`).
    [[nodiscard]] std::unique_ptr<World> ReleaseGeneratedWorld() noexcept { return std::move(m_world); }

    /// Re-reads the save metadata (seed/generator version), then builds the initial chunk
    /// queue and enqueues generation jobs. Called automatically by `OnEnter`.
    void RunGeneration();
    void OnEnter() override;
    void OnExit() override;
    void Update(double deltaSeconds) override;
    void Render() override;
    [[nodiscard]] const World& GetWorld() const noexcept { return *m_world; }
    [[nodiscard]] Vec3I GetSpawnPosition() const noexcept { return m_spawnPosition; }
    [[nodiscard]] GenerationPhase GetPhase() const noexcept { return m_phase; }
    /// 0..1, quantized by phase (0.25 per completed phase); not per-chunk granular.
    [[nodiscard]] float GetProgress() const noexcept;

private:
    struct PendingGenerationJob {
        ChunkCoordinate coordinate{};
        std::future<Chunk> result;
    };

    ISaveManager* m_saveManager = nullptr;
    std::string m_saveName;
    WorldOptions m_options{};
    std::unique_ptr<World> m_world;
    GenerationPhase m_phase = GenerationPhase::Shape;
    Vec3I m_spawnPosition{0, 1, 0};
    bool m_generationComplete = false;
    std::vector<ChunkCoordinate> m_generationQueue;
    std::unique_ptr<JobSystem> m_generationJobs;
    std::vector<PendingGenerationJob> m_generationResults;
    std::size_t m_generatedChunks = 0;
};

/// Direct-IP join screen: the player enters the host address and UDP port, and a connection
/// attempt hands off to `JoinLoadingState` (work_items/16_multiplayer_runtime_integration.md).
class JoinGameState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::JoinGame; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    std::string m_host = "127.0.0.1";
    std::string m_portText = "27015";
    std::string m_error;
};

/// Waits for the remote handshake, receives the host's world info, and integrates streamed
/// chunks until the spawn area is walkable, then enters a remote `InGameState`.
class JoinLoadingState final : public IAppState {
public:
    JoinLoadingState(AppContext* context, std::string endpointLabel)
        : IAppState(context), m_endpointLabel(std::move(endpointLabel)) {}
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::JoinLoading; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    void FailWith(std::string title, std::string detail);

    static constexpr double kHandshakeTimeoutSeconds = 8.0;
    static constexpr double kStreamStallTimeoutSeconds = 15.0;
    /// Columns (radius 1 around spawn) that must be fully streamed before the player spawns.
    static constexpr int kRequiredColumnRadius = 1;

    std::string m_endpointLabel;
    std::unique_ptr<World> m_world;
    RemoteChunkApplier m_applier;
    double m_elapsedSeconds = 0.0;
    double m_lastProgressSeconds = 0.0;
    std::size_t m_lastAppliedChunks = 0;
};

/// The playable gameplay state. Drives a `GameSession` (player movement, physics, block
/// interaction) and renders it through `ChunkRenderer` plus the HUD and remote-player
/// renderers. Autosaves local sessions periodically on a background job, forwards
/// networked block updates, and supports remote (server-streamed) sessions.
class InGameState final : public IAppState {public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::InGame; }

    void SetBlockRegistry(BlockRegistry* registry) noexcept { m_registry = registry; }
    void SetTextureAtlas(TextureAtlas* atlas) noexcept { m_atlas = atlas; }
    void SetWorldOptions(WorldOptions options) noexcept { m_options = options; }
    void SetInputManager(InputManager* inputManager) noexcept { m_inputManager = inputManager; }
    void SetPlayerCamera(Camera* camera) noexcept { m_cameraOverride = camera; }
    void SetPlatform(IPlatform* platform) noexcept { m_platform = platform; }
    void SetActiveSave(GameSave save) { m_activeSave = std::move(save); }
    /// Adopts a world generated by `LoadingScreenState`, skipping in-state generation.
    void SetPreparedWorld(std::unique_ptr<World> world) noexcept { m_preparedWorld = std::move(world); }
    /// Marks this session as a remote join: the world is streamed from the host server and
    /// nothing is generated or persisted locally.
    void SetRemoteSession(bool remote) noexcept { m_remoteSession = remote; }
    void SetRemoteSpawn(const Vec3& spawn) noexcept { m_remoteSpawn = spawn; m_hasRemoteSpawn = true; }
    void ApplyPreferences(const GamePreferences& preferences) noexcept { m_session.SetPreferences(preferences); }

    void OnEnter() override;
    void OnExit() override;
    void Update(double deltaSeconds) override;
    void Render() override;

    [[nodiscard]] const World& GetWorld() const noexcept { return m_session.GetWorld(); }
    [[nodiscard]] graphics::ChunkRenderer* GetChunkRenderer() const noexcept { return m_chunkRenderer.get(); }

private:
    void GenerateInitialWorld();
    void StartAutosave();
    void ApplyNetworkedBlockUpdates();

    BlockRegistry* m_registry = nullptr;
    TextureAtlas* m_atlas = nullptr;
    InputManager* m_inputManager = nullptr;
    Camera* m_cameraOverride = nullptr;
    IPlatform* m_platform = nullptr;
    WorldOptions m_options{};

    GameSession m_session;
    std::unique_ptr<JobSystem> m_jobSystem;
    std::unique_ptr<graphics::ChunkRenderer> m_chunkRenderer;
    std::unique_ptr<graphics::GameplayHudRenderer> m_hudRenderer;
    std::unique_ptr<graphics::RemotePlayerRenderer> m_remotePlayerRenderer;
    RemoteChunkApplier m_remoteChunkApplier;
    float m_elapsedSeconds = 0.0f;
    float m_autosaveSeconds = 0.0f;
    std::future<bool> m_autosaveFuture;
    bool m_screenshotPressed = false;
    bool m_worldGenerated = false;
    bool m_remoteSession = false;
    bool m_hasRemoteSpawn = false;
    Vec3 m_remoteSpawn{};
    GameSave m_activeSave{};
    std::unique_ptr<World> m_preparedWorld;
};

/// Pause overlay pushed above `InGameState`. Offers resume/settings/controls, world
/// visibility toggling (republished to the hosting server), and save-and-quit paths;
/// remote sessions instead get leave-server options that reset networking to loopback.
class PauseMenuState final : public IAppState {
public:
    PauseMenuState(AppContext* context = nullptr, GameSave activeSave = {}, bool remoteSession = false)
        : IAppState(context), m_activeSave(std::move(activeSave)), m_remoteSession(remoteSession) {}
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::PauseMenu; }
    void OnEnter() override;
    void OnExit() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    GameSave m_activeSave;
    bool m_remoteSession = false;
};

/// Settings overlay editing a pending copy of the shared `GamePreferences`; Apply commits
/// to the live preferences, the running game/audio, and settings.json on disk.
class SettingsState final : public IAppState {
public:
    explicit SettingsState(AppContext* context) : IAppState(context) {}
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::Settings; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    GamePreferences m_pending{};
    RendererBackend m_activeRenderer = RendererBackend::OpenGL;
};

/// Dismissible overlay listing the core keybinds. Shown once automatically on first launch
/// (AppContext::firstRun) and reopenable at any time from the pause menu (work_items/18 §4).
class ControlsCardState final : public IAppState {
public:
    explicit ControlsCardState(AppContext* context) : IAppState(context) {}
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::ControlsCard; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;
};

/// Terminal error screen showing a title/detail pair with a single path back to the main menu.
class ErrorState final : public IAppState {
public:
    ErrorState(AppContext* context, std::string title, std::string detail)
        : IAppState(context), m_title(std::move(title)), m_detail(std::move(detail)) {}
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::Error; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    std::string m_title;
    std::string m_detail;
};

/// Injects the module-wide renderer that state `Render()` implementations draw through;
/// the app sets it after GL init and resets it to nullptr before renderer teardown.
void SetGlobalRenderer(voxels::graphics::IGraphicsRenderer* renderer) noexcept;

/// Owns exactly one active `IAppState` (plus an overlay stack) and guarantees `OnExit`/
/// `OnEnter` are called in that order on every transition. Keeps a chronological log of
/// transitions (state id plus "Enter"/"Exit") to make lifecycle ordering directly
/// verifiable in tests.
class AppStateMachine {
public:
    struct TransitionRecord {
        AppStateId state;
        bool entered; // true = OnEnter, false = OnExit
    };

    /// Sets the initial state without exiting any previous state. Calls `OnEnter`.
    void Start(std::unique_ptr<IAppState> state);

    /// Exits the current state (if any) and enters `state`, in that order. Pops and exits
    /// every open overlay first.
    void TransitionTo(std::unique_ptr<IAppState> state);

    /// Deferred `TransitionTo`: applied at the end of the next `Update`, so states can
    /// safely request transitions from inside their own hooks.
    void RequestTransition(std::unique_ptr<IAppState> state);
    /// Enters `state` on top of the current state; the base state keeps rendering beneath it.
    void PushOverlay(std::unique_ptr<IAppState> state);
    void RequestPushOverlay(std::unique_ptr<IAppState> state);
    /// Exits and destroys the topmost overlay; no-op when none is open.
    void PopOverlay();
    void RequestPopOverlay();

    /// Updates only the topmost overlay (or the base state when none), then applies any
    /// deferred transition, overlay pop, and overlay push, in that order.
    void Update(double deltaSeconds);
    /// Renders the base state, then overlays in push order (topmost last).
    void Render();

    /// Exits the current state (if any) without entering a replacement. Must be called before
    /// the GL context/renderer/platform are torn down, since a state's `OnExit` may release GPU
    /// resources (e.g. `InGameState`'s `ChunkRenderer`) that require a live context.
    void Shutdown();

    [[nodiscard]] IAppState* GetCurrentState() const noexcept { return m_current.get(); }
    /// Returns the visible state, preferring the topmost overlay over the base state.
    [[nodiscard]] IAppState* GetVisibleState() const noexcept { return m_overlays.empty() ? m_current.get() : m_overlays.back().get(); }
    [[nodiscard]] const std::vector<TransitionRecord>& GetTransitionLog() const noexcept { return m_log; }

private:
    std::unique_ptr<IAppState> m_current;
    std::unique_ptr<IAppState> m_pending;
    std::vector<std::unique_ptr<IAppState>> m_overlays;
    std::unique_ptr<IAppState> m_pendingOverlay;
    bool m_popOverlayRequested = false;
    std::vector<TransitionRecord> m_log;
};

} // namespace voxels
