## WI-06.01: Replace the in-game HUD and pause overlays with web player UI

### Goal
Give the player a transparent React HUD and web-driven crafting menu/item hot bar and health/notification experience over live gameplay, with correct input ownership and no lost game state.

### Scope
- Own: In-Game HUD (item bar, hearts, crafting menu), React routes, HUD view-model publisher, action wiring, native GL HUD migration/removal where superseded, input policy, tests, accessibility/performance updates.
- Exclude: editor UI, player-ImGui retirement enforcement, and unrelated gameplay features.

### Implementation Contract
- Inputs and outputs: publish hotbar slots/selection, held item, crosshair/target state, health/status when available, prompts, notifications, and F3-independent diagnostics. Engine remains authoritative for inventory, interactions, persistence, and networking.
- Runtime integration: InGameState publishes HUD at 30 Hz maximum plus event-driven interaction changes. Pause pushes web Overlay, clears gameplay edge actions, and restores Gameplay only on close. Delete screen-space GL HUD primitives only after equivalent web visuals/input tests pass; target outline, particles, and other world-space effects remain native.
- Threading and performance: serialization/update adds no more than 0.25 ms median main-thread cost and no allocations in fixed simulation. A hotbar/progress update does not force a full browser repaint. UI remains within WI-03 budget while moving/interacting.
- Platform and dependencies: responsive safe margins at 16:9, 16:10, 21:9, and 4:3 from 100-200% scaling. Text uses localizable message IDs. Persist crosshair size/contrast/reduced-motion preferences through existing settings.

### Acceptance Criteria
- [ ] Fully replace the current in game HUD (not the pause menu, that has already been worked on to be react)
- [ ] Live gameplay HUD correctly shows hotbar, selection, held item, crosshair, text notifications (bottom left), and relevant status without hiding the world.
- [ ] Escape opens web pause; gameplay input is suppressed while paused/editing settings and resumes without spurious look, break, place, or hotbar actions.
- [ ] Save/leave, settings, controls, fatal errors, and reconnect paths retain state-machine, persistence, and network behavior through dispatcher actions.
- [ ] World-space outline and break particles remain correct; only superseded screen-space GL HUD code is removed.
- [ ] Integration tests assert gameplay/persistence/network effects, GPU/layout tests assert alpha/z-order/resizes, and browser E2E covers keyboard and pointer flows.
- [ ] A 10-minute gameplay soak records no UI/browser-buffer growth or lost input and stays within update/pass budgets.

### Exceptions

You may retain imgui for the F3 diagnostics view but only if you agree that the graphic it shows for frame pacing or frame time wouldn't be performant in the react ui.

### Notes from the human operator

Implement a control binding to show a message window which also hosts the notifications such as player joined, player left, player died, those will show up in the same area as the chat stream in the bottom left, but we don't want to show the entire chat interface when the player is just playing, only when they for instance press the 'T' key would the frame and input to chat show up around the area containing player messages and server messages. When you start the chat window, you can't move anymore and the mouse frees, and you can close the mini chat window with your mouse to end or press escape to close it without a mouse click.

The crafting menu can be basic to start. We need to have a menu where you can see and organize all the items you have in your inventory slots, craft, but it isn't like minecraft crafting with clicking and dragging, it's menu and recipe based. You can discover new recipes that will fit in to categories. I want to see an alpha numerically sorted kitchen sink tab of recipes, then more specific tabs with recipes for construction, food, tools, furniture, stuff like smelters. The UI should be controller friendly first, that is our design philosophy. If you want some recipes, you could make a few really simple ones: log -> planks, dirt + stone -> garden mix, plank + 3 stone -> axe  

Recipes should have a small image of the item it will make in the list item with the name of the item, you can use placeholders or get creative if you want, but the image should be ready to add to the assets folder for each craftable item/recipe.

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