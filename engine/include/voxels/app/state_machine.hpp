#pragma once

/*
 * Scope: Application-level state machine driving the boot -> menu -> gameplay flow.
 *
 * States are plain polymorphic objects with `OnEnter`/`OnExit`/`Update`/`Render` hooks so the
 * loading, menu, and gameplay flow described in ARCHITECTURE.md can be composed and tested
 * without depending on rendering or platform code.
 *
 * Relation to the rest of the codebase: the app entry point drives this machine using input
 * from the UI layer (menu selections) and world generation progress callbacks.
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
#include "voxels/app/save_manager.hpp"
#include "voxels/core/job_system.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/render/chunk_renderer.hpp"
#include "voxels/render/gameplay_hud.hpp"
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
    InGame,
    PauseMenu,
    Settings,
    Error
};

class WorldCreationController;
class PauseMenuController;
class LoadingScreenModel;
class IPlatform;
class ImGuiUIManager;
class SaveManager;
class InGameState;

struct AppContext {
    IPlatform* platform = nullptr;
    graphics::GLRenderer* renderer = nullptr;
    ImGuiUIManager* ui = nullptr;
    InputManager* input = nullptr;
    BlockRegistry* blockRegistry = nullptr;
    TextureAtlas* textureAtlas = nullptr;
    SaveManager* saveManager = nullptr;
    GamePreferences* preferences = nullptr;
    AudioEngine* audio = nullptr;
    std::unordered_map<std::string, SoundHandle> soundBank;
    InGameState* activeGame = nullptr;
    std::function<void(std::unique_ptr<class IAppState>)> requestTransition;
    std::function<void(std::unique_ptr<class IAppState>)> requestPushOverlay;
    std::function<void()> requestPopOverlay;
    std::function<void()> requestQuit;
};

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

class BootState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::Boot; }
};

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

class LoadingScreenState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::LoadingScreen; }

    void SetSaveManager(ISaveManager& manager) noexcept { m_saveManager = &manager; }
    void SetSaveName(std::string saveName) noexcept { m_saveName = std::move(saveName); }
    void SetWorldOptions(WorldOptions options) noexcept { m_options = std::move(options); }
    [[nodiscard]] std::unique_ptr<World> ReleaseGeneratedWorld() noexcept { return std::move(m_world); }

    void RunGeneration();
    void OnEnter() override;
    void OnExit() override;
    void Update(double deltaSeconds) override;
    void Render() override;
    [[nodiscard]] const World& GetWorld() const noexcept { return *m_world; }
    [[nodiscard]] Vec3I GetSpawnPosition() const noexcept { return m_spawnPosition; }
    [[nodiscard]] GenerationPhase GetPhase() const noexcept { return m_phase; }
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

/// The real playable gameplay state: generates a bounded voxel world and renders it through
/// `ChunkRenderer` (work_items/05_chunk_mesh_pipeline_and_world_rendering.md). A full player
/// controller (movement, mouse-look, collision) is work item 06; until then this state drives
/// a slow automatic flythrough camera so the generated world is directly observable.
class InGameState final : public IAppState {
public:
    using IAppState::IAppState;
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::InGame; }

    void SetBlockRegistry(BlockRegistry* registry) noexcept { m_registry = registry; }
    void SetTextureAtlas(TextureAtlas* atlas) noexcept { m_atlas = atlas; }
    void SetWorldOptions(WorldOptions options) noexcept { m_options = options; }
    void SetInputManager(InputManager* inputManager) noexcept { m_inputManager = inputManager; }
    void SetPlayerCamera(Camera* camera) noexcept { m_cameraOverride = camera; }
    void SetPlatform(IPlatform* platform) noexcept { m_platform = platform; }
    void SetActiveSave(GameSave save) { m_activeSave = std::move(save); }
    void SetPreparedWorld(std::unique_ptr<World> world) noexcept { m_preparedWorld = std::move(world); }
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
    float m_elapsedSeconds = 0.0f;
    float m_autosaveSeconds = 0.0f;
    std::future<bool> m_autosaveFuture;
    bool m_screenshotPressed = false;
    bool m_worldGenerated = false;
    GameSave m_activeSave{};
    std::unique_ptr<World> m_preparedWorld;
};

class PauseMenuState final : public IAppState {
public:
    PauseMenuState(AppContext* context = nullptr, GameSave activeSave = {})
        : IAppState(context), m_activeSave(std::move(activeSave)) {}
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::PauseMenu; }
    void OnEnter() override;
    void OnExit() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    GameSave m_activeSave;
};

class SettingsState final : public IAppState {
public:
    explicit SettingsState(AppContext* context) : IAppState(context) {}
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::Settings; }
    void OnEnter() override;
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    GamePreferences m_pending{};
};

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

/// Owns exactly one active `IAppState` at a time and guarantees `OnExit`/`OnEnter` are called
/// in that order on every transition. Keeps a chronological log of transitions (state id plus
/// "Enter"/"Exit") to make lifecycle ordering directly verifiable in tests.
void SetGlobalRenderer(voxels::graphics::GLRenderer* renderer) noexcept;

class AppStateMachine {
public:
    struct TransitionRecord {
        AppStateId state;
        bool entered; // true = OnEnter, false = OnExit
    };

    /// Sets the initial state without exiting any previous state. Calls `OnEnter`.
    void Start(std::unique_ptr<IAppState> state);

    /// Exits the current state (if any) and enters `state`, in that order.
    void TransitionTo(std::unique_ptr<IAppState> state);

    void RequestTransition(std::unique_ptr<IAppState> state);
    void PushOverlay(std::unique_ptr<IAppState> state);
    void RequestPushOverlay(std::unique_ptr<IAppState> state);
    void PopOverlay();
    void RequestPopOverlay();

    void Update(double deltaSeconds);
    void Render();

    /// Exits the current state (if any) without entering a replacement. Must be called before
    /// the GL context/renderer/platform are torn down, since a state's `OnExit` may release GPU
    /// resources (e.g. `InGameState`'s `ChunkRenderer`) that require a live context.
    void Shutdown();

    [[nodiscard]] IAppState* GetCurrentState() const noexcept { return m_current.get(); }
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
