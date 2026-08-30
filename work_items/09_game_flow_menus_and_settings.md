# Work Item 09 — Game Flow: Menus, Settings & State Machine

**Phase:** C — The Application Shell · **Prerequisites:** 06, 07, 08

---

## 1. Problem Statement

`MainMenuState`, `WorldSelectState`, `WorldCreationState`, `InGameState`, and `PauseMenuState` are empty classes whose only member returns an enum. The player has no way to start a game, change a setting, pause, or quit. The application is a window that boots straight into nothing.

## 2. Objective

Every state in [DIAGRAMS.md](../DIAGRAMS.md) §3 becomes a real, rendered, interactive screen, and the full flow **Main Menu → World Select → World Creation → Loading → In-Game → Pause → Save & Quit → Main Menu** works end to end with a mouse and keyboard.

## 3. Scope

**In scope:** `engine/app/state_machine` state implementations, `AppContext`, every menu screen, the settings system with live application, the loading screen driving real generation, error state.

**Out of scope:** the on-disk save format itself (10), audio (11), multiplayer join UI (16).

## 4. Implementation Tasks

1. **`AppContext`** — a struct of non-owning references (`IPlatform&`, `IRenderer&`, `IUIManager&`, `IAudioDevice&`, `InputManager&`, `AssetManager&`, `JobSystem&`, `Preferences&`, `SaveManager&`, plus a `RequestTransition(AppStateId)` callback). Passed to every state's constructor. States never reach for globals.
2. **`MainMenuState`:** title/logo, buttons **Play**, **Settings**, **Quit**, build version string in the corner, and a slowly rotating rendered world or gradient behind the menu so it isn't a flat void. Keyboard and gamepad navigable, not mouse-only.
3. **`WorldSelectState`:** lists saves from `SaveManager::ListSaves()` with name, seed, last-played timestamp, size on disk, and game mode. Actions: **Play**, **Delete** (typed-confirmation dialog), **New World**, **Back**. Empty-state message when there are no saves.
4. **`WorldCreationState`:** world name (validated: non-empty, filesystem-safe, not a duplicate), seed field (blank → random; text seeds hashed deterministically to `u64`), and `WorldOptions` toggles — game mode (Survival/Creative), peaceful, permadeath, always-day, visibility (private/public), render distance override. **Create** → `LoadingScreenState`.
5. **`LoadingScreenState`:** actually drives the generation pipeline on job workers and shows **real** progress — current phase name (Shape → Caves → Ore → Vegetation → Lighting → Spawn), chunks completed / total for the spawn neighbourhood, and a progress bar. Transitions to `InGameState` only once the spawn area is resident and the spawn point is verified. Generation failure → `ErrorScreen`.
6. **`SettingsState`** (reachable from both main menu and pause, returning to whichever opened it), with tabs:
   * **Video:** resolution, window mode (windowed/borderless/fullscreen), vsync, FOV, render distance, simulation distance, anti-aliasing, texture filtering, brightness/gamma.
   * **Audio:** master, music, SFX, ambience volumes with immediate audible feedback (from work item 11).
   * **Controls:** mouse sensitivity, invert Y, full **rebindable key map** with conflict detection and a "press a key" capture row, plus gamepad bindings.
   * **Gameplay:** view bob, particles, autosave interval, HUD scale.
   * **Apply / Revert / Reset to Defaults.** Changes apply **live** where possible (FOV, sensitivity, render distance, volumes) and persist to `settings.json` in the user data directory on apply.
7. **`PauseMenuState`:** rendered over a dimmed, frozen game scene. **Resume**, **Settings**, **Toggle World Visibility (Private/Public)** — updating `WorldOptions` and, when public, showing the LAN address/port — **Save & Quit to Menu**, **Save & Exit to Desktop**. `Escape` toggles pause; simulation is halted while paused (and the local server is paused in singleplayer).
8. **`ErrorScreen`:** displays a human-readable failure (world load failed, generation failed, disk full) with the log file path and an acknowledge button returning to the main menu. Never a silent crash.
9. **State machine upgrades:** state stack support for overlay states (Settings over Pause), guaranteed `OnExit` on shutdown so quitting mid-game autosaves, and a transition request queue so a state can safely request a transition from inside `Update`.
10. **First-run experience:** with no saves present, the main menu's Play button leads directly to World Creation with sensible defaults, so a brand-new player reaches gameplay in two clicks.

## 5. Acceptance Criteria

* Launching the game shows a **real main menu** — this is the single most visible fix in the project.
* A new player can go from double-clicking the executable to walking around a generated world using only the mouse, in under five interactions.
* The loading screen shows generation phases advancing, not a fake animation.
* Changing FOV, sensitivity, render distance, or volume in Settings takes effect immediately and survives a restart.
* Rebinding a key changes the actual in-game control; conflicts are flagged.
* `Escape` pauses; Resume returns with the cursor re-captured and the camera unmoved.
* Save & Quit returns to the main menu with the world listed and reloadable.
* Every navigation path has a working Back; no dead ends.

## 6. Automated Tests

`tests/test_game_flow.cpp` — drive the state machine headlessly with a `NullUIManager` and scripted intents:
* `Flow.MainMenuToInGameCreatesWorldAndSpawnsPlayer` — asserts a save directory exists, chunks are generated, and the player AABB is clear above solid ground.
* `Flow.LoadingScreenReportsMonotonicProgressThroughAllPhases`.
* `Flow.PauseHaltsSimulationAndResumeRestoresIt` — player position is unchanged across a paused interval.
* `Flow.SaveAndQuitPersistsThenWorldAppearsInWorldSelect`.
* `Flow.SettingsChangeAppliesLiveAndPersistsAcrossRestart`.
* `Flow.KeyRebindChangesResultingPlayerIntent`.
* `Flow.DeleteSaveRemovesDirectoryOnlyAfterConfirmation`.
* `Flow.DuplicateOrInvalidWorldNameIsRejectedWithMessage`.
* `Flow.GenerationFailureRoutesToErrorScreen`.
* `Flow.SettingsOverlayReturnsToOriginatingState` — main menu vs pause.
* `Flow.ShutdownMidGameTriggersAutosave`.
* `Flow.SeedTextHashesDeterministically` — the same seed string always yields the same world.

## 7. Anti-Shell Checks

- [ ] No `IAppState` subclass has an empty `Update` or `Render`.
- [ ] Every menu button performs a real action; no non-functional placeholders shipped.
- [ ] Settings are read from and written to the user data directory, not a hard-coded default.

## 8. Assets & Human Actions

* `app/assets/ui/logo.png` (512×256 RGBA8) and `app/assets/ui/menu_background.png` (1920×1080) — request from the operator, ship generated placeholders, register in [ASSET_REQUESTS.md](../ASSET_REQUESTS.md).

## 9. Verification

Build, test, launch, and **walk the entire flow by hand**: menu → create world → loading → play → pause → settings → resume → save & quit → reload the same world. Report what you saw at every step, including anything that felt wrong.
