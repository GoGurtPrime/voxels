#pragma once

/**
 * @file app_state.hpp
 * @brief Minimal plain-data model of the high-level app lifecycle.
 *
 * @details Names the coarse phases (boot, menu, world setup, loading, play, pause, exit)
 *          without any behavior or dependencies. The shipping menu/gameplay flow is driven
 *          by `AppStateMachine`/`AppStateId` in state_machine.hpp; this header is a
 *          standalone descriptive model that is not wired into that runtime flow.
 */

#include <string>

namespace voxels {

/// Coarse lifecycle phase; a simplified view of the richer `AppStateId` set.
enum class AppState {
    Boot,
    MainMenu,
    WorldSetup,
    Loading,
    Playing,
    Paused,
    Exiting
};

/// Snapshot of the app's lifecycle: current phase, loaded world name, and run flag.
struct AppLifecycle {
    AppState current = AppState::Boot;
    std::string activeWorld; ///< Save/world name; empty when no world is loaded.
    bool running = true;     ///< Cleared to request main-loop exit.
};

} // namespace voxels
