#pragma once

/*
 * Scope: High-level app lifecycle state model.
 *
 * The app uses explicit states to describe the loading flow, main menu, pause menu, and
 * active gameplay. This structure allows the game runtime and tool UI to evolve without
 * entangling state transitions with rendering or input code.
 *
 * Relation to the rest of the codebase: the application layer transitions between these
 * states while the engine services remain reusable across game and editor targets.
 */

#include <string>

namespace voxels {

enum class AppState {
    Boot,
    MainMenu,
    WorldSetup,
    Loading,
    Playing,
    Paused,
    Exiting
};

struct AppLifecycle {
    AppState current = AppState::Boot;
    std::string activeWorld;
    bool running = true;
};

} // namespace voxels
