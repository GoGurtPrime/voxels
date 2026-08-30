# Work Item 02 — SDL2 Platform & Window Runtime

**Phase:** A — Foundation Repair · **Prerequisites:** 01

---

## 1. Problem Statement

`sdl_platform.cpp` exists but has never been compiled into a default build, and `HeadlessPlatform` is what the game actually runs on. There is no GL context creation, no relative mouse mode, no fullscreen/vsync handling, no resize plumbing, and no controlled shutdown. The frame loop in `main.cpp` is a bounded 200-tick counter with a `sleep`-based frame limiter.

## 2. Objective

A real, resizable, correctly-behaving desktop window with an OpenGL 3.3 Core context, a complete event pipeline, and a proper fixed-timestep frame loop — the substrate everything visual depends on.

## 3. Scope

**In scope:** `engine/platform/` (SDL2 implementation), platform event model, window/display/vsync/fullscreen management, `core/job_system`, the rewritten frame loop in `app/src/main.cpp`.

**Out of scope:** drawing anything (work item 03), UI (08), gameplay (06).

## 4. Implementation Tasks

1. **`SDL2Platform` implementing `IPlatform`:**
   * `Initialize(const WindowDesc&)`: `SDL_Init(VIDEO|AUDIO|GAMECONTROLLER|EVENTS)`, GL attributes (core profile, major 3 / minor 3, 24-bit depth, 8-bit stencil, double buffer, sRGB-capable), window creation with `SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI`, `SDL_GL_CreateContext`, GL loader init, `SDL_GL_SetSwapInterval` from preferences.
   * On macOS request 4.1 Core (the GL 3.3 feature subset per ADR-001/§10).
   * **Fail loudly:** any failure logs the SDL error, shows `SDL_ShowSimpleMessageBox`, and returns false so `main` exits non-zero.
   * `Shutdown()` releases context, window, and SDL subsystems in reverse order.
2. **Window & display services:** `SetFullscreen(mode)` (windowed / borderless / exclusive), `SetResolution`, `EnumerateDisplayModes`, `SetVSync`, `GetDrawableSize` (distinct from logical size for high-DPI), `SetWindowTitle`, `SetRelativeMouseMode`, `SetCursorVisible`, `GetHighResolutionTime`.
3. **Event pipeline:** expand `PlatformEvent` to cover `WindowClosed`, `WindowResized`, `WindowFocusGained/Lost`, `WindowMinimized/Restored`, `KeyDown/KeyUp` (scancode + keycode + repeat + modifiers), `TextInput`, `MouseMotion` (absolute + relative delta), `MouseButtonDown/Up`, `MouseWheel`, `ControllerAdded/Removed`, `ControllerButton`, `ControllerAxis`, `QuitRequested`. `PollEvents` dispatches to an ordered listener list so the UI layer can consume an event before the game layer sees it.
4. **Focus behavior:** losing window focus releases relative mouse mode and, when in gameplay, raises a pause request. Regaining focus must not teleport the camera — discard the first mouse delta after recapture.
5. **`voxels::JobSystem`** (`core/job_system.hpp`): fixed worker pool (`max(1, hardware_concurrency - 1)`), `Enqueue(std::function<void()>)`, `EnqueueWithResult`, cooperative shutdown via `std::jthread` + `stop_token`, main-thread `DrainCompleted()`. This is the pool ADR-008 requires.
6. **Rewrite the frame loop** in `app/src/main.cpp` per [DIAGRAMS.md](../DIAGRAMS.md) §4: delta measurement with a 0.25 s clamp, event poll, 60 Hz fixed-step accumulator, render interpolation alpha, present. Frame pacing comes from vsync/swap interval, not `sleep`. Remove the `maxTicks` production path; keep a bounded loop only under `--headless`.
7. **CLI flags:** `--headless`, `--fullscreen=<true|false>`, `--resolution=<WxH>`, `--vsync=<true|false>`, `--max-frames=<n>` (headless/CI only), `--server`, `--world=<name>`. CLI overrides `settings.json`, which overrides defaults.
8. **Retire the silent fallback:** the platform factory returns `SDL2Platform` unless `--headless` was passed. `HeadlessPlatform` remains for tests.

## 5. Acceptance Criteria

* Launching `voxels_app` opens a titled, resizable window that stays open until the player closes it, and closes cleanly on the window X, `Alt+F4`, and `SDL_QUIT`.
* Resizing the window updates the reported drawable size; the app does not crash when minimized.
* `--fullscreen=true` and `--resolution=1920x1080` visibly take effect.
* The process exits with code 0 and no leaked SDL/GL handles.
* Frame timing is stable (measured frame delta ≈ display refresh with vsync on).

## 6. Automated Tests

`tests/test_platform_runtime.cpp`:
* `FrameLoop.FixedStepAccumulatorRunsExpectedTicks` — feeding a synthetic 1.0 s of delta produces exactly 60 simulation steps and a bounded leftover accumulator.
* `FrameLoop.LargeDeltaIsClamped` — a 5 s stall produces at most the clamp's worth of steps (no spiral of death).
* `Platform.EventDispatchOrderRespectsListenerPriority` — a consuming UI listener prevents the gameplay listener from seeing the event.
* `Platform.FocusLossReleasesMouseCaptureAndRequestsPause`.
* `JobSystem.ExecutesEnqueuedWorkAndShutsDownCleanly` — N jobs complete, results drain on the main thread, destructor joins.
* `Cli.OverridesPreferences` — CLI resolution/fullscreen beat the settings file.

Window-creating tests stay out of CI; they run behind a `[.window]` Catch2 tag.

## 7. Anti-Shell Checks

- [ ] `SDL2Platform` is the default; `HeadlessPlatform` requires `--headless`.
- [ ] No `PlatformEvent` type is declared but never emitted.
- [ ] The frame loop has no fixed tick ceiling on the shipping path.

## 8. Assets & Human Actions

* Report GPU/driver requirements if context creation fails on the operator's machine (see `TOOL-001` in [ASSET_REQUESTS.md](../ASSET_REQUESTS.md)).
* Optional: a window icon at `app/assets/ui/icon.png` (256×256 RGBA8). Generate a placeholder if absent.

## 9. Verification

Build, run tests, then **launch the app and confirm with your own observation**: window appears, resizes, responds to close, and the process exits 0. Report the observed window size and frame rate.
