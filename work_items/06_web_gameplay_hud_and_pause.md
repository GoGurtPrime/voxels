## WI-06.01: Replace the in-game HUD and pause overlays with web player UI

### Goal
Give the player a transparent React HUD and web-driven pause/settings/controls/notification experience over live gameplay, with correct input ownership and no lost game state.

### Scope
- Own: HUD/pause/settings React routes, HUD view-model publisher, action wiring, native GL HUD migration/removal where superseded, input policy, tests, accessibility/performance updates.
- Exclude: editor UI, player-ImGui retirement enforcement, and unrelated gameplay features.
- Prerequisites: WI-02 through WI-05; gameplay HUD renderer, session, inventory/hotbar, and settings/audio controls.

### Implementation Contract
- Inputs and outputs: publish hotbar slots/selection, held item, crosshair/target state, break progress, health/status when available, prompts, notifications, and F3-independent diagnostics. Actions cover Resume, Settings, Save and Quit/Leave Server, settings, controls card, and errors. Engine remains authoritative for inventory, interactions, persistence, and networking.
- Runtime integration: InGameState publishes HUD at 30 Hz maximum plus event-driven interaction changes. Pause pushes web Overlay, clears gameplay edge actions, and restores Gameplay only on close. Delete screen-space GL HUD primitives only after equivalent web visuals/input tests pass; target outline, particles, and other world-space effects remain native.
- Threading and performance: serialization/update adds no more than 0.25 ms median main-thread cost and no allocations in fixed simulation. A hotbar/progress update does not force a full browser repaint. UI remains within WI-03 budget while moving/interacting.
- Platform and dependencies: responsive safe margins at 16:9, 16:10, 21:9, and 4:3 from 100-200% scaling. Text uses localizable message IDs. Persist crosshair size/contrast/reduced-motion preferences through existing settings.

### Acceptance Criteria
- [ ] Live gameplay HUD correctly shows hotbar, selection, held item, targeting/break progress, crosshair, notifications, and relevant status without hiding the world.
- [ ] Escape opens web pause; gameplay input is suppressed while paused/editing settings and resumes without spurious look, break, place, or hotbar actions.
- [ ] Save/leave, settings, controls, fatal errors, and reconnect paths retain state-machine, persistence, and network behavior through dispatcher actions.
- [ ] World-space outline and break particles remain correct; only superseded screen-space GL HUD code is removed.
- [ ] Integration tests assert gameplay/persistence/network effects, GPU/layout tests assert alpha/z-order/resizes, and browser E2E covers keyboard and pointer flows.
- [ ] A 10-minute gameplay soak records no UI/browser-buffer growth or lost input and stays within update/pass budgets.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Gameplay|WebUiHud|WebUiPause|Persistence|Networking"
npm --prefix ui run test
npm --prefix ui run test:e2e
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: web HUD/overlays, game model/action wiring, superseded native HUD code, input logic, tests, and docs.
- Observed: live world rendering remains beneath interactive transparent HUD and pause UI.
- Results: record soak duration, memory trend, update/pass timings, test/E2E counts, and desktop observation.
- Known gaps: ImGui remains only as opt-in developer diagnostics until WI-08.