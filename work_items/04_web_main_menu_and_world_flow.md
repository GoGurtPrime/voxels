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