# v0.15 - Homeward Skies

This horizon extends the proven animal and seasonal systems with persistent companions and dynamic
weather. It is intentionally planned now but should be re-estimated after v0.14 measurements.

## WI-14.01: Add persistent animal bonds, naming, follow, and stay

### Goal
Let a player befriend an eligible animal, give it a safe name, and command it to follow or stay.

### Scope
- Own: bond/owner state, eligibility/interactions, validated names, follow/stay commands, navigation behavior, persistence/networking, UI/action bridge, tests.
- Exclude: home anchors, respawn, breeding, multiple-owner permissions, and combat companions.
- Prerequisites: WI-13.05, controller-complete UI, and entity persistence.

### Implementation Contract
- Inputs and outputs: eligible species data defines bonding item/chance/rule; bonded entity stores owner player ID, UTF-8 display name within documented limits, and follow/stay mode. Server validates all mutations.
- Runtime integration: interaction opens a controller-accessible naming/command surface; follow paths toward a safe offset rather than the player's occupied cell; stay persists across unload/save/restart.
- Threading and performance: reuse navigation budgets and spatial queries; following repaths only on distance/path invalidation thresholds.
- Platform and dependencies: names are escaped in UI/chat/save/network and rejected for invalid length/control characters.

### Acceptance Criteria
- [ ] Eligible animals can be bonded, named, commanded, saved, reloaded, and observed by remote clients.
- [ ] Follow maintains a safe distance, avoids hazards, and does not repath every tick; stay prevents wander/despawn.
- [ ] Unauthorized players cannot rename or command another player's companion.
- [ ] Keyboard, pointer, and gamepad E2E complete naming and commands without leaking gameplay input.
- [ ] Build, full CTest, frontend/E2E, docs, and desktop companion smoke are green.

### Verification Commands
```text
npm --prefix ui run build
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Pet|Bond|Entity|Navigation|Persistence|Networking|PlayerUi"
npm --prefix ui run lint
npm --prefix ui run test
npm --prefix ui run test:e2e
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: bond/name/command state, behavior, UI bridge, schemas, tests, and docs.
- Observed: bond, name, follow, stay, save, reload, and reconnect with a companion.
- Results: record navigation load, E2E matrix, and desktop observations.
- Known gaps: home recovery is WI-14.02.

## WI-14.02: Give companions persistent home anchors and safe recovery

### Goal
Let players assign a bed/home location so lost or injured companions return or respawn there under
clear, non-exploitable rules.

### Scope
- Own: bed/home-anchor content, assignment, reachability checks, lost-state policy, injury/death recovery timer, safe respawn search, persistence/networking, tests.
- Exclude: player sleeping, world spawn reassignment, breeding, and instant combat teleportation.
- Prerequisites: WI-14.01 and safe-spawn/navigation services.

### Implementation Contract
- Inputs and outputs: owner assigns a valid placed bed/home block; companion stores world/position anchor. Lost companions first path home; only after documented distance/time/unreachable or recovery conditions may the server relocate/respawn them at a safe nearby cell.
- Runtime integration: block removal invalidates anchors visibly; recovery emits an owner notification. Companion health/death uses WI-10.01 damage semantics and cannot duplicate drops plus respawn rewards.
- Threading and performance: home path requests use normal budgets; safe-cell search is bounded and deferred if surrounding chunks are unavailable.
- Platform and dependencies: bed/home model, texture, item icon, sounds, and recipe ship as registered filler.

### Acceptance Criteria
- [ ] Assigning, moving, breaking, saving, and loading a home anchor produces explicit tested state.
- [ ] Separated companions navigate home when possible and recover only after configured thresholds when impossible.
- [ ] Injured/dead companion recovery preserves identity/name and cannot duplicate entity IDs or drops.
- [ ] Unsafe, flooded, obstructed, unloaded, or missing home areas defer or select a verified safe nearby point.
- [ ] Build, full CTest, assets/docs, and non-headless separation/injury/restart smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "PetHome|Recovery|Spawn|Navigation|Persistence|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: home content/state/recovery, schemas, notifications, tests, assets, and diagrams.
- Observed: separate and injure a companion, leave/reload, and observe safe home recovery.
- Results: record threshold cases, persistence results, and desktop observation.
- Known gaps: player sleeping is outside this roadmap.

## WI-14.03: Schedule and present deterministic dynamic weather

### Goal
Drive clear, overcast, rain, thunderstorm, snow, blizzard, and a data-defined rare Fall phenomenon
from the shared world calendar with coherent sky, fog, audio, and gameplay signals.

### Scope
- Own: weather state/schedule, biome/season eligibility, persistence/networking, sky/fog/light modifiers, precipitation renderer, thunder/audio, shelter exposure API, tests, assets.
- Exclude: accumulated snow mutation, flooding, lightning fire/damage, and crop destruction.
- Prerequisites: WI-09.02, WI-09.03, WI-12.01, and stable seasonal UI.

### Implementation Contract
- Inputs and outputs: server selects weather from deterministic seeded transition tables with minimum/maximum durations and seasonal/biome weights. The rare Fall event is content-configured and spoiler-safe in ordinary UI/logging.
- Runtime integration: replicated state drives celestial lighting/fog, bounded camera-local precipitation, ambience/music context, and a shelter/exposure query for future gameplay. Transitions are smooth and recover across save/load/reconnect.
- Threading and performance: precipitation is GPU-batched around the camera; no world-wide particles. Weather CPU under 0.15 ms and GPU under 1.0 ms at 1080p in blizzard conditions.
- Platform and dependencies: generated rain/snow/thunder/phenomenon assets are legally shippable and registered.

### Acceptance Criteria
- [ ] Seeded schedules reproduce across save/load while differing across world seeds.
- [ ] Every weather state has distinct sky/fog/light/audio/precipitation treatment and valid seasonal eligibility.
- [ ] Precipitation is suppressed under tested solid shelter and does not render incoherently underwater/interior.
- [ ] Remote clients observe identical state/timing; reconnect resumes rather than restarts a storm.
- [ ] Build, full CTest, assets/docs, performance stress, and accelerated desktop all-weather smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Weather|Season|Celestial|Audio|Networking|Performance"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: weather scheduler/state, render/audio integration, schemas, assets, tests, and diagrams.
- Observed: inspect all states, shelter, underwater behavior, transitions, save/reload, and reconnect.
- Results: record schedule hashes, CPU/GPU metrics, audio voice counts, and screenshots.
- Known gaps: physical snow layers are WI-14.04.

## WI-14.04: Accumulate and melt snow without world-update spikes

### Goal
Let snowfall build visible, collision-consistent snow on eligible exposed surfaces and melt it under
season/weather/light rules within strict mutation budgets.

### Scope
- Own: snow-layer state/model, exposure/temperature rules, accumulation/melt scheduler, placement collision, persistence/networking, meshing, tests, metrics, assets.
- Exclude: avalanches, snow deformation tracks, roof collapse, and fluid meltwater.
- Prerequisites: WI-11.01, WI-12.03, and WI-14.03.

### Implementation Contract
- Inputs and outputs: exposed eligible cells gain bounded layer state during snow/blizzard ticks and lose layers under configured melt conditions. Rules use global coordinates/world ticks and never scan inactive world space.
- Runtime integration: server mutates layers through random-tick budgets; renderer meshes stable layer heights; player/animal navigation and collision consume the same layer height/cost.
- Threading and performance: cap edits/remeshes per frame and retain catch-up debt; large storms cannot exceed generation/mesh upload budgets or starve player edits.
- Platform and dependencies: snow textures/model/sounds ship as registered filler.

### Acceptance Criteria
- [ ] Accumulation occurs only on exposed eligible support and respects maximum depth, obstruction, water, and warm/melt rules.
- [ ] Loaded and unloaded catch-up fixtures reach deterministic equivalent states without startup hitch.
- [ ] Player collision, block placement, dropped items, plants, and animal navigation agree on snow-layer geometry.
- [ ] Blizzard stress stays within edit, random-tick, mesh-upload, CPU, and GPU budgets.
- [ ] Build, full CTest, assets/docs, full-storm performance capture, and non-headless accumulation/melt smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Snow|Weather|RandomTick|Collision|Navigation|Persistence|Performance"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: snow state/scheduler/meshing/collision/navigation, schemas, assets, tests, and metrics.
- Observed: accumulate under storms, inspect shelter boundaries, traverse layers, save/reload, and melt.
- Results: record edit/remesh queues, frame timings, deterministic hashes, and screenshots.
- Known gaps: advanced snow deformation and avalanches are outside this roadmap.
