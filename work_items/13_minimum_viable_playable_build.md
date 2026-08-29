# Work Item 13: Minimum Viable Playable Build (End-to-End)

## 🎯 Objective & Overview
This is the capstone integration item that fulfills the original project goal: **a buildable, running game the player can actually play** — main menu → create/pick a world → loading/generation screen → spawn safely on the surface → walk/jump/fall/break/place blocks → pause menu → save & quit — all backed by the local client/server loopback architecture from `10_networking_and_server_client`, using the running game loop from `12_persistence_and_runtime_integration`. Every prior work item produced correct, unit-tested *pieces*; this item's Definition of Done is explicitly about the *whole screen-to-screen experience*, not another isolated subsystem.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `09_app_lifecycle_ui_and_menus`, `10_networking_and_server_client`, `11_gameplay_player_and_physics`, `12_persistence_and_runtime_integration`
* **Target Subsystems:** `app/src/main.cpp`, `engine/include/voxels/app/state_machine.hpp` (extend), `engine/include/voxels/ui/` (wire real screens, not just headless model classes)

---

## 📋 Detailed Task Breakdown
1. **Wire Every App State to Real Behavior (not just a log entry):**
   * `MainMenuState`: renders Play/Settings/Exit; Play transitions to `WorldSelectState` which lists saves via `SaveManager::ListSaves()`.
   * `WorldSelectState` → `WorldCreationState` (new world: name, seed, peaceful/permadeath/always-sunny/visibility/sandbox toggles from `07_world_generation_pipeline`'s `WorldOptions`) or straight to `LoadingScreenState` (existing save).
   * `LoadingScreenState`: actually drives the `IGenerationPhase` queue (`07`) and renders real progress (Shape → Caves → Vegetation → Safe Spawn), not a hard-coded discrete value.
   * `InGameState`: the real game loop from `12` — player movement/physics/block interaction render every frame.
   * `PauseMenuState`: Resume, Settings, Toggle World Public/Private (updates `WorldOptions.visibility` and, when public, exposes the loopback `GameServer` port for LAN join), Save & Quit (invokes `12`'s save path then returns to `MainMenuState`).
2. **Joinable Local Server, Even Solo:** confirm (via integration test, not just manual reasoning) that the same `GameServer` instance created for singleplayer accepts a second `GameClient` connecting over LAN when visibility is toggled public — this is the "hosting its own game server even when running locally" requirement from the project brief. No separate dedicated-server code path should be required for this to work; `--server` (headless) remains available for dedicated hosting.
3. **Per-Platform Input Definitions (stub consoles only):**
   * Concrete input bindings/action maps for: PC keyboard+mouse, PC XInput controller (Windows), Mac keyboard+mouse, Linux keyboard+mouse, generic gamepad (SDL GameController API) for Mac/Linux.
   * Add empty-but-compiling stub headers for future console pads (`engine/include/voxels/input/console/xbox_gdk_input.hpp`, `playstation_input.hpp`, `switch_input.hpp` or similar) guarded by `#ifdef` feature flags that default `OFF`, matching the `VOXELS_ENABLE_STEAM`-style opt-in pattern from `14_steam_sdk_and_platform_integration` — they must compile to nothing today but give a clear extension point later.
4. **Asset Wiring Checklist:** confirm every place that currently uses a procedural fallback (menu background, block textures, UI font, audio tones) has a single documented human drop-in path (see Human-in-the-Loop section) — consolidate the scattered per-work-item asset notes into one canonical list so the human operator has one place to look.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_mvp_playthrough.cpp`
* **Test Cases:**
  * `MVP.FullStateFlow`: Drive the state machine programmatically Boot → MainMenu → WorldCreation → LoadingScreen (assert generation phases complete in order) → InGame → PauseMenu → MainMenu (Save & Quit), asserting each transition's real side effect (save file exists, world chunks generated, player spawned above solid ground) rather than only the transition log.
  * `MVP.SafeSpawnIsPlayable`: After generation, verify the spawned player's AABB does not intersect solid blocks and `onGround` becomes true within a handful of physics ticks.
  * `MVP.LocalServerAcceptsSecondClient`: Start the app's internal loopback `GameServer` as in singleplayer, toggle visibility public, connect a second independent `GameClient` from the test, verify both clients receive consistent `S2C_EntityState`/`S2C_ChunkData`.
  * `MVP.SaveQuitResumeRoundTrip`: Save & Quit from `PauseMenuState`, relaunch the state machine into `WorldSelectState`, load the same save, verify player position/inventory and modified blocks match pre-quit state.

---

## 👤 Human-in-the-Loop Actions Required
* **Consolidated Asset Drop-In List (Human Step):** supply real assets to replace procedural fallbacks whenever convenient (none block automated build/test):
  1. Block/item textures → `app/assets/textures/blocks/<block_name>.png` (16×16 or 32×32 RGBA, consistent size across the set).
  2. UI art → `app/assets/ui/menu_bg.png` (1920×1080), `app/assets/ui/game_logo.png` (512×256 RGBA).
  3. Audio → `app/assets/audio/sfx/*.wav` (44.1kHz 16-bit PCM), `app/assets/audio/music/*.ogg`.
  4. Fonts → `app/assets/ui/fonts/*.ttf`.
* **SDL2 Development Libraries (Human Step, Windows):** the current dev environment has no SDL2 found by CMake, so the app silently falls back to `HeadlessPlatform` (no real window). Install SDL2 development libraries (e.g. via `vcpkg install sdl2` and pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`, or download the SDL2 VC development package) so `SDL2::SDL2` resolves and a real window/render loop is exercised locally, not just in headless CI.

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R MVPPlaythroughTest`
3. **Manual smoke check (documented for the human operator):** launch `voxels_app.exe` with SDL2 available, click through Main Menu → Create World → watch the loading screen → confirm the player spawns standing on solid ground and can move/turn/jump/mine/place blocks with keyboard+mouse, then Pause → Save & Quit → relaunch and resume the same world.
4. **Self-Healing:** If any state transition in `MVP.FullStateFlow` has no observable side effect to assert on, that is a defect in the *previous* work item's integration (not a reason to weaken this test) — go back and wire the missing behavior rather than loosening the assertion.
