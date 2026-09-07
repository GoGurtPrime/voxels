## WI-03.02: Wire the CEF web UI into the running app and prove it on screen

### Goal
Turn the already-building CEF integration into a **running, human-observable** transparent web UI pass: `voxels_app` (built with the web UI enabled) boots CEF, composites a React diagnostic route over the live OpenGL frame, ships its CEF runtime beside the executable, and shuts down cleanly — with the default (web UI OFF) build and every existing menu/HUD unchanged. When this item is done, an operator can launch the built exe and *see* the transparent React overlay; there is nothing left to defer.

### Current State (delivered by WI-03.01 build integration — do not redo)
- `-DVOXELS_ENABLE_WEB_UI=ON` configures, and the **whole solution builds** (wrapper, engine incl. `engine/src/ui/web_ui_manager.cpp`, app, editor, tests); `ctest` is 185/185 green. Default build stays OFF/green.
- CEF is integrated via CEF's official CMake in `cmake/cef/CMakeLists.txt` (curated sources, `USE_SANDBOX=OFF`, `CEF_RUNTIME_LIBRARY_FLAG=/MD`). It exposes the `voxels_cef` interface target which defines `VOXELS_HAS_CEF=1` and is linked into `voxels_engine` when the option is ON.
- `WebUIManager` (`engine/include/voxels/ui/web_ui_manager.hpp`, `.cpp`) exists and implements `IPlayerUI` with `ExecuteSubprocess`, CEF init, an OSR `BrowserClient`, and a `WebUiOpenGLCompositor`. It is **compiled but not yet constructed by the app** — `app/src/main.cpp` still uses `ImGuiUIManager` unconditionally.
- The extracted runtime lives under `build/third_party/cef/cef_binary_*/{Release,Resources}`; **nothing is copied next to `voxels_app.exe` yet**.

### Scope
- Own: `app/src/main.cpp` app composition (subprocess bootstrap + UI-manager selection behind `VOXELS_HAS_CEF`), CEF runtime-file staging in CMake, any `IPlayerUI` interface reconciliation needed to swap implementations, lifecycle/shutdown ordering, a wiring/lifecycle test, and the `ASSET_REQUESTS.md`/`ARCHITECTURE.md` updates this touches.
- Exclude: migrating menus/HUD to React (WI-04/WI-06), the cinematic background (WI-05), ImGui retirement (WI-08), and re-enabling the CEF sandbox (tracked below as an explicit follow-up).
- Prerequisites: WI-03.01 (done). Operator-supplied CEF archive is already staged and cached in `build/`.

### Implementation Contract
- **Subprocess bootstrap.** At the very top of `main()`, before any engine/SDL/window creation, call `voxels::WebUIManager::ExecuteSubprocess(argc, argv)` and return its value when `>= 0` (CEF child process). Guard the call and the `#include "voxels/ui/web_ui_manager.hpp"` with `#ifdef VOXELS_HAS_CEF`. In the non-CEF build this code does not exist and behavior is unchanged.
- **UI-manager selection.** Construct the player UI polymorphically through `IPlayerUI`: when `VOXELS_HAS_CEF` is defined, construct `WebUIManager`; otherwise `ImGuiUIManager`. Everything downstream must talk to the interface. Resolve the `ImGuiUIManager`-only calls in `main.cpp` (at minimum `SetDebugMetrics(...)`) rather than deleting the behavior: either promote the needed method onto `IPlayerUI` with a default no-op and keep the ImGui override, or call it only when the concrete type is ImGui. Do not silently drop the debug-metrics feature from the ImGui path.
- **No menu/HUD regression.** ImGui still owns all player routes this milestone (`WebUIManager::UsesNativeRoutePresentation()` returns `false`, which makes `BeginMenuFrame` in `engine/src/app/state_machine_screens.cpp` skip ImGui menu drawing). A naïve swap therefore **blanks every menu**. The running app must remain fully usable: main menu, world select/create, settings, pause, loading, and error screens all render and are interactive exactly as before, while CEF composites a **diagnostic** React route (not the menus) as a transparent pass above the world, HUD, and dev diagnostics. Choose the composition path that satisfies this (e.g. CEF diagnostic overlay alongside the existing ImGui presenter); the observable contract is "menus still work AND a transparent React overlay is visible", not "ImGui disappeared".
- **Frame lifecycle.** Pump the browser once per frame, resize it on DPI/drawable changes, submit the composite after state/HUD render and before present, and release the browser/compositor **before** the GL context and platform are torn down (follow the existing shutdown-ordering rule in `main.cpp`; releasing CEF after `renderer->Shutdown()` will hang or crash on exit).
- **Runtime staging (CMake).** Add a post-build step (only when the option is ON) that copies the CEF runtime beside `$<TARGET_FILE:voxels_app>` so a clean launch with no developer paths works: from the distribution `Release/` copy `libcef.dll`, `chrome_elf.dll`, `v8_context_snapshot.bin`, `libEGL.dll`, `libGLESv2.dll`, `vk_swiftshader.dll`, `vk_swiftshader_icd.json`, `vulkan-1.dll`, `d3dcompiler_47.dll`, `dxcompiler.dll`, `dxil.dll` (i.e. every `Release/` entry except `*.lib` and the `bootstrap*.exe` sandbox loaders); and copy the entire `Resources/` tree (`icudtl.dat`, `resources.pak`, `chrome_100_percent.pak`, `chrome_200_percent.pak`, and the full `locales/` folder). Also set explicit CEF `resources_dir_path`/`locales_dir_path`/user-data/cache/log paths in `WebUIManager::Initialize` so retail layout is deterministic. The install/package rules (WI-07/CPack) are out of scope here, but the per-build copy is not.
- **Failure visibility.** CEF init, browser creation, manifest, or GL-composite failures are logged and cause a visible, non-zero-exit failure — never a silent fallback to a blank window.

### Acceptance Criteria
- [ ] With `-DVOXELS_ENABLE_WEB_UI=ON`, a clean `voxels_app.exe` (run from its own output dir, no dev env) starts CEF subprocesses and shows a transparent React diagnostic route composited over the moving OpenGL world with correct premultiplied-alpha and z-order (above world, HUD, and dev diagnostics). No local web server, internet, popup, or navigation is possible.
- [ ] Every pre-existing ImGui player screen (main menu, world select/create, guarded delete, join, loading, settings, controls card, pause, error) still renders and is fully interactive — verified by navigating them in the running app.
- [ ] Resize, high-DPI (100/150/200%), minimize/restore, and focus changes keep the overlay correct; five resize cycles remain stable with no leak or crash. Shutdown releases CEF before GL/platform teardown and the process exits 0.
- [ ] The default build (`cmake -S . -B build` with the web UI OFF) is unchanged: `ImGuiUIManager` path only, no CEF symbols, build + `ctest` green. `SetDebugMetrics`/debug overlay still work on the ImGui path.
- [ ] A native test asserts the app composition selects `WebUIManager` when CEF is compiled in and `ImGuiUIManager` otherwise, and that shutdown order releases the browser before the renderer. Existing `WebUiCompositor`/`WebUiRuntimeProof`/`WebUiManifest` tests stay green.
- [ ] CEF runtime files are present beside `voxels_app.exe` after a build (assert the key files exist).

### Verification Commands
```text
cmake -S . -B build -DVOXELS_BUILD_TESTS=ON -DVOXELS_ENABLE_WEB_UI=ON -DVOXELS_WEB_UI_CEF_ARCHIVE=<archive> -DVOXELS_WEB_UI_CEF_SHA256=<sha>
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "WebUi|AppLifecycle"
build/app/Debug/voxels_app.exe            # operator smoke run; operator closes the window
# Regression: default build stays green
cmake -S . -B build -DVOXELS_BUILD_TESTS=ON -DVOXELS_ENABLE_WEB_UI=OFF && cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure
```

### Completion Rules (this item must finish, not defer)
- CEF is available; there is **no external blocker**. Do not report BLOCKED and do not park in-scope work under "Known Gaps" (AGENT_RULES §7). The only permitted follow-ups are the explicitly excluded items below.
- Reproduce the AGENT_RULES §4.1 Definition-of-Done checklist with evidence, and report status **COMPLETE** with the commits pushed for each green checkpoint (§4.0, §8). Use the single canonical `build/` directory (§2.5) — never a per-item build tree.
- Work in green, committable slices in this order: (1) `main.cpp` bootstrap + interface reconciliation behind the flag, still ImGui-active, green; (2) runtime staging + `WebUIManager` construction + compositor pass, green; (3) resize/shutdown hardening + wiring test; (4) smoke run + docs.

### Explicitly Out of Scope (legitimate follow-ups)
- **CEF sandbox.** It is disabled (`USE_SANDBOX=OFF`) because `cef_sandbox.lib` is a static `/MT`-only library that cannot link into the engine's `/MD` runtime. Re-enabling it requires an app-wide static-runtime decision (and `bootstrap.exe` loader) and belongs in a dedicated hardening item, not here. Note the current no-sandbox state in the completion report and confirm `ARCHITECTURE.md` ADR-015 / the roadmap's CEF sandbox statement reflect it.
- Migrating real menus/HUD to React (WI-04/WI-06); install/package staging (WI-07); ImGui player retirement (WI-08).

### Completion Evidence
- Changed: `app/src/main.cpp`, CMake runtime staging, any `IPlayerUI` reconciliation, `WebUIManager` path/setting init, a wiring/lifecycle test, and docs.
- Observed: the operator launched the built exe and saw the transparent React overlay over the live world while menus remained usable; five resize cycles stable; clean exit.
- Results: record CEF versions, staged file list, timings/retained paint bytes if measured, test counts, both ON and OFF build/test results, and the smoke observation.
- Known gaps: only the out-of-scope items above, each with its owning work item.
