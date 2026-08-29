# Work Item 03: Platform & Windowing Abstraction (SDL2 & Dreamcast)

## 🎯 Objective & Overview
Construct the abstract platform layer (`IPlatform`) wrapping window management, graphics context initialization, display mode enumeration, native OS event pumps, and high-resolution timer access using SDL2 for PC/Mac/Linux and platform stubs for KallistiOS (Sega Dreamcast).

---

## 🔗 Dependencies & Context
* **Prerequisites:** `02_core_engine_foundation`
* **Target Subsystems:** `engine/include/voxels/platform/`, `engine/src/platform/`

---

## 📋 Detailed Task Breakdown
1. **Platform Interface (`voxels/platform/platform.hpp`):**
   * Abstract interface `IPlatform` with methods:
     * `bool initialize(const WindowConfig& config)`
     * `void pollEvents(IEventListener* listener)`
     * `void swapBuffers()`
     * `void setWindowFullscreen(bool fullscreen)`
     * `void setWindowResolution(int width, int height)`
     * `double getHighResTimeSeconds()`
     * `void shutdown()`

2. **SDL2 Platform Implementation (`voxels/platform/sdl_platform.hpp`):**
   * Implements `IPlatform` using SDL2 (`SDL_CreateWindow`, `SDL_PollEvent`, `SDL_GL_SwapWindow` / Vulkan surface creation helper).
   * Maps OS window resize, focus, close, and DPI change events into internal engine event dispatches.

3. **Dreamcast/KallistiOS Platform Stub (`voxels/platform/dreamcast_platform.hpp`):**
   * Conditional compilation stub under `#ifdef VOXELS_PLATFORM_DREAMCAST`.
   * Targets hardware display output (640x480 @ 60Hz VGA/NTSC) without desktop windowing semantics.

4. **Engine Lifecycle Hooks in `voxels/engine.hpp`:**
   * Integrate platform creation into `Engine::initialize()` based on active build target definitions.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_platform.cpp`
* **Test Cases:**
  * `Platform.HeadlessInitialization`: Test platform subsystem initialization in offscreen/headless mode.
  * `Platform.EventPumpDispatch`: Simulate injected SDL window close/resize events and verify callback delivery to `IEventListener`.
  * `Platform.HighResTimer`: Verify timer monotonicity and non-zero deltas.

---

## 👤 Human-in-the-Loop Actions Required
* **Window Icon Asset (Human Step):**
  1. Create a 32x32 RGBA PNG icon file named `window_icon.png`.
  2. Place it in `app/assets/icons/window_icon.png`.
  3. (Procedural Fallback: The code will generate a 32x32 solid colored bitmap in memory if this file is missing).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R PlatformTest`
3. **Self-Healing:** If running on a headless CI/build server without an active display server (e.g. X11/Wayland), ensure `SDL_VIDEODRIVER=dummy` environment variable is set during headless testing.
