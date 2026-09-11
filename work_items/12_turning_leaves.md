# v0.13 - Turning Leaves

This phase derives a persistent seasonal calendar from the shared world clock and applies it through
bounded simulation rules. Weather remains a later consumer, not a second calendar.

## WI-12.01: Derive and persist the four-season calendar

### Goal
Give every world a deterministic Spring, Summer, Fall, Winter cycle of 20 in-game days per season.

### Scope
- Own: season enum/calendar math, world-generation initial season policy, save/network representation, transition events, tests, debug output, docs.
- Exclude: visual ecology changes, weather, snow accumulation, and crop modifiers.
- Prerequisites: WI-09.01 world clock.

### Implementation Contract
- Inputs and outputs: derive season index, day-in-season, and progress from authoritative world day plus a persisted initial offset selected deterministically at world creation. Sequence is fixed and each season spans exactly 20 days.
- Runtime integration: server emits one transition event at boundaries and replicates compact calendar snapshots; clients never advance independently.
- Threading and performance: constant-time pure calendar math on simulation thread; no per-frame save writes.
- Platform and dependencies: add versioned metadata/migration and stable display/localization IDs.

### Acceptance Criteria
- [ ] Boundary tests cover day 0, days 19/20, all transitions, full-year wrap, and large tick values.
- [ ] New-world initial season is deterministic for creation inputs and load resumes exact season/day/progress.
- [ ] Loopback and remote clients observe one identical transition event with no duplicate after reconnect.
- [ ] Debug display reports season, day-in-season, and transition tick.
- [ ] Build, full CTest, docs, and accelerated desktop transition smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Season|WorldClock|Persistence|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: calendar contract, save/network metadata, events, tests, and diagrams.
- Observed: advance across each boundary and reload mid-season.
- Results: record boundary matrix and desktop observation.
- Known gaps: visible ecology consumes the calendar in WI-12.03.

## WI-12.02: Present season status in the inventory route

### Goal
Show the current season and progress as a compact decorative illustration and name within the
inventory/crafting experience.

### Scope
- Own: HUD payload calendar fields, season illustration assets, responsive inventory placement, transition notification, accessibility/E2E tests.
- Exclude: simulation effects, weather forecast, full calendar screen, and production art.
- Prerequisites: WI-12.01 and WI-10.02 controller-complete inventory route.

### Implementation Contract
- Inputs and outputs: publish immutable season ID, localized name, day 1-20, and progress only when changed; browser resolves a verified local illustration.
- Runtime integration: inventory shows one un-nested season region without reducing square inventory slots; transition uses the event feed and reduced-motion-aware presentation.
- Threading and performance: event-driven UI publication; no per-frame payload churn; illustration remains within existing browser memory budget.
- Platform and dependencies: generated square RGBA8/approved vector art for four seasons enters manifest and asset ledger.

### Acceptance Criteria
- [ ] Inventory shows correct name, day, progress, and distinct illustration for all four seasons.
- [ ] Layout remains usable at 4:3, 16:9, 16:10, 21:9, and 200% scale with controller focus unchanged.
- [ ] Illustration has semantic text equivalent and season is not communicated by color alone.
- [ ] Browser/native tests catch stale season payloads and boundary notifications.
- [ ] Build, full CTest, frontend/E2E, assets, and desktop route smoke are green.

### Verification Commands
```text
npm --prefix ui run build
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Season|WebUiHud|PlayerUiProtocol"
npm --prefix ui run lint
npm --prefix ui run test
npm --prefix ui run test:e2e
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: season payload/UI/assets, bridge docs, tests, and manifest.
- Observed: inspect every season at required aspect ratios and input methods.
- Results: record E2E matrix and screenshots.
- Known gaps: final illustration art may replace registered placeholders.

## WI-12.03: Apply bounded seasonal vegetation and snow rules

### Goal
Make seasons visibly and mechanically affect wild grass, crops, leaves, and winter ground without
regenerating or synchronously scanning the entire world.

### Scope
- Own: season modifier data, dormant/dead plant states, leaf-decay scheduling, snow placement/removal baseline, growth modifiers, chunk catch-up, tests, assets.
- Exclude: active precipitation, blizzards, deep layered accumulation, biome remeshing at unlimited distance, and permanent tree destruction.
- Prerequisites: WI-11.01, WI-11.03, WI-11.04, WI-12.01, and WI-12.02.

### Implementation Contract
- Inputs and outputs: data maps season to growth multiplier, plant dormancy, leaf retention probability, and snow eligibility. Changes are evaluated lazily on active/random ticks and bounded catch-up from last season evaluation.
- Runtime integration: server mutates persistent plant/snow state; renderer may apply global tint immediately while structural changes arrive under simulation budget. Returning to a chunk yields the calendar-equivalent state.
- Threading and performance: no whole-world transition pass; active seasonal work remains within WI-11.01 budget and avoids remesh bursts.
- Platform and dependencies: snow/seasonal plant textures and sounds use registered placeholders.

### Acceptance Criteria
- [ ] Wild grass reaches maturity then becomes dormant/dead according to winter policy and resumes only under configured season rules.
- [ ] Crop growth modifiers, tree leaf thinning/fall, and eligible snow placement are deterministic and data-driven.
- [ ] Season transitions do not enqueue unbounded block edits or mesh uploads.
- [ ] Unloaded chunks catch up to the same bounded result as continuously loaded fixtures.
- [ ] Build, full CTest, performance metrics, assets/docs, and accelerated full-year desktop smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Season|Vegetation|Farming|Snow|RandomTick|Determinism"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: seasonal rule data/simulation/render state, assets, tests, and diagrams.
- Observed: accelerate through one year while inspecting the same loaded and reloaded plots/trees.
- Results: record edit/mesh queue peaks, frame timing, deterministic hashes, and screenshots.
- Known gaps: active weather and layered accumulation are WI-14.03/WI-14.04.