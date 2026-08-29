# Work Item 12: Persistence & Runtime Integration (Game Loop)

## 🎯 Objective & Overview
Wire the previously isolated subsystems (platform, input, renderer, world/worldgen, audio, app state machine, networking, gameplay/physics from `11`) into one real, continuously-running game loop inside `app/src/main.cpp`, and implement full world+player save/load so a session can be closed and resumed without data loss. Prior work items were each verified in isolation; this item's entire purpose is the *integration* that was missing, plus the persistence format needed to make saves real.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `03_platform_windowing_sdl2`, `05_graphics_renderer_abstraction`, `06_world_voxel_core`, `07_world_generation_pipeline`, `09_app_lifecycle_ui_and_menus`, `10_networking_and_server_client`, `11_gameplay_player_and_physics`
* **Target Subsystems:** `app/src/main.cpp`, `engine/include/voxels/app/save_manager.hpp` (extend), `engine/include/voxels/world/world_serialization.hpp`

---

## 📋 Detailed Task Breakdown
1. **Real Fixed-Timestep Game Loop (`app/src/main.cpp`):**
   * Replace the current "construct subsystems, print a line, exit" scaffold with an actual loop: `while (running) { platform->PollEvents(); input->Update(dt); player.Update(dt, world); localServer.Tick(); localClient.Tick(); renderer->RenderFrame(...); uiManager->Render(appState); }`.
   * Loop must run until the platform reports a quit event (`Alt+F4`/window close/`Esc` from pause menu → confirm exit) — this is what "the app should be built alongside the engine" in practice means: a process that stays alive and simulates.
   * On `HeadlessPlatform` (no SDL2 available), the loop must still run for a bounded number of ticks in automated tests/CI rather than blocking forever — expose a `--max-ticks=<N>` debug flag for this purpose.
2. **World Save Format (`voxels/world/world_serialization.hpp` / `.cpp`):**
   * Extend the existing `Chunk::SerializeRLE`/`DeserializeRLE` (from `06`) to a whole-world save: directory `<saveRoot>/<worldName>/region/<cx>_<cz>.chunk` per loaded/modified chunk, plus `<saveRoot>/<worldName>/world.meta` (seed, `WorldOptions`, play-time, last-saved timestamp).
   * Only persist chunks that have been generated/modified (dirty-flag) to keep save size bounded on constrained hardware.
3. **Player Save Format:**
   * Extend `SaveManager` (`09_app_lifecycle_ui_and_menus`) with a per-player file `<saveRoot>/<worldName>/players/<playerId>.player` storing the plain-data `PlayerState` from `11` (position, rotation, inventory, health) — one file per player supports future multiplayer saves without format changes.
4. **Autosave & Explicit Save Points:**
   * Autosave on a configurable interval (default every 60s of played time) and always on pause-menu "Save & Quit" and on clean shutdown signal.
   * Loading a save must restore world chunks lazily (only what's within render/simulation distance of the restored player position) rather than blocking on the whole world.
5. **Chunk Streaming Around the Player:**
   * Each tick (or on player chunk-boundary crossing), request generation/load for any chunk within `renderDistanceChunks` of the player and unload (after saving if dirty) chunks that fall outside `simulationDistanceChunks + margin`.
6. **Networking Tie-In:**
   * The local server (already started in `main.cpp` per `10`) must be the sole authority that mutates `World`; the local client only submits `C2S_PlayerMove`/`C2S_BlockModify` and applies `S2C_EntityState`/`S2C_BlockUpdate`, even in singleplayer — this guarantees the same code path works identically for a joinable/hosted session with zero special-casing later.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_runtime_integration.cpp`
* **Test Cases:**
  * `WorldSave.RoundTrip`: Generate a small world, modify several blocks, save, reload into a fresh `World` instance, verify identical block data.
  * `PlayerSave.RoundTrip`: Save a `PlayerState` with non-default position/inventory, reload, verify field-for-field equality.
  * `GameLoop.RunsBoundedTicksHeadless`: Launch the app loop with `--max-ticks=50` against `HeadlessPlatform`, verify it exits cleanly with status 0 and produced no unhandled exceptions.
  * `ChunkStreaming.LoadsAroundPlayerAndUnloadsFar`: Move a simulated player across a chunk boundary, verify newly-in-range chunks become loaded and far chunks are unloaded (and saved if dirty).

---

## 👤 Human-in-the-Loop Actions Required
* None required — save files are plain engine-generated data, no external assets needed.

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R RuntimeIntegrationTest`
3. **Manual smoke check (documented for the human operator, since the agent cannot see a window):** run `voxels_app.exe`, confirm the process stays running (does not immediately exit) and closes only on window-close/quit input.
4. **Self-Healing:** If chunk streaming causes flicker/thrash at chunk boundaries in tests, add hysteresis (unload distance > load distance) instead of using a single threshold.
