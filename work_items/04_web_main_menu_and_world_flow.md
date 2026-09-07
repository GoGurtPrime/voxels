## WI-04.01: Migrate the main menu and world-management flow to React

### Goal
Let a player launch, create/select/delete worlds, join a server, observe loading/errors, and change menu settings entirely through transparent web UI while native game services retain authority.

### Scope
- Own: React routes/components/styles/tests for non-gameplay screens, model/action mapping, web accessibility, corresponding ImGui adapter removal, state-machine integration tests, UI assets/fonts.
- Exclude: cinematic background (WI-05), HUD/pause (WI-06), final ImGui removal (WI-08).
- Prerequisites: WI-02 and WI-03.

### Implementation Contract
- Inputs and outputs: routes are MainMenu, WorldSelect, WorldCreation, Loading, JoinGame, JoinLoading, menu Settings, ControlsCard, and Error. React consumes engine models and emits named actions only; it does not generate authoritative seeds, mutate saves, or own settings.
- Runtime integration: state entry selects/publishes route before presentation. Action acknowledgement/rejection republishes model/error. Browser history is disabled; routes follow AppStateMachine only.
- Threading and performance: route/model changes appear next frame after dispatch. Local code-split chunks preload before transition; first menu frame arrives within two seconds on target desktop.
- Platform and dependencies: semantic HTML, visible focus, keyboard activation, labelled fields, reduced-motion query, WCAG 2.1 AA contrast, and locally bundled licensed fonts.

### Acceptance Criteria
- [ ] First desktop main menu is React/Tailwind over transparent background with Play, Join Game, Settings, and Quit.
- [ ] World creation, selection/load, guarded delete, remote join, loading, error acknowledgement, settings, and first-run controls operate through existing native services/controllers.
- [ ] Native validation errors are accessible; malformed/repeated actions cannot delete/create unintended saves or transition twice.
- [ ] Keyboard navigation, dialogs/focus restoration, Escape/back, pointer use, 100/150/200% DPI, and screen-reader names are tested for every route.
- [ ] Remove ImGui only for fully migrated routes; pause/HUD adapter remains until WI-06.
- [ ] Native integration tests assert real save/state effects; browser E2E runs against static bundle with protocol fixtures.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_web_ui_assets voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "AppLifecycle|WebUiMenu|WebUiProtocol"
npm --prefix ui run test
npm --prefix ui run test:e2e
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: React routes, native model/action integration, migrated ImGui paths, tests, assets, and docs.
- Observed: player completes every menu/world flow without player-facing ImGui.
- Results: record E2E/tests, load timing, DPI observations, and desktop result.
- Known gaps: cinematic background is WI-05; pause/HUD is WI-06.

---

## Current Handoff Status (2026-09-06)

### Status
**BLOCKED -- WI-04 is not complete.** The real desktop CEF path does not currently render a usable React menu. Do not claim completion or commit the current state as a completed work item.

### What Was Implemented
- Replaced the prior React runtime-proof surface with routes for MainMenu, WorldSelect, WorldCreation, Loading, Join, Settings, ControlsCard, and Error in `ui/src/main.jsx`.
- Added native action decoding in `WebUIManager` using a CEF V8 binding (`window.voxelsAction`) and `CefProcessMessage` messages, then expanded `PlayerUIActionDispatcher` to use the existing authoritative save, preferences, network, and state-machine services.
- Added a native integration test, `PlayerUI.DispatcherCreatesWorldSaveBeforeLoading`, which asserts that a CreateWorld action produces a real saved world before the state machine enters Loading.
- Added browser-route presentation guards for most migrated ImGui states. `SettingsState::Render()` is now guarded; Pause/HUD intentionally remain native for WI-06.
- Moved renderer frame start to the real app loop so browser-owned states do not leave stale ImGui contents onscreen.
- Changed the CEF texture compositor to retain and composite its most recent complete texture every frame. This fixed the one-frame menu flash and addresses ghost trails caused by uploading only two dirty rectangles.
- Added temporary stderr diagnostics for received web actions and rejected dispatches. These should be replaced with structured logging or removed after the bridge is proven.
- Updated `ARCHITECTURE.md` and `DIAGRAMS.md`, but those documents are ahead of the actual working desktop result and must be corrected or deferred until CEF rendering works.

### Confirmed Runtime Evidence
- The CEF window initially flashed a browser surface and then cleared. Retaining the last compositor texture fixed that symptom.
- With the original external Vite CSS/module files, the desktop app rendered only unstyled white HTML text in the upper-left corner. It also logged repeated CEF `browser_info_manager.cc:858` timeouts for newly created frames. Buttons did not initially work.
- After the V8 process-message bridge was introduced, a desktop smoke run logged `Web UI action received: play` and `Web UI action received: load-world`; the player reached WorldSelect and could load a world. This proves pointer forwarding, V8 binding, CEF process messaging, and native dispatch can work on the current path.
- The same successful routing run still displayed raw JavaScript in the page and unstyled menu text. The loading route displayed its initial 0% model only.
- Loading progress publication was updated so each integrated world chunk republishes the Loading model with a real fraction. This builds but has not been successfully desktop-verified because rendering regressed afterward.
- The latest run displayed only the colored background, with no text or controls. The current `data:`-URL JavaScript bundling experiment therefore does not execute under the embedded CEF runtime.

### Regressions And Failed Approaches
- **CEF `file://` subresources:** Vite's external CSS and module script do not reliably load through the current windowless CEF setup. The entry HTML loads, but dependent resources produce CEF browser-info timeouts and leave static unstyled HTML.
- **Inline script experiment:** Inlining Vite's bundle as element text caused React's internal literal `</script>` test string to terminate the HTML script tag, rendering the remaining JavaScript as visible page text. Escaping attempts did not produce a stable emitted page.
- **Base64 plus `eval`:** Removed raw source, but CEF did not execute the resulting code; the page became only the styled/background surface with no React content.
- **`data:text/javascript;base64,...` script:** The current experiment also produces only a background and no React content. It must not be considered a solution.
- **CEF bootstrap executable:** Staging `bootstrap.exe` and assigning it to `browser_subprocess_path` is invalid for this standalone executable client. CEF's supplied documentation says bootstrap is used with a sandboxed DLL client. That configuration crashed child GPU/network processes with exit code `0x80000003` and exited the app. The build files and `browser_subprocess_path` were subsequently restored to the normal standalone executable configuration.
- **GPU switches:** `disable-gpu` and `disable-gpu-compositing` did not cure the bootstrap configuration crash. They remain in `FileAccessApp::OnBeforeCommandLineProcessing`; evaluate whether they are needed after the resource-loading architecture is corrected.

### Current Dirty State
- No checkpoint commits were made.
- `imgui.ini` is user/runtime modified and must not be reverted as part of this item.
- Generated browser assets were regenerated under `app/assets/ui/browser/`; old hashed files are deleted and new hashed files are untracked. Keep generated output aligned with `ui-manifest.json` using the normal UI build, not manual edits.
- The canonical `build/` directory is currently configured with `VOXELS_ENABLE_WEB_UI=ON`. Before completing the item, restore and verify the default OFF configuration in the same `build/` directory.

### Recommended Recovery Plan
1. **Stop trying to embed or load the Vite bundle through `file://`.** Revert `ui/scripts/build.mjs` to emit normal Vite `index.html`, CSS, and JavaScript references. Remove the `data:` experiment.
2. **Serve UI assets through a registered CEF scheme handler.** Register a dedicated, standard scheme such as `voxels-ui://app/` before `CefInitialize`, then implement `CefSchemeHandlerFactory`/`CefResourceHandler` to serve only verified files below `Paths::AssetsDir() / "ui" / "browser"`. Normalize paths, reject traversal, assign MIME types, and return an error for missing files. Navigate CEF to `voxels-ui://app/index.html` instead of `file:///...`.
3. **Use normal Vite resource links on that scheme.** CSS and module chunks will then load as same-origin web resources, avoiding both CEF's fragile `file://` origin behavior and inline-script parser hazards. Keep `allow-file-access-from-files` only if it remains necessary after this migration; it likely should be removed.
4. **Add CEF loading diagnostics.** Implement `CefLoadHandler::OnLoadError` and `CefDisplayHandler::OnConsoleMessage` on `BrowserClient`, with URL and error-code logging. Do not rely on a blank compositor output to infer JavaScript failures.
5. **Handle model publication readiness.** `Publish()` currently drops a model before `OnAfterCreated`/document readiness. Store the latest `PlayerUIViewModel` in `WebUIManager` or `BrowserClient` and send it from `OnLoadEnd`; this prevents a route model from being lost during initial navigation.
6. **Re-run the real desktop smoke test before visual redesign.** Verify first frame, CSS, JavaScript, click `Play`, route to WorldSelect, create/load/delete a world, loading progress, settings, join failure, and error acknowledgement. Confirm no ImGui panel is visible on browser-owned routes.
7. **Only after desktop rendering is correct, improve layout/DPI and add browser tests.** The current React layout is intentionally incomplete and was reported as cramped. Required missing acceptance coverage includes keyboard/focus/Escape behavior, 100/150/200% DPI, accessibility names, static-bundle protocol fixtures, and `npm --prefix ui run test` / `test:e2e` scripts.
8. **Finish verification and documentation.** Build/tests with Web UI ON, non-headless desktop smoke, then reconfigure the same `build/` directory with Web UI OFF and run the full default build/CTest suite. Correct architecture/diagram claims to match observed behavior. Commit only coherent green checkpoints, per repository rules.

### Last Verification Snapshot
- `cmake --build build --config Debug --target voxels_app`: succeeded after the latest C++ and UI changes.
- Focused `WebUiCompositor.*` tests: passed after retaining the latest frame texture.
- Earlier focused `WebUi|PlayerUI` CTest run: 10/10 passed before the latest rendering experiments; rerun after recovery.
- `npm --prefix ui run build`: succeeds.
- Desktop smoke: **failed**. Latest `data:`-script output renders only a background with no text or controls.
- Full default OFF build and full CTest have not been rerun after the latest edits.