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
};

class InGameState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::InGame; }
};

class PauseMenuState final : public IAppState {
public:
    [[nodiscard]] AppStateId GetId() const noexcept override { return AppStateId::PauseMenu; }
};

/// Owns exactly one active `IAppState` at a time and guarantees `OnExit`/`OnEnter` are called
/// in that order on every transition. Keeps a chronological log of transitions (state id plus
/// "Enter"/"Exit") to make lifecycle ordering directly verifiable in tests.
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
