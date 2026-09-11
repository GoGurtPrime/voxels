# v0.12 - Green Shoots

This phase builds one sparse, deterministic plant simulation rather than separate grass and crop
loops. The foundation is independently useful to later snow, leaf decay, fire, and other evolving
block content.

## WI-11.01: Add versioned sparse block state and deterministic random ticks

### Goal
Support millions of mostly-static world cells while advancing only active evolving cells with
deterministic, persistent, server-authoritative updates.

### Scope
- Own: per-cell state representation, chunk serialization/migration, active/random tick scheduler, unloaded catch-up policy, dirty tracking, server integration, tests, metrics, docs.
- Exclude: plant-specific rules, seasons, weather, liquids, and arbitrary scripting.
- Prerequisites: WI-09.01 world clock and current region persistence.

### Implementation Contract
- Inputs and outputs: store compact typed state only for cells whose definition declares state; include schema/version, last-evaluated world tick, and deterministic seed domain. Unknown future state types fail or migrate explicitly.
- Runtime integration: server schedules bounded chunk/cell updates from world ticks, marks changed chunks dirty, and replicates resulting block/state changes. Unloaded catch-up computes bounded elapsed outcomes rather than replaying every missed tick.
- Threading and performance: world mutation on main/server thread; optional workers operate on snapshots only. Budget under 0.5 ms/frame for 10,000 active states and cap catch-up work per frame.
- Platform and dependencies: region format migration and debug metrics for active states, queue depth, updates, and catch-up debt.

### Acceptance Criteria
- [ ] Same seed, state, and tick range produce byte-identical results independent of chunk load order.
- [ ] State round-trips and migrates without dirtying unchanged chunks or invalidating old saves.
- [ ] Load after a long absence is bounded and reaches the documented equivalent state.
- [ ] Runtime edits remove/replace stale state and replicate exactly once.
- [ ] Build, full CTest, stress budget, persistence fixtures, docs, and desktop metric smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "BlockState|RandomTick|Persistence|Networking|Determinism"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: state storage/scheduler, save/network schemas, metrics, tests, architecture, and diagrams.
- Observed: active-state counters advance without frame spikes across unload/reload.
- Results: record determinism hashes, migration corpus, and timing percentiles.
- Known gaps: concrete vegetation behaviors begin in WI-11.03.

## WI-11.02: Render crossed and camera-facing plant geometry

### Goal
Render thin vegetation such as wild grass and crops as inexpensive textured planes instead of full
solid cubes.

### Scope
- Own: plant render type, crossed/camera-facing geometry choice, alpha cutout, atlas integration, lighting, batching, GPU tests, placeholder textures.
- Exclude: growth, placement, drops, wind simulation, and seasonal coloration.
- Prerequisites: texture atlas and chunk/entity render paths.

### Implementation Contract
- Inputs and outputs: block definitions select `plant_cross` or explicit camera-facing mode plus atlas layers and growth-stage state. Geometry is centered on its support block and never contributes collision/AO as a solid cube.
- Runtime integration: chunk meshing or a dedicated vegetation batch consumes plant state and draws in the alpha-cutout pass with correct depth/culling.
- Threading and performance: mesh generation follows ADR-008; 20,000 visible plants remain batched and add no more than 1.0 ms GPU at 1080p.
- Platform and dependencies: OpenGL 3.3; alpha-tested PNG placeholders enter atlas/VPK validation.

### Acceptance Criteria
- [ ] Plants remain visible from every horizontal camera angle without stretching or full-cube faces.
- [ ] Geometry sits only at block center/support height and has correct outward winding, depth, light, and shadow assumptions.
- [ ] Stage changes dirty only the owning render data.
- [ ] GPU tests cover transparent pixels, silhouette, winding, distance fog, and multiple stages.
- [ ] Build, full CTest, asset ledger, performance measurement, and desktop field smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "PlantRender|ChunkMeshing|Gpu|Atlas"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: plant render contract/meshing, atlas assets, tests, and render docs.
- Observed: inspect dense plant fields from near/far and all headings.
- Results: record plant count, triangles, draw calls, GPU time, and screenshots.
- Known gaps: vegetation behavior is WI-11.03 and WI-11.04.

## WI-11.03: Generate and grow wild grass with seed drops

### Goal
Populate grass terrain with varied wild-grass growth stages that spread at maturity and have a 50%
chance to drop one configured seed item when broken.

### Scope
- Own: wild-grass data/state, deterministic generation phase, support rules, growth/spread, winter dormancy hook, break/drop table, assets, tests.
- Exclude: farm crops, season implementation, generalized ecology competition, and production art.
- Prerequisites: WI-11.01 and WI-11.02.

### Implementation Contract
- Inputs and outputs: generation places stages only centered above grass support using a separate seed domain; mature plants attempt bounded nearby spread onto valid grass. Break drops resolve a weighted data table with exactly 50% aggregate seed probability.
- Runtime integration: vegetation generation creates initial state; random ticks advance/spread it; support edits remove unsupported grass and emit configured drops through `ItemDropSimulation`.
- Threading and performance: generation is order-independent; random spread has a hard neighbor-attempt cap; no cascading same-tick growth.
- Platform and dependencies: seed items and stage textures are data-driven and registered in assets.

### Acceptance Criteria
- [ ] Same world seed produces byte-identical initial grass positions/stages independent of generation order.
- [ ] Grass never generates or spreads onto sand, stone, water, occupied cells, or unsupported air.
- [ ] Growth reaches but never exceeds 100%; mature spread and winter-dormancy input are deterministic.
- [ ] Statistical deterministic fixtures verify the configured 50% seed-drop outcome without flaky randomness.
- [ ] Build, full CTest, assets, docs, and non-headless generation/growth/break smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "WildGrass|Vegetation|RandomTick|ItemDrop|Determinism"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: grass definitions/generation/growth/drop rules, assets, tests, and generation diagram.
- Observed: inspect varied new-world grass, advance growth, spread it, and break it for seeds.
- Results: record deterministic hashes, distribution result, timing, and screenshots.
- Known gaps: winter behavior is activated by WI-12.03.

## WI-11.04: Ship the first persistent farming loop

### Goal
Let players till supported soil, plant seeds, water plots, grow light-dependent crops over days,
and harvest useful food/seeds.

### Scope
- Own: soil/crop catalogues, tilling integration, planting, hydration, sunlight, spacing lottery, daily growth, harvest/drops, food healing, save/network/UI feedback, starter crops and one tree.
- Exclude: automation, fertilizer crafting beyond declared garden mixes, pests, genetics, and production crop art.
- Prerequisites: WI-10.01, WI-10.03, WI-11.01, WI-11.02, and WI-11.03 seed items.

### Implementation Contract
- Inputs and outputs: data defines valid substrates (tilled dirt, garden mix, super garden mix, gravel), stages, days per stage, water need, skylight threshold, spacing, mature drops, and food healing. Water adjacency or one daily watering satisfies hydration.
- Runtime integration: server validates planting/use; daily world-clock evaluation advances eligible plants. Trees require ten in-game days and a spacing lottery; blocked growth remains planted and retries later.
- Threading and performance: daily batch is spread across frames under WI-11.01 budgets; no full-world scan. Generation workers do not mutate loaded chunks.
- Platform and dependencies: starter content includes at least two vegetables, one grain/grass, one flower, and one tree with generated textures/models/audio registered.

### Acceptance Criteria
- [ ] Planting succeeds only on each crop's declared substrate and preserves the seed on rejected placement.
- [ ] Water adjacency, daily watering, natural skylight, and spacing independently gate growth in deterministic tests.
- [ ] Trees cannot mature adjacent to another winning tree and require ten complete in-game days.
- [ ] Harvest returns configured produce/seeds through world drops; food heals through the authoritative health API.
- [ ] Save/load, unload/catch-up, and multiplayer replication preserve exact stages/hydration without duplication.
- [ ] Build, full CTest, assets/docs, performance measurement, and a multi-day desktop farming smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Farming|Crop|Hydration|Growth|Health|Persistence|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: soil/crop data, planting/watering/growth/harvest, content, schemas, tests, and docs.
- Observed: till, plant, water, wait, harvest, eat, save, reload, and inspect the same plot.
- Results: record growth timeline, catch-up timing, replication tests, and screenshots.
- Known gaps: season modifiers are owned by WI-12.03.