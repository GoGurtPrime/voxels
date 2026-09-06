## WI-08.01: Make web UI mandatory for the desktop player and confine ImGui to tools

### Goal
Complete the progressive migration: every player-visible menu, overlay, and HUD uses verified web UI by default and missing runtime/content fails visibly, while editor and opt-in developer diagnostics retain ImGui where useful.

### Scope
- Own: player composition/CMake dependency policy, removal of superseded ImGui player APIs/paths, diagnostics gating, docs/diagrams/packaging, final regression/runtime verification.
- Exclude: removing ImGui from `voxels_editor`, player feature work, and renderer backend implementations.
- Prerequisites: WI-01 through WI-07, with no unresolved high-severity security, accessibility, or performance result.

### Implementation Contract
- Inputs and outputs: desktop `voxels_app` always constructs `WebUIManager`; CEF and verified UI manifest are hard dependencies. Failure shows native SDL-visible error plus log and exits nonzero, never selecting ImGui. Test doubles remain explicit headless fixtures.
- Runtime integration: remove `ImGuiUIManager` from player AppContext, event routing, state rendering, and normal player linkage where unused. Preserve ImGui for editor. Any player diagnostic overlay is explicitly developer-gated, visually separate, and cannot own production input/routes.
- Threading and performance: retain WI-07 budgets and shutdown order: states/session release, browser/compositor GL release, renderer, platform/CEF. No CEF object outlives its required process/context lifetime.
- Platform and dependencies: `VOXELS_ENABLE_WEB_UI=OFF` is only permitted for explicit headless/test targets, never shipping desktop player. CMake states this constraint. Package includes only selected platform runtime/licenses.

### Acceptance Criteria
- [ ] Search/build graph confirms no player-visible state or HUD renders ImGui controls and no default player path constructs ImGuiUIManager.
- [ ] `voxels_editor` still launches with its ImGui workspace; tests verify player/editor UI split does not break linkage.
- [ ] Corrupting/deleting packaged UI manifest or CEF resource makes startup fail loudly with visible error/log, never blank window or ImGui fallback.
- [ ] Offline packaged executable completes launch, menu/world flow, cinematic, join, loading/errors, HUD, pause/settings, save/quit, and clean shutdown solely through web UI.
- [ ] Full build, CTest, frontend tests/E2E, CPack, clean-machine package smoke, and human non-headless desktop smoke are green; architecture supersedes ADR-003/player ImGui references.
- [ ] `ASSET_REQUESTS.md` and package license records have no unowned web runtime/content gaps.

### Verification Commands
```text
cmake --preset windows-debug -DVOXELS_ENABLE_WEB_UI=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
npm --prefix ui run lint
npm --prefix ui run test
npm --prefix ui run test:e2e
cpack -C Debug
build/package-smoke/Debug/voxels_app.exe
build/editor/Debug/voxels_editor.exe
```

### Completion Evidence
- Changed: player dependency/composition, removed ImGui player code, editor/dev boundary, package rules, tests, and authoritative docs.
- Observed: offline packaged player shows only transparent web UI over rendered content; editor retains ImGui.
- Results: report full tests, package hash/path, runtime smoke, startup failure proof, and final metrics.
- Known gaps: none for player UI migration; renderer/editor work stays separately scoped.