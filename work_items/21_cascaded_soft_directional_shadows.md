# Work Item 21 - Cascaded Soft Directional Shadows

## Goal

Implement production-quality soft directional shadows for the moving sun and moon, using a renderer-neutral shadow-pass contract and a complete OpenGL reference path. Shadows must be stable while the player moves, soften with distance, and coexist with propagated voxel light so a newly opened small hole is lit by item 19 rather than rendered as a flat black void.

After this item, terrain and block structures visibly cast stable, soft-edged shadows that track the celestial cycle, while shadow quality settings produce predictable quality/performance tradeoffs.

## Scope

- **Own:** backend-neutral shadow resources/pass description, cascade selection and stabilization, directional shadow rendering, alpha-cutout caster rules, receiver sampling, depth bias, PCF filtering, quality settings, GL 3.3 reference implementation, metrics, tests, desktop visual/performance validation, and architecture documentation.
- **May change:** the graphics/RHI and render interfaces, chunk/model shadow shaders and fallback shader sources, `ChunkRenderer` draw paths, camera data, preferences/default configuration, UI/F3 metrics, and test fixtures.
- **Exclude:** ray-traced global illumination, screen-space ambient occlusion, point-light cubemap shadows, weather/cloud shadows, volumetric lighting, and actual Metal/Vulkan/DX11 backend implementations. The contract must make those backends implementable later without embedding GL handles in higher layers.
- **Prerequisites:** Items 19 and 20 are complete. The OpenGL renderer remains the shipping reference backend under ADR-001.

## Implementation Contract

### Renderer-Neutral Design

- Define plain shadow settings and a directional-shadow pass contract: cascade count, split distances, per-cascade light view-projection matrices, depth comparison sampler semantics, filter radius, bias parameters, and enable state. Keep API resource ownership inside each backend.
- The render layer supplies camera and `SceneLighting` inputs; the backend allocates and owns depth targets, pipelines, descriptor/sampler bindings, and synchronization. Do not introduce `GLuint`, GLSL source details, or GL lifecycle requirements into `world`, gameplay, or public cross-backend data types.
- Specify the contract in terms that map directly to Metal depth textures/samplers, Vulkan image views/descriptors, and DX11 depth shader-resource views. Record shader coordinate/depth conventions and a single cascade-selection convention to prevent backend-specific visual divergence.

### Reference Rendering Path

- Implement at least two stabilized cascades for the primary active directional light, with texel-snapped projections to avoid swimming as the camera moves. Select the sun by day and the moon at night; define and test the twilight handoff.
- Render resident opaque terrain and shadow-casting sub-voxel models into the cascade depth targets before the main opaque pass. Define intentional policy for transparent, water, foliage, particles, debug primitives, and UI; alpha-cutout foliage must use the same discard threshold in caster and receiver paths.
- In the main material path, use comparison sampling with multi-tap PCF at every enabled quality level. Quality levels must scale resolution, cascade coverage, and/or tap count through validated settings, never by silently disabling shadows when a higher quality is selected.
- Use slope-scaled and normal-aware depth bias with documented bounds. Combine shadow visibility with direct celestial light only; retain propagated sky/block light, ambient sky contribution, vertex AO, texture alpha behavior, fog, and tone mapping so enclosed spaces do not become artificial black patches.
- Recreate and release all shadow resources safely on renderer initialization, resize/configuration change, context loss/shutdown, and quality change. Keep all GPU work on the main/render thread and expose no mock path as desktop proof.

### Performance and Diagnostics

- Add F3 metrics for enabled quality, cascade count, shadow-map resolution, shadow-pass draw calls/triangles, CPU timing, and GPU timing when the active backend exposes it.
- Set an initial target at render distance 8: the shadow pass plus receiver sampling stays within $3.0\,ms$ GPU on the project reference machine, and total frame time remains within the $16.6\,ms$ budget. If hardware cannot provide GPU timing, report that limitation and CPU/draw-call evidence instead.

## Acceptance Criteria

- [ ] A structure casts a visibly soft-edged shadow outdoors; its direction and length follow the sun, then transition predictably to moonlight at night.
- [ ] Shadows remain visually stable during slow camera movement and chunk streaming, with no cascade seams, swimming, full-screen flicker, or self-shadow acne beyond documented tolerance.
- [ ] A player can break a ground block in direct daylight and observe both propagated illumination in the small cavity and softened directional occlusion at its edges; the cavity is not an unnaturally black flat patch.
- [ ] Low, medium, and high shadow settings each produce a valid configured pipeline with documented cascade/filter parameters and apply live or via a clearly documented renderer restart policy.
- [ ] Tests validate cascade split/matrix determinism, texel snapping, quality configuration, sun/moon handoff, resource lifecycle bookkeeping, and the backend-neutral pass description. Add an offscreen GL readback/image comparison that detects the absence of receiver shadowing and detects an unlit opened-cavity regression.
- [ ] A desktop smoke run inspects daytime terrain shadows, a player-built structure, an excavated hole, camera movement, a night transition, and each shadow quality level. The completion report includes frame/shadow metrics at render distance 8.
- [ ] `ARCHITECTURE.md` and `DIAGRAMS.md` document the new shadow pass before the main opaque world pass, resource ownership, and the renderer-portability contract.

## Verification Commands

```text
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

Expected result: warning-free build, all tests pass including offscreen GPU checks, and a human-observed non-headless shadow sweep with measured quality/performance results.

## Completion Evidence

- Changed: shadow contract, GL resources/passes/shaders, settings, diagnostics, tests, and render-flow documentation.
- Observed: specific outdoor, excavation, movement, and day/night shadow observations at each quality level.
- Results: CTest counts, image-test artifacts/results, cascade settings, and frame/shadow timings.
- Known gaps: future render backends implement this documented pass contract; they are not falsely represented as complete by this item.
