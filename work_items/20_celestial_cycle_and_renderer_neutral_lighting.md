# Work Item 20 - Celestial Cycle and Renderer-Neutral Lighting

## Goal

Add an authoritative time-of-day system with sun and moon illumination, then feed a backend-neutral lighting state to the voxel renderer so daylight, twilight, and night change smoothly without baking a particular graphics API or a hardcoded sun direction into world code.

After this item, players can observe a continuous day/night cycle, with a configurable always-day world remaining fixed at daytime. The host is authoritative, and joined players see the same celestial state and lighting.

## Scope

- **Own:** time-of-day state and persistence, hosted-server authority and replication, sun/moon direction and spectral/ambient values, sky-light intensity policy, renderer-neutral scene-light contract, OpenGL reference uniform binding, linear-light color handling, settings/HUD observability, and focused tests.
- **May change:** world options and save schema, network world/session packets, `GameSession`, app state composition, render public interfaces, chunk shader sources/fallbacks, preferences/settings, documentation, and diagrams.
- **Exclude:** local voxel propagation and edit relighting (item 19), shadow-map resources and filtering (item 21), weather, volumetric clouds/fog, and implementation of Metal/Vulkan/DX11 backends.
- **Prerequisites:** Item 19 must be complete. This item provides the directional-light and ambient inputs consumed by item 21.

## Implementation Contract

### Simulation and Authority

- Define a versioned `CelestialClock` owned by the authoritative `GameServer`, expressed in simulation ticks or a normalized day fraction with a documented real-time day length. Advance it on the fixed $60\,Hz$ simulation step, not the render frame rate.
- Persist it with world metadata and include it in join/initial-state synchronization plus bounded periodic updates. Clients interpolate received state for smooth rendering but never choose gameplay-relevant time locally.
- Make `WorldOptions::alwaysSunny` an actual policy: it fixes the clock and light state at documented daytime values. Preserve it through create, save/load, server join, and settings UI.

### Portable Lighting Contract

- Introduce a plain render-facing `SceneLighting` value type owned above `graphics` and supplied once per frame. It must contain normalized sun and moon directions, direct-light colors/intensities, hemispherical/sky ambient contribution, global skylight multiplier, and clear/fog colors in linear space.
- Keep world light propagation independent of renderer APIs. Item 19's voxel light values remain local visibility/light transport; the global celestial multiplier and directional vectors are render inputs, not per-voxel mutations every frame.
- Define the API-neutral shader/resource contract in the renderer/RHI boundary before OpenGL binding. The OpenGL implementation may use uniforms, but no world, gameplay, or public renderer interface may expose OpenGL types or GLSL-only assumptions.
- Remove the hardcoded shader sun vector and hardcoded sky tint. Bind the scene lighting each frame to both external shader assets and compiled fallback sources. Keep CPU and shader math documented well enough to reproduce in Metal, HLSL, and SPIR-V backends later.
- Perform lighting math in linear space and apply one documented display transform/tone map at the final color boundary. Do not apply gamma twice. Retain material tinting, vertex AO, sky/block light, transparency, and fog under the new contract.

### Player Observability and Budgets

- Add F3 readouts for time, active celestial direction/intensity, and the active global sky-light multiplier. The settings/menu state must make always-day behavior clear without relying on a console log.
- The update must add no per-voxel relight work per tick, no render-thread allocation per frame, and no measurable frame pacing regression at render distance 8. Record before/after frame metrics in the completion report.

## Acceptance Criteria

- [ ] Starting the same seeded world with the same saved time produces the same celestial state; advancing exactly $N$ fixed ticks produces the same state independent of render frame timing.
- [ ] Host and client converge on the same server-owned time after join and remain within a documented interpolation tolerance during normal packet delivery.
- [ ] The sun smoothly transitions through daylight and twilight; the moon provides a distinct, low-intensity night directional contribution. The world never jumps between hardcoded lighting states.
- [ ] `alwaysSunny` visibly locks daylight and survives world creation, persistence, and multiplayer world-info transfer.
- [ ] The GL chunk renderer consumes the new `SceneLighting` values; no hardcoded sun vector or sky tint remains in either loaded or fallback chunk shader source.
- [ ] Tests cover deterministic clock progression, save/load, packet encode/decode, always-day policy, and renderer-contract input validation. A test must fail if time derives from variable render delta.
- [ ] A desktop smoke run observes a full accelerated cycle or deterministic time-debug control, verifies an always-day world remains stable, and verifies a joining client sees the host's current time.
- [ ] `ARCHITECTURE.md` and `DIAGRAMS.md` document the server-to-client celestial path and the frame-level scene-light binding. No new asset request is needed unless a deliberately added sun/moon visual asset requires one.

## Verification Commands

```text
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

Expected result: warning-free build, all tests green, and a human-observed non-headless cycle in both local hosted play and a join-client scenario.

## Completion Evidence

- Changed: simulation clock, save/network schema, scene-light contract, GL binding/shaders, UI diagnostics, tests, and architecture flow.
- Observed: daylight, twilight, moonlit night, always-day behavior, and synchronized client time.
- Results: test counts, deterministic tick evidence, network convergence tolerance, and frame-time comparison.
- Known gaps: PCF-filtered cascaded shadows remain owned by item 21.
