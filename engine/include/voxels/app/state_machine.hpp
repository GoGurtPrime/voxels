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
#include <string>
#include <string_view>
#include <vector>

#include "voxels/app/menus.hpp"
#include "voxels/app/save_manager.hpp"
#include "voxels/graphics/gl_renderer.hpp"
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
    PauseMenu
};

class WorldCreationController;
class PauseMenuController;
class LoadingScreenModel;

[[nodiscard]] std::string_view ToString(AppStateId id) noexcept;

/// Base class for every state managed by `AppStateMachine`. Concrete states override the
/// hooks they need; all hooks are optional no-ops by default.
class IAppState {
public:
    virtual ~IAppState() = default;
    [[nodiscard]] virtual AppStateId GetId() const noexcept = 0;
    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void Update(double deltaSeconds) { (void)deltaSeconds; }
    virtual void Render() {}
};

class BootState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::Boot; }
};

class MainMenuState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::MainMenu; }
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    float m_elapsedSeconds = 0.0f;
};

class WorldSelectState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::WorldSelect; }
};

class WorldCreationState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::WorldCreation; }
};

class LoadingScreenState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::LoadingScreen; }

    void SetSaveManager(ISaveManager& manager) noexcept { m_saveManager = &manager; }
    void SetSaveName(std::string saveName) noexcept { m_saveName = std::move(saveName); }
    void SetWorldOptions(WorldOptions options) noexcept { m_options = std::move(options); }

    void RunGeneration();
    [[nodiscard]] const World& GetWorld() const noexcept { return m_world; }
    [[nodiscard]] Vec3I GetSpawnPosition() const noexcept { return m_spawnPosition; }
    [[nodiscard]] GenerationPhase GetPhase() const noexcept { return m_phase; }
    [[nodiscard]] float GetProgress() const noexcept;

private:
    ISaveManager* m_saveManager = nullptr;
    std::string m_saveName;
    WorldOptions m_options{};
    World m_world;
    GenerationPhase m_phase = GenerationPhase::Shape;
    Vec3I m_spawnPosition{0, 1, 0};
};

class InGameState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::InGame; }
    void Update(double deltaSeconds) override;
    void Render() override;

private:
    float m_elapsedSeconds = 0.0f;
    Camera m_camera{};
};

class PauseMenuState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::PauseMenu; }
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

    void Update(double deltaSeconds);
    void Render();

    [[nodiscard]] IAppState* GetCurrentState() const noexcept { return m_current.get(); }
    [[nodiscard]] const std::vector<TransitionRecord>& GetTransitionLog() const noexcept { return m_log; }

private:
    std::unique_ptr<IAppState> m_current;
    std::vector<TransitionRecord> m_log;
};

} // namespace voxels
