## WI-03.01: Integrate CEF off-screen rendering as the transparent player UI pass

### Goal
Run the local web bundle inside CEF and alpha-composite it after the OpenGL world frame while forwarding SDL input through native input policy.

### Scope
- Own: `WebUIManager`, CEF application/client/render/resource handlers, GL UI texture/quad compositor, SDL-to-CEF events, app lifecycle composition, CMake link/runtime rules, focused tests/diagnostics.
- Exclude: production menu/HUD routes, cinematics, CEF accelerated texture sharing, and ImGui retirement.
- Prerequisites: WI-01 and WI-02; staged CEF distribution and UI manifest.

### Implementation Contract
- Inputs and outputs: initialize CEF with `CefExecuteProcess`/`CefInitialize`, normal subprocess handling, external message pump, explicit user-data cache/log paths. Create one transparent windowless browser at drawable pixel dimensions and load only the manifest entry through the local resource handler.
- Runtime integration: pump browser each frame; resize for DPI/drawable changes; submit after state/HUD render; release browser/compositor before GL context/platform teardown. CEF paint callbacks copy dirty BGRA regions into bounded upload buffers. Only render thread performs GL upload/composition.
- Threading and performance: protect latest-frame queue and drop superseded frames. At 1920x1080 retain at most 4 MiB paint data, upload no more than two textures/frame, and measure UI at $\leq 1.5$ ms CPU plus $\leq 0.5$ ms GPU.
- Platform and dependencies: OpenGL first with premultiplied alpha. A renderer-neutral compositor hides CEF from states. Initialization, manifest, browser, or GL failures are visible and logged.

### Acceptance Criteria
- [ ] Normal `voxels_app` starts CEF subprocesses and opens one transparent OSR browser with no web server, internet, popup, or navigation capability.
- [ ] A React diagnostic route preserves alpha/z-order above moving GL content and current gameplay HUD in readback tests.
- [ ] Resize, high DPI, minimize/restore, focus changes, browser crash/reload, and shutdown remain correct or visibly fail without leak/crash.
- [ ] SDL mouse, wheel, key, text, focus, and IME events reach CEF; tests prevent simultaneous gameplay interaction during Overlay/TextEntry.
- [ ] Tests cover dirty-rectangle clipping/coalescing, alpha composition, shutdown ordering, and errors; hidden-window GPU readback asserts blend output.
- [ ] Desktop smoke keeps interactive transparent UI stable through five resize cycles.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "WebUi(Compositor|Input|Lifecycle)|Graphics"
build/tests/Debug/voxels_tests.exe "[web-ui]"
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: CEF lifecycle/browser/resource handlers, manager/compositor, SDL adapter, composition, tests, and docs.
- Observed: local React UI is interactive and transparent over a real OpenGL frame.
- Results: record timings, retained paint bytes, upload count, tests, and smoke observation.
- Known gaps: player routes are ImGui pending WI-04 and WI-06.
