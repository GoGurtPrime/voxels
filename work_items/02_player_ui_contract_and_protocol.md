## WI-02.01: Define backend-neutral player UI state, actions, and input ownership

### Goal
Make menus and HUD publish validated state and consume validated commands without depending on ImGui or CEF, preventing duplicated game rules in the web layer.

### Scope
- Own: `engine/ui`, UI-facing app state/controller seams, `AppContext`, platform-event routing abstraction, protocol schemas/codecs, focused tests, architecture/diagram updates.
- Exclude: CEF lifecycle/painting, React route implementation, cinematics, and retirement of ImGui player screens.
- Prerequisites: WI-01; current menu controllers, save manager, preferences, input manager, and app-state transitions.

### Implementation Contract
- Inputs and outputs: define `IPlayerUI`, typed route enum, immutable per-route view models, model revisions, and an action variant. Serialize only `{version, kind, requestId, payload}`. Payload limit is 64 KiB; text limits retain existing save/seed bounds. Use one schema/parser, never per-screen JSON parsing.
- Runtime integration: replace `AppContext::ui` concrete ImGui type with `IPlayerUI*`. `PlayerUIActionDispatcher` maps action plus active route/state to existing controllers and deferred state callbacks. An ImGui adapter submits/consumes identical models/actions until WI-08.
- Threading and performance: publication and dispatch occur on main thread; models are value snapshots; backend retains no controller/state references. Publish changes event-driven or cap changing HUD metrics at 30 Hz.
- Platform and dependencies: CEF headers stay within web implementation. Interfaces/test doubles compile headlessly. Define native input policies `Gameplay`, `Overlay`, and `TextEntry`, including SDL forwarding/capture behavior.

### Acceptance Criteria
- [ ] No app state, input listener, or AppContext requires concrete `ImGuiUIManager` to compile.
- [ ] Tests prove accepted actions produce existing controller/state effects; invalid route/version/request ID/payload/save name/seed/preference range changes nothing.
- [ ] Tests prove Overlay/TextEntry blocks gameplay input and return to Gameplay restores mouse mode while discarding exactly one transition delta.
- [ ] Models cover main menu, save selection/delete, creation, loading, join/error, pause, settings, controls card, HUD, toasts, and fatal error.
- [ ] Existing ImGui flow remains unchanged through the adapter; headless tests use a non-rendering double.
- [ ] Protocol, ownership, rate limits, and input policy are documented in architecture/frame-event diagrams.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "AppLifecycle|UiFramework|PlayerUiProtocol|PlayerUiInput"
build/tests/Debug/voxels_tests.exe "[player-ui]"
```

### Completion Evidence
- Changed: UI API, protocol codec/schema, dispatcher, adapter/test double, event routing, tests, and docs.
- Observed: current ImGui menu and gameplay input behavior remains usable through the new seam.
- Results: record protocol test count and desktop menu/input smoke result.
- Known gaps: CEF runtime backend is WI-03.