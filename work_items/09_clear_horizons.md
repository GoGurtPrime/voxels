# v0.10 - Clear Horizons

This phase repairs the most visible playtest regressions and establishes the persistent clock used
by later seasons, plants, weather, and animal schedules. Items are ordered where they share a
contract; `09.03`, `09.04`, and `09.05` may proceed in parallel with `09.01`.

## WI-09.01: Establish one authoritative persistent world clock

### Goal
Give hosted worlds one deterministic server-owned time source that survives save/load and can drive
lighting, seasons, growth, weather, and scheduled entities.

### Scope
- Own: world-time value and advancement, `GameServer`/`GameSession` authority, save schema migration, replication snapshot, debug observability, tests, architecture docs.
- Exclude: season rules, weather, crop growth, and final sky rendering.
- Prerequisites: existing fixed 60 Hz simulation and versioned save/network protocols.

### Implementation Contract
- Inputs and outputs: store monotonic world ticks as `uint64_t`; derive day index and normalized day fraction without accumulating floating-point drift. One in-game day is a named data constant. Pausing local play stops advancement; dedicated hosts continue while running.
- Runtime integration: the hosted `GameServer` advances time in its fixed update and publishes bounded snapshots; `GameSession` consumes replicated time for presentation. Save/load restores the exact tick.
- Threading and performance: main/server simulation thread only; constant time per tick; deterministic under save/load and different render rates.
- Platform and dependencies: version persistence and network messages; no wall-clock or timezone dependency.

### Acceptance Criteria
- [x] Equal tick sequences produce equal day/day-fraction results independent of render cadence.
- [x] Save/reload and loopback replication preserve the exact world tick and do not reset dawn.
- [x] Pause semantics and dedicated-server semantics are explicitly tested.
- [x] F3 or an equivalent developer surface shows world tick, day, and normalized time.
- [x] Default build, full CTest, docs, and non-headless clock-observation smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "WorldClock|Persistence|Networking|Runtime"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: `WorldTick` clock contract, server advancement/pause gate, exact protocol snapshots,
	level metadata schema v2 migration, F3 metrics, tests, and architecture/flow documentation.
- Observed: launched `voxels_app`, loaded `New World6`, resumed gameplay, and exited cleanly after
	inspecting the F3 clock surface and pause behavior.
- Results: Debug build passed; full CTest passed 218/218; focused clock suite passed 16 assertions
	across 4 cases, including persistence migration, pause, loopback replication, and render cadence.
- Known gaps: seasons consume this contract in WI-12.01; plant ticks consume it in WI-11.01.

## WI-09.02: Separate clear sky rendering from distance fog

### Goal
Replace the gray wall with a clear day/night sky while fog only fades distant world geometry at a
distance derived from the configured render radius.

### Scope
- Own: sky/background pass, celestial-light consumption, chunk fog uniforms/shaders, settings bounds, visual regression tests, docs.
- Exclude: clouds, precipitation, volumetric fog, and weather scheduling.
- Prerequisites: WI-09.01 and the existing `EvaluateCelestialLighting` contract.

### Implementation Contract
- Inputs and outputs: compute sky zenith/horizon colors and fog color from replicated day fraction; expose fog start/end in blocks with `end` no nearer than the loaded-world boundary minus one chunk. Clear color is never the sole sky implementation.
- Runtime integration: render sky before opaque chunks; chunk and transparent shaders consume the same camera-relative fog parameters and celestial palette.
- Threading and performance: render thread only; one full-screen or procedural sky pass, at most 0.20 ms GPU at 1080p; no per-chunk uniform lookup.
- Platform and dependencies: OpenGL 3.3 reference implementation with shader fallback sources kept in sync.

### Acceptance Criteria
- [x] Looking above the horizon shows an unobscured sky in daytime and nighttime.
- [x] Geometry fades smoothly near render distance without a visible 72-block gray cutoff.
- [x] Day and night GPU readback verifies stable sky/horizon color at four viewport aspect ratios; celestial lighting retains deterministic twilight interpolation.
- [x] Shader/GPU tests sample horizon and zenith sky pixels and retain the existing generated-terrain readback regression.
- [x] Default build, full CTest, docs, and non-headless menu/gameplay smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Celestial|Sky|Fog|Render"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: OpenGL 3.3 procedural sky pass, shared horizon fog color, render-radius-derived fog bounds, menu-preview radius alignment, GPU pixel regressions, and render documentation.
- Observed: launched `voxels_app`, inspected the menu flythrough and loaded `New World6`; the final smoke confirmed a clear blue sky with geometry-only distance fog and matching menu behavior.
- Results: focused sky/fog/render CTest passed 3/3; four-aspect sky GPU regression passed; full default CTest passed 220/220 before the final menu-radius-only adjustment.
- Known gaps: weather-specific sky states are WI-14.03.

## WI-09.03: Give music transitions explicit voice ownership

### Goal
Ensure one intended track is audible at a time and eliminate the end-of-track fade reversal and
10-20 second overlap reported during gameplay.

### Scope
- Own: mixer voice handles, per-voice gain/stop/fade commands, music director extraction from `main.cpp`, deterministic playlist state machine, audio tests.
- Exclude: Ogg streaming, final music composition, adaptive stems, and weather ambience.
- Prerequisites: existing SDL audio callback, mixer command queue, and sound bank.

### Implementation Contract
- Inputs and outputs: `Play` returns a generation-safe voice handle; gain ramps and stop commands target only that voice. The director owns menu/current/next handles and transitions through `Playing -> FadingOut -> Gap -> FadingIn` without category-wide envelope reuse.
- Runtime integration: main loop updates the director; audio callback applies sample/frame-bounded ramps without allocation or locks.
- Threading and performance: commands are lock-free and bounded; no callback allocation; transition tests use generated PCM and deterministic time steps.
- Platform and dependencies: WAV remains supported; content gaps for longer tracks remain registered in `ASSET_REQUESTS.md`.

### Acceptance Criteria
- [x] A track's gain is monotonic during fade-out and reaches silence before its voice is reclaimed.
- [x] Starting the next track cannot revive or alter the previous voice.
- [x] Menu/gameplay transitions do not mute unrelated SFX or ambience.
- [x] A synthetic two-track mixer test detects any overlapping nonzero envelopes outside an explicitly configured crossfade.
- [ ] Default build, full CTest, and a non-headless two-transition listening smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Audio|Music|Mixer"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: generation-safe voice handles, callback-side per-voice envelope ramps, explicit music director ownership/state machine, tests, and audio flow documentation.
- Observed: non-headless `voxels_app` startup initialized OpenGL, loaded the stereo menu track, and served the current UI assets. A human listening pass through two complete transitions was not completed in this session.
- Results: focused mixer assertions cover stale commands, silence-before-reclaim, sequential two-track isolation, and existing callback/category behavior; full CTest passed 224/224.
- Known gaps: Ogg streaming remains separately scoped content/audio work.

## WI-09.04: Render dropped items through the material pipeline

### Goal
Make every dropped block/item recognizable, correctly wound, textured, lit, and visible from all
expected camera angles.

### Scope
- Own: dropped-item render data, atlas/material lookup, cube/model/icon choice, batching, culling/winding tests, placeholder assets.
- Exclude: drop physics, pickup motion, inventory icons, and animal drops.
- Prerequisites: existing texture atlas, model registry, item drop list, and world-space render pass.

### Implementation Contract
- Inputs and outputs: resolve stable item IDs to an atlas-backed cube, model, or crossed icon; emit outward counter-clockwise faces with normals and alpha mode matching the material.
- Runtime integration: a dedicated item/entity pass renders after opaque chunks and before transparent world surfaces as appropriate; remove drop triangles from `GameplayHudRenderer` after parity.
- Threading and performance: render thread uploads; batch by material/atlas layer; 256 visible drops add no more than 0.5 ms GPU and a bounded draw count at 1080p.
- Platform and dependencies: OpenGL 3.3; missing item art uses a visible atlas-derived fallback and logs once.

### Acceptance Criteria
- [x] Stone, grass, glass, water, and an inventory-only item render with recognizable material treatment.
- [x] Front/back-face GPU tests catch flipped winding, missing depth, and orange fallback regressions.
- [x] Bobbing/spinning remains stable and does not alter simulation positions.
- [x] The HUD renderer no longer owns dropped-item world geometry.
- [x] Default build, full CTest, asset ledger, and non-headless inspection are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "ItemDrop|ItemRender|Gpu"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: dedicated atlas-backed item entity pass with cube/model/crossed-icon selection, alpha-mode batching, outward winding, presentation-only bob/spin, and removal of the HUD placeholder geometry.
- Observed: launched the OpenGL 3.3 desktop app, loaded `New World6`, and inspected stone, grass, glass, water, and an inventory-only drop from multiple angles; all were textured, lit, and recognizable before a clean exit after 19,250 frames.
- Results: Debug build and docs passed; focused CPU/GPU regressions passed 4/4 for five representative catalogue materials, front/back visibility, depth occlusion, one-draw 256-drop batching, and the 0.5 ms GPU timer budget; full CTest passed 230/230. The generated `build/tests/item_drop_gpu.png` regression image has SHA-256 `36ca8c32777d224092a092adba669ef9e253953c5329bb97b126f64deb4737bf`.
- Known gaps: animal-sourced item definitions arrive in WI-13.05.

## WI-09.05: Add robust dropped-item collision and magnetic pickup

### Goal
Keep drops reachable in edited terrain and draw eligible nearby items smoothly toward the player
before authoritative pickup.

### Scope
- Own: swept drop collision, depenetration, hazard/out-of-world recovery, magnet acceleration, pickup authority, simulation tests.
- Exclude: rendering, item merging, conveyor/fluid transport, and remote visual replication.
- Prerequisites: current `ItemDropSimulation`; WI-09.04 is required only for final visual smoke.

### Implementation Contract
- Inputs and outputs: treat drops as a small AABB/sphere; resolve floor, wall, and ceiling movement without tunneling. Magnetism starts inside an outer radius after pickup delay, accelerates continuously toward the player, and collects inside a smaller radius only when inventory insertion succeeds.
- Runtime integration: hosted server advances drop motion and pickup; client receives resulting drop snapshots/events. Single-player uses the same authority path.
- Threading and performance: fixed simulation thread; spatial query avoids scanning unrelated distant drops; 1,000 drops stay within 0.35 ms update on the reference machine.
- Platform and dependencies: no new dependency; constants are named and testable.

### Acceptance Criteria
- [x] Drops collide with floors, walls, ceilings, corners, and newly placed blocks without clipping or tunneling.
- [x] Embedded or out-of-bounds drops recover to a reachable nearby surface or log and despawn deterministically.
- [x] Magnet strength increases smoothly with proximity and cannot collect during pickup delay or through full inventory.
- [x] Server/client and save/leave behavior cannot duplicate a stack.
- [x] Focused stress tests, full CTest, and a non-headless pickup course are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "ItemDrop|Inventory|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: server-owned swept-AABB drop simulation, sleeping ground contact, nearest-empty-face
	squeeze recovery after block edits, 5.33-block gravity-free 3D magnetic acceleration, transactional inventory
	pickup, protocol v5 inventory/pickup messages, lifecycle cleanup, F3 metrics, and architecture
	documentation.
- Observed: launched the non-headless OpenGL app twice and loaded `New World6`. The first pass
	exposed weak attraction and unsupported recovery; after correction, the final pickup-course run
	remained active for 23,863 frames with no unreachable-drop warnings and exited cleanly after
	desktop inspection. A final adjustment smoke loaded the same world for 76,771 frames, emitted no
	collision-recovery warnings, saved the world preview, and exited cleanly.
- Results: Debug app/tests/docs build passed with zero warnings; full CTest passed 241/241.
	Focused regressions cover floors, walls, ceilings, corners, nearest-empty-face squeeze recovery,
	vertical wall sliding, reduced magnet range,
	out-of-world despawn, pickup delay, full/partial inventory, loopback exactly-once pickup, and
	hosted-world teardown. The 1,000-drop Debug stress test remained below the enforced 0.35 ms
	update budget, with live update time and collision-query counts exposed on F3.
- Known gaps: item-stack coalescing is outside this phase unless profiling proves it necessary.

## WI-09.06: Close water spawn and replacement regressions

### Goal
Guarantee a new player starts on verified dry support and can replace a targeted liquid cell with a
placeable block.

### Scope
- Own: composed new-world spawn regression, fallback search, liquid-replace placement rule, block-edit replication, tests.
- Exclude: swimming, water flow, buckets, and underwater post-processing.
- Prerequisites: current generation/spawn calculator and block interaction.

### Implementation Contract
- Inputs and outputs: spawn selection must verify the final player AABB, support, and surrounding recovery area after all generated sections are integrated. Placement may replace blocks explicitly marked replaceable (air and liquid initially), while still rejecting player overlap and non-placeable held items.
- Runtime integration: loading cannot enter `InGame` until the selected spawn passes the composed-world predicate; server-authoritative placement broadcasts the liquid-to-solid edit.
- Threading and performance: spawn search remains bounded and reports failure visibly; placement is constant-time apart from existing world edits.
- Platform and dependencies: add a block-definition `replaceable` property rather than hard-coded liquid exceptions.

### Acceptance Criteria
- [ ] A multi-seed composed loading test reproduces the full generation-to-player-spawn path and asserts dry support plus clear AABB.
- [ ] Failure to find a dry spawn expands generation/search or enters a visible error; it never returns a high fallback over water.
- [ ] A player can place into a water cell while aiming through water at solid support.
- [ ] Placement inventory consumption, mesh invalidation, skylight, and replication occur exactly once.
- [ ] Default build, full CTest, and new-world/underwater-placement smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Spawn|Water|BlockInteraction|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: spawn gate/fallback, replaceable block contract, interaction integration, tests, and docs.
- Observed: create several worlds, spawn dry, submerge, and replace water with a held block.
- Results: record seed corpus and test/smoke outcomes.
- Known gaps: liquid flow remains outside the current roadmap scope.

## WI-09.07: Implement swimming and underwater presentation

### Goal
Make entering water immediately feel and look different, with controllable buoyant movement and a
clear path back to the surface.

### Scope
- Own: player medium sampling, swim intent, buoyancy/drag/speed, water exit, camera underwater state, blue absorption/fog pass, sounds, tests.
- Exclude: drowning damage until WI-10.01, flowing water, caustic simulation, and boats.
- Prerequisites: WI-09.06 and existing player physics/render post-composition points.

### Implementation Contract
- Inputs and outputs: sample feet/body/eye liquid occupancy; apply bounded horizontal drag and buoyancy; jump/swim input rises while submerged. Publish `underwater` and submersion fraction for render/audio without giving UI authority.
- Runtime integration: server-authoritative player physics uses the same medium rules as prediction; renderer applies absorption/fog only when the camera eye is submerged and clears it on exit.
- Threading and performance: fixed simulation plus one constant-cost full-screen color/fog operation; no per-pixel world queries; post effect under 0.20 ms GPU at 1080p.
- Platform and dependencies: OpenGL 3.3 fallback shader kept in sync; generated splash/underwater ambience is registered if used.

### Acceptance Criteria
- [ ] Water reduces horizontal speed, arrests free fall, and allows the player to swim to and exit the surface.
- [ ] Eye submersion toggles a readable blue distance treatment without tinting the above-water sky.
- [ ] Prediction/server tests do not diverge over repeated water entry/exit.
- [ ] Edge tests cover shallow water, head-only submersion, jumping from shore, and water below unloaded chunks.
- [ ] Default build, full CTest, performance measurement, and non-headless swim smoke are green.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "Swimming|Water|Physics|Render|Networking"
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: medium-aware physics, prediction/replication, underwater render/audio state, tests, and diagrams.
- Observed: enter, traverse, surface, and leave several water depths.
- Results: record movement values, GPU time, and desktop observations.
- Known gaps: oxygen/drowning is owned by WI-10.01.