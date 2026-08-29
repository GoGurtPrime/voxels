# Work Item 09: App Lifecycle, UI System & Menus

## 🎯 Objective & Overview
Implement the complete application lifecycle, state machine, Dear ImGui UI integration, main menu, pause menu, settings dialogs, world creation/save manager interface, and command-line argument parser (e.g. `--fullscreen=<true|false>`, `--resolution=1920x1080`, `--server`).

---

## 🔗 Dependencies & Context
* **Prerequisites:** `02_core_engine_foundation`, `03_platform_windowing_sdl2`, `04_input_and_multiplayer_abstraction`, `05_graphics_renderer_abstraction`, `07_world_generation_pipeline`
* **Target Subsystems:** `app/src/`, `engine/include/voxels/app/`

---

## 📋 Detailed Task Breakdown
1. **Command Line Argument Parser (`voxels/app/cli_parser.hpp`):**
   * Parses command-line flags on application launch:
     * `--fullscreen=<true|false>` (Overrides config file)
     * `--resolution=<WIDTHxHEIGHT>`
     * `--server` (Boots in headless server mode)
     * `--world=<WORLD_NAME>`
     * `--seed=<SEED>`
     * `--render-distance=<CHUNKS>`

2. **Application State Machine (`voxels/app/state_machine.hpp`):**
   * States: `BootState`, `MainMenuState`, `WorldSelectState`, `WorldCreationState`, `LoadingScreenState`, `InGameState`, `PauseMenuState`.
   * Clean transition hooks (`onEnter()`, `onExit()`, `update(dt)`, `render()`).

3. **UI Engine & Dear ImGui Integration (`voxels/ui/ui_manager.hpp`):**
   * Dear ImGui setup wired to `IPlatform` events and `IRenderer` pipeline.
   * Responsive layout scaling for high-DPI displays and fixed 640x480 Dreamcast output.

4. **Game Menus Implementation:**
   * **Main Menu:**
     * Play (Pick existing save file / Create new world / Delete save).
     * Settings (Video, Audio, Input keybindings, Gameplay).
     * Exit.
   * **World Creation Screen:**
     * Inputs: World Name, Seed, Peaceful toggle, Permadeath toggle, Always Sunny toggle, Visibility (Public/Private/Local-only), Sandbox Mode toggle.
   * **Pause Menu:**
     * Resume, Settings, Toggle World Public/Private, Save & Quit to Main Menu.
   * **Loading Screen:**
     * Progress bar displaying generation phases (Shape $\rightarrow$ Caves $\rightarrow$ Vegetation $\rightarrow$ Spawn Placement).

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_app_lifecycle.cpp`
* **Test Cases:**
  * `CLIParser.ParseArguments`: Pass `{"--fullscreen=false", "--resolution=1280x720", "--server"}`, verify parsing into struct fields.
  * `StateMachine.TransitionOrder`: Push `MainMenuState`, transition to `LoadingScreenState`, verify `onExit` and `onEnter` lifecycle call order.
  * `SaveManager.CreateAndListSaves`: Create world save "TestWorld", verify file directory structure created in user data path and returned in `listSaves()`.

---

## 👤 Human-in-the-Loop Actions Required
* **Menu Background Art & Logo Assets (Human Step):**
  1. Create main menu background image:
     * `app/assets/ui/menu_bg.png` ($1920 \times 1080$ PNG)
  2. Create game title logo image:
     * `app/assets/ui/game_logo.png` ($512 \times 256$ RGBA PNG with alpha channel)
  3. (Procedural Fallback: The UI layer will render a procedural gradient background and a stylized vector text title if PNG logo/background assets are omitted).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R AppLifecycleTest`
3. **Self-Healing:** Verify ImGui context creation works headlessly without throwing null context assertion errors during unit testing.
