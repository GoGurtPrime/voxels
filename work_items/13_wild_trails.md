# v0.14 - Wild Trails

This phase delivers animals in vertical slices over a reusable actor runtime. It does not begin with
four hard-coded classes: entity ownership, rendering, and navigation are proven first with synthetic
actors, then species content is loaded through validated data.

## WI-13.01: Establish a server-authoritative simulation entity runtime

### Goal
Create a bounded, persistent, replicated runtime for non-player actors and future hostile entities.

### Scope
- Own: stable entity IDs, component/state storage, spatial index, spawn/despawn lifecycle, distance-tiered ticking, persistence, replication, debug metrics, tests, docs.
- Exclude: animal AI, navigation, rendering, breeding, pets, and combat behavior.
- Prerequisites: WI-09.01 clock, WI-10.01 health/damage types, existing server/network loop.

### Implementation Contract
- Inputs and outputs: entities have stable ID, type ID, transform/velocity, health, lifecycle state, and optional persisted payload. Server creates/mutates/destroys; clients consume snapshots/events.
- Runtime integration: `GameServer` owns entity simulation and a chunk-aware spatial index; `GameSession` exposes immutable render snapshots. Save/load restores eligible entities without duplicating transient despawned actors.
- Threading and performance: world mutation on server thread; workers may compute immutable decisions. Tiered rates depend on player distance/visibility; 1,000 simple actors stay below 1.0 ms average server update and bounded network bandwidth.
- Platform and dependencies: protocol/save versions, malformed packet/save validation, debug counts by tier/type.

### Acceptance Criteria
- [ ] Spawn, move, damage, destroy, save/load, reconnect, and distance despawn have side-effect tests.
- [ ] Entity IDs are never reused while stale snapshots could exist and cannot duplicate across load/reconnect.
- [ ] Spatial queries return correct neighbors across chunk boundaries and edits do not require full scans.
- [ ] Tiering caps updates and bandwidth under a 1,000-actor stress fixture.
- [ ] Build, full CTest, docs/diagrams, metrics, and non-headless synthetic-entity smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "EntityRuntime|Persistence|Networking|SpatialIndex|Performance"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: entity owner/store/index, save/network schemas, metrics, tests, and architecture.
- Observed: synthetic entities persist, replicate, tier, and despawn under real gameplay.
- Results: record actor/frame/network budgets and migration fixtures.
- Known gaps: creature visuals and decisions are downstream items.

## WI-13.02: Render articulated voxel creatures in batches

### Goal
Render data-defined animal bodies whose rectangular-prism legs swing procedurally during movement.

### Scope
- Own: creature model/part schema, transform hierarchy, instanced/batched renderer, gait phase, culling, shadows/lighting, GPU tests, placeholder models.
- Exclude: AI, pathfinding, species stats, ragdolls, and authored animation files.
- Prerequisites: WI-13.01 plus model/atlas registries.

### Implementation Contract
- Inputs and outputs: species model defines body parts, pivots, parent links, material, and gait group; render snapshot supplies transform, velocity, heading, and state. Front/back legs use opposite phase with speed-scaled bounded swing.
- Runtime integration: entity renderer draws after opaque chunks and before transparent world surfaces; no render ownership enters gameplay/entity simulation.
- Threading and performance: render thread uploads; frustum/distance culling and material batching; 256 visible articulated animals remain below 1.5 ms GPU and a bounded draw count at 1080p.
- Platform and dependencies: VMDL or explicit cuboid part data with generated launch placeholders registered.

### Acceptance Criteria
- [ ] Stationary, walking, running, fleeing, and dead/despawn transition snapshots produce stable poses.
- [ ] Four-legged test model swings diagonal/opposed groups without feet translating body authority.
- [ ] Culling, winding, lighting, fog, underwater state, and chunk-boundary interpolation have GPU tests.
- [ ] Rendering 256 visible models meets triangle/draw/GPU budgets.
- [ ] Build, full CTest, assets, docs, and desktop animation smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "CreatureRender|EntityRuntime|Gpu|Performance"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: articulated model contract/renderer, culling/batching, assets, tests, and render diagram.
- Observed: inspect stationary/walking/running synthetic quadrupeds.
- Results: record visible count, triangles, draws, GPU time, and screenshots.
- Known gaps: species-specific assets are WI-13.05.

## WI-13.03: Build dynamic voxel navigation and bounded pathfinding

### Goal
Let actors find safe local routes through editable voxel terrain while respecting support, headroom,
steps, drops, hazards, and species movement capabilities.

### Scope
- Own: chunk-local walk graph, portals, capability/hazard costs, dirty invalidation, bounded local/hierarchical A*, path request queue, debug visualization, tests, metrics.
- Exclude: flying, digging, doors, group flocking, and global all-pairs navigation.
- Prerequisites: WI-13.01, current world edit invalidation, and block catalogue tags.

### Implementation Contract
- Inputs and outputs: derive walk nodes from solid support plus actor clearance; edges encode step-up, safe drop, jump, water, slope-equivalent, and hazard cost. Requests include start/goal/capability and return complete, partial, failed, or stale paths.
- Runtime integration: worker jobs build/search immutable chunk snapshots; main/server thread publishes results only if source revisions still match. Block edits dirty affected columns/chunks/portals.
- Threading and performance: strict node-expansion/time budgets and per-frame request cap; no server tick waits on path completion. Reuse paths until invalidated or behavior changes.
- Platform and dependencies: no external navmesh library is required; deterministic tie-breaking and debug overlays are mandatory.

### Acceptance Criteria
- [ ] Fixtures cover flat ground, one-block step, unsafe cliff, head obstruction, water capability, hazard avoidance, chunk portal, and no-path result.
- [ ] Editing a path cell invalidates/rebuilds only affected graph regions and stale worker results never apply.
- [ ] Flee requests choose reachable goals increasing distance from the threat, not merely opposite vectors into hazards.
- [ ] Stress tests meet named queue latency and server-frame budgets with hundreds of actors.
- [ ] Build, full CTest, docs/diagrams, metrics, and desktop debug-path smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Navigation|Pathfinding|WorldEdit|JobSystem|Performance"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: navigation data/build/search/invalidation, job integration, debug overlay, tests, and diagrams.
- Observed: inspect safe paths rerouting after terrain edits.
- Results: record graph memory, expansion counts, queue latency, and frame timing.
- Known gaps: hostile pursuit uses the same service in a future combat item.

## WI-13.04: Implement reusable animal behavior, spawning, fleeing, and drops

### Goal
Make data-defined animals roam safely, react to players and damage, vocalize, flee intelligently,
and leave configured drops under server authority.

### Scope
- Own: animal species schema/stats, behavior state machine, spawn/despawn scheduler, perception, wander/idle/flee, health/damage, death/drop integration, sound events, tests, metrics.
- Exclude: species art/content tuning, taming, pets, breeding, predators, and attacks.
- Prerequisites: WI-09.01, WI-10.01, WI-13.01, WI-13.02, WI-13.03.

### Implementation Contract
- Inputs and outputs: species data defines health, walk/run speed, active/spawn time window, biome/surface rules, group limits, awareness, flee distance, safe despawn distance, sounds, and weighted drops. Behavior states are explicit with timed transitions and deterministic seeded choices.
- Runtime integration: server schedules spawns outside immediate view but within simulation range; actors path through navigation, flee from damage instigator, run beyond render distance, and despawn only after configured safe distance/time.
- Threading and performance: perception uses spatial queries; path requests are rate-limited; far actors use coarse updates. No per-actor full player/entity/world scan.
- Platform and dependencies: generated sounds/models permitted but registered; drop items resolve through validated item catalogue.

### Acceptance Criteria
- [ ] State tests cover spawn, idle, wander, blocked path, player awareness, hit/flee, death/drop, out-of-range despawn, and save/load.
- [ ] Time-of-day and biome/surface constraints are deterministic and use the shared world clock.
- [ ] Fleeing increases navigable path distance from the attacker and avoids known cliffs/hazards.
- [ ] Damage/death/drop events replicate once and never duplicate resources.
- [ ] Stress population meets entity/navigation/audio budgets; build, full CTest, assets/docs, and desktop behavior smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "AnimalBehavior|EntityRuntime|Navigation|Damage|ItemDrop|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: species schema, behavior/spawn systems, damage/drop/audio integration, tests, and metrics.
- Observed: synthetic species roam, flee safely, despawn, vocalize, and drop configured items.
- Results: record population, AI/path timings, network bandwidth, and smoke notes.
- Known gaps: launch species tuning/content is WI-13.05; pets are WI-14.01.

## WI-13.05: Ship foxes, hedgehogs, raccoons, and deer

### Goal
Populate the world with four visually and behaviorally distinct launch species and their intended
resource drops.

### Scope
- Own: four species data sets, male deer variant, spawn habitats/times, stats/tuning, models/textures/sounds, pelts/quills/venison/antlers, drop/food recipes, integration tests, accessibility/content review.
- Exclude: taming/naming, pet respawn, breeding, predation, hostile animals, and production-quality outsourced art.
- Prerequisites: WI-13.04 and stable item/recipe catalogues.

### Implementation Contract
- Inputs and outputs: use species data only for variation. Deer drop venison; male deer additionally may drop antlers and has higher meat yield; foxes/raccoons drop pelts; hedgehogs drop quills. Exact probabilities/counts are explicit and testable.
- Runtime integration: all species use common entity/render/navigation/behavior paths and appear through ordinary world play, not a showcase-only spawn command.
- Threading and performance: combined populations remain within WI-13 budgets; unique sounds are cooldown/range limited.
- Platform and dependencies: ship legal generated filler for every model/texture/sound/item icon and append exact replacement requests.

### Acceptance Criteria
- [ ] Each species is identifiable by silhouette, gait, sound, habitat/time, speed, and health without code branches keyed to species names.
- [ ] Drop tables and male deer variant produce exactly the configured resources and food healing behavior.
- [ ] Multi-seed runtime tests prove natural spawning and bounded populations for all species.
- [ ] A 30-minute gameplay soak records no path queue growth, entity leaks, stuck population growth, or audio voice exhaustion.
- [ ] Build, full CTest, packaging/assets, performance soak, and non-headless four-species smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Animal|Species|Spawn|Drop|Recipe|Performance"
ctest --test-dir build -C Debug --output-on-failure
cpack -C Debug
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: species/item/recipe content, generated assets, tuning, tests, package, and asset ledger.
- Observed: encounter, hear, follow, startle, and inspect drops from all four species in normal worlds.
- Results: record seed/habitat matrix, soak metrics, package path/hash, and screenshots.
- Known gaps: taming and home behavior are WI-14.01/WI-14.02.