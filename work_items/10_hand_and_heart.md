# v0.11 - Hand & Heart

This phase turns existing health and crafting placeholders into extensible survival systems. Recipe
extraction precedes tool content; authoritative damage precedes animal combat and pet safety.

## WI-10.01: Ship authoritative health, damage, death, and five-heart HUD

### Goal
Give the player five hearts with half-heart precision, real damage/death consequences, and a
configurable heart color visible in normal gameplay.

### Scope
- Own: health units, damage events/sources, invulnerability window, fall/drowning damage, death/respawn, save/network state, React hearts, settings, tests.
- Exclude: hunger, armor, potions, animal attacks, and health-restoring food content.
- Prerequisites: WI-09.06 dry spawn and WI-09.07 water medium state.

### Implementation Contract
- Inputs and outputs: represent maximum health as ten half-heart units; all mutations pass through a server-owned damage/heal API with source and instigator. Clamp values and replicate events plus authoritative health.
- Runtime integration: fall impact and depleted underwater oxygen apply damage; zero health emits a death notification, drops or preserves inventory according to one documented rule, and respawns at a verified world spawn.
- Threading and performance: fixed simulation thread; constant-time updates; no browser work inside simulation. HUD publishes on health changes.
- Platform and dependencies: store heart color as validated RGB/HSV preference and preserve contrast/shape at common color-vision settings.

### Acceptance Criteria
- [ ] HUD shows five chunky hearts and every health value from 0-10 half-heart units without fractional ambiguity.
- [ ] Fall and drowning tests assert source, invulnerability, death, respawn, and exact persisted/replicated health.
- [ ] Heart color changes live, persists, and retains a non-color empty/damaged distinction.
- [ ] Death appears in the existing event/chat feed and never leaves input or camera in a broken state.
- [ ] Build, full CTest, frontend tests/E2E, docs, and non-headless damage/death smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Health|Damage|Death|Persistence|Networking|WebUiHud"
npm --prefix ui run lint
npm --prefix ui run test
npm --prefix ui run test:e2e
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: health/damage authority, hazards, respawn, HUD/settings, schemas, tests, and bridge docs.
- Observed: record heart states, color change, damage feedback, death, and respawn.
- Results: record native/frontend tests and desktop observations.
- Known gaps: animal attacks consume the damage API in WI-13.04; food healing arrives with crop content.

## WI-10.02: Replace the hard-coded recipe table with validated content data

### Goal
Let developers add or balance recipes without recompiling C++ and give players a complete,
controller-operable recipe browser.

### Scope
- Own: recipe JSON schema/catalogue, stable IDs/categories/discovery flags, asset resolution, startup validation, crafting service extraction, authoring documentation, UI focus/action parity, tests.
- Exclude: tool effects, cooking stations, timed crafting, and final production icon art.
- Prerequisites: current inventory/action bridge and completed WI-06 reconciliation gate.

### Implementation Contract
- Inputs and outputs: load recipes from `app/assets/data/recipes.json`; resolve ingredient/output stable item names against `BlockRegistry`; reject duplicate IDs, unknown items, invalid counts, and unknown categories with actionable errors.
- Runtime integration: a gameplay crafting service owns validation and atomic inventory mutation; app state publishes catalogue/view state only. Browser supports directional focus, craft, move/swap, split where defined, and drop without pointer drag.
- Threading and performance: catalogue loads once; crafting is bounded by inventory slots plus ingredients; no filesystem access during fixed ticks.
- Platform and dependencies: bundled JSON and item icon paths enter VPK/manifest validation; document schema and examples.

### Acceptance Criteria
- [ ] Existing recipes reproduce identical outputs from JSON and no recipe definition remains in `state_machine_render.cpp`.
- [ ] Invalid catalogue fixtures fail before gameplay with the exact recipe/field named.
- [ ] Crafting is atomic: insufficient input or full output capacity leaves inventory unchanged.
- [ ] Keyboard and gamepad E2E can browse categories, craft repeatedly without closing, move items, and drop them.
- [ ] Square slots remain stable at required aspect ratios and 200% UI scale.
- [ ] Build, full CTest, frontend/E2E, package asset check, docs, and desktop smoke are green.

### Verification Commands
```text
npm --prefix ui run build
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Recipe|Crafting|Inventory|WebUiHud|Assets"
npm --prefix ui run lint
npm --prefix ui run test
npm --prefix ui run test:e2e
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: data catalogue/schema, crafting service, UI focus/actions, docs, assets, and tests.
- Observed: add one fixture recipe, rebuild assets, discover it, craft it, and rearrange output with keyboard/gamepad.
- Results: record validation fixtures, E2E matrix, and desktop result.
- Known gaps: recipe discovery progression is data-supported but progression content is future work.

## WI-10.03: Implement data-driven tool actions and durability

### Goal
Make axes, pickaxes, shovels, sledgehammers, swords, and hoes materially change world interaction
instead of behaving as inert inventory items.

### Scope
- Own: tool catalogue/schema, material tier/stats, action compatibility, mining-speed calculation, combat damage, till action, durability, recipes/content, persistence/network events, tests.
- Exclude: held rendering/animation, enchantments, repair, ranged tools, and crop implementation.
- Prerequisites: WI-10.01 damage API and WI-10.02 recipe catalogue.

### Implementation Contract
- Inputs and outputs: define tool kind, tier, durability, base damage, swing interval, and per-block-tag speed multipliers in validated data. Server authoritatively resolves the selected item and action.
- Runtime integration: breaking progress uses tool/block tags; sword attack uses the damage API; hoe converts eligible dirt to a named tilled block consumed by WI-11.04; durability decrements only after successful effects.
- Threading and performance: fixed simulation; catalogue lookup is indexed by stable item ID; no string scans in the hot interaction loop.
- Platform and dependencies: recipes and item/model placeholders are bundled and registered.

### Acceptance Criteria
- [ ] Each tool family has at least stone and iron definitions and recipes with distinct measurable effects.
- [ ] Wrong tools, correct tools, bare hands, broken tools, and unbreakable blocks produce tested timings/results.
- [ ] Durability survives save/load and cannot diverge or decrement twice through networking.
- [ ] Hoe emits a real till mutation; sword emits damage only inside reach and cooldown.
- [ ] Build, full CTest, data validation, asset ledger, and desktop interaction smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Tool|Mining|Damage|Inventory|Persistence|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: tool data/service, interaction timing, durability, till/combat effects, recipes, tests, and assets.
- Observed: craft/equip representative tools and compare their intended actions.
- Results: record timing table, persistence/network results, and smoke notes.
- Known gaps: tilled soil becomes plantable in WI-11.04.

## WI-10.04: Render and procedurally swing held tools

### Goal
Show the selected hand/tool in first person and animate a clear, responsive swing synchronized with
the authoritative action cadence.

### Scope
- Own: first-person held-item pass, item/model resolution, procedural transform animation, action feedback, reduced motion, tests, assets.
- Exclude: skeletal animation, third-person player animation, animal animation, and tool gameplay stats.
- Prerequisites: WI-10.03 and existing model/atlas registries.

### Implementation Contract
- Inputs and outputs: consume selected item plus action phase; produce camera-relative transforms for idle bob, wind-up, contact, and recovery. Rendering never changes action authority.
- Runtime integration: render after world transparent geometry and before HUD/UI; action events start animation, while held input may chain only at the server-approved cadence.
- Threading and performance: render thread only; reuse model buffers; one held-item draw group and under 0.15 ms GPU at 1080p.
- Platform and dependencies: generated VMDL/icon fallbacks ship for every launch tool; reduced-motion preference limits bob and swing amplitude.

### Acceptance Criteria
- [ ] Empty hand, block, and each tool family have readable first-person silhouettes and correct material.
- [ ] Swing contact aligns with break/attack/till events and cannot accelerate gameplay by animation cancellation.
- [ ] Camera clipping, extreme FOV, left/right aspect bounds, underwater tint, and pause transitions are covered.
- [ ] GPU tests catch missing model/material and depth-order regressions.
- [ ] Build, full CTest, asset ledger, performance measurement, and desktop smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "HeldItem|Tool|Render|Gpu"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: held-item renderer/animation, integration, models/icons, tests, and render diagram.
- Observed: use every tool family at normal and reduced motion.
- Results: record event alignment, screenshots, draw calls, and GPU timing.
- Known gaps: third-person actor animation is owned by the entity phase.