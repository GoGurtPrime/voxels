## WI-05.01: Render a data-driven voxel-world cinematic behind transparent menus

### Goal
Present the main menu above a real, smooth sequence of sweeping generated-world camera shots without entering gameplay or allowing UI opacity to hide the scene.

### Scope
- Own: menu background scene/session, deterministic generation/camera-sequence asset format and loader, renderer integration, lifecycle/resources, menu visual treatment, tests, profiling, and asset records.
- Exclude: replay/camera editor, video decoding, gameplay-world mutation, and HUD/pause migration.
- Prerequisites: WI-03 and WI-04; existing generation, chunk renderer, camera, and asset manager.

### Implementation Contract
- Inputs and outputs: load versioned JSON camera-sequence assets containing seed, world options, ordered shots, duration, position/look-at or spline controls, FOV, easing, time-of-day, and loop transition. Reject non-finite coordinates, non-positive durations, excessive shots, and unsupported generator versions. `MenuBackgroundScene` owns a read-only world with no save/network/player authority.
- Runtime integration: MainMenu owns/requests the background scene. It schedules generation/meshing through the existing job system with capped work and renders before web compositing. An invalid sequence selects a deterministic low-cost visible fallback and logs/registers the asset failure.
- Threading and performance: retain worker generation and main-thread GPU uploads. At target 1080p, background CPU+GPU median is $\leq 12$ ms and p99 is $\leq 16.6$ ms after warmup; UI retains the WI-03 budget. Menu input never waits for generation.
- Platform and dependencies: no browser video, canvas, or remote media. Browser receives only route/model opacity/layout data; world samples, camera interpolation, and metrics remain native.

### Acceptance Criteria
- [ ] Main menu visibly renders a voxel landscape with at least three authored camera shots and a seamless loop; controls leave substantial scenery visible.
- [ ] The scene cannot create saves, affect GameSession, consume gameplay input, or leak GPU resources when transitioning/quitting.
- [ ] Sequence validation rejects malformed data and determinism tests reproduce camera transforms for a seed/timestamp.
- [ ] Integration tests verify ordering: background world, world-space/debug effects, then alpha-composited web UI.
- [ ] Profiling reports background frame time, loaded/meshed chunks, queue depth, and UI time; a scripted capture meets the budget on target hardware.
- [ ] Document asset format and register required final art/audio in `ASSET_REQUESTS.md`.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "MenuBackground|WebUiMenu|ChunkMeshing"
build/tests/Debug/voxels_tests.exe "[menu-background]"
build/app/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: menu scene, sequence assets/validation, render integration, metrics, tests, and docs.
- Observed: generated-world shots appear behind transparent React menu controls while navigating.
- Results: record sequence seed/hash, frame median/p99, memory/mesh metrics, tests, and desktop observation.
- Known gaps: in-game HUD and pause are WI-06.
