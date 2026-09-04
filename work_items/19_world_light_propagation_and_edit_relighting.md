# Work Item 19 - World Light Propagation and Edit Relighting

## Goal

Replace the current vertical-only skylight rebuild with authoritative, deterministic voxel-light propagation, so opening or closing terrain produces naturally lit caves, shafts, and small holes across loaded chunk boundaries instead of stale, extremely dark geometry.

After this item, a player who breaks a ground block can see the newly opened cavity receive skylight that attenuates through the connected volume; placing a block removes that light again without waiting for a reload.

## Scope

- **Own:** `engine/world` chunk light fields and propagation, generation-time initial lighting, block light metadata, edit-driven relighting, loaded-chunk boundary handling, authoritative multiplayer light updates, mesh invalidation, persistence/migration decision, and focused world/meshing/network tests.
- **May change:** `BlockType`/block-definition data, `World`, `GenerationPipeline`, `GameServer`/`GameClient` block-edit handling, `ChunkRenderer` dirty marking, serialization formats, architecture diagrams, and the lighting work-item records.
- **Exclude:** time-of-day progression, sun/moon rendering, shadow maps, cascades, post-processing, and new renderer backends. These belong to items 20 and 21.
- **Prerequisites:** Items 01-18 are complete. This item precedes items 20 and 21. It must preserve the existing `world -> render -> graphics` dependency direction.

## Current Defect and Controlling Path

`World::RebuildSkyLightAround` currently recomputes only the edited $(x,z)$ column from $Y=255$ to $Y=0$; its `radiusBlocks` argument is unused. The chunk mesher faithfully bakes those stale sky/block-light values into vertices, and the chunk shader uses them. The defect is therefore world-light propagation, not a mesh or shader-only defect.

The initial hypothesis is falsified if a deterministic test opens a roofed cavity beside a chunk boundary and the existing light data already attenuates through both resident chunks. Otherwise, replace the column algorithm rather than adjusting the fragment shader's minimum-light clamp.

## Implementation Contract

### Authoritative Light Field

- Keep sky light and block light as separate clamped $[0,15]$ voxel fields. `World` is the sole authority for mutating them; render code only consumes the resulting values through `ChunkMeshData`.
- Define light transmission in block metadata. Air, water, glass, and foliage must have explicit attenuation/occlusion behavior; opaque blocks stop sky propagation. Add explicit emitted-light strength for light-producing blocks rather than overloading model palette emissive data.
- Replace the edit-only column rebuild with deterministic remove-and-repropagate queues that handle both light addition and light removal. Propagation must traverse all six voxel neighbors, cross every resident chunk boundary, and stop safely at unloaded space without creating chunks merely to light them.
- Use a stable neighbor order and stable queue ordering so identical world state and edits yield byte-identical light fields regardless of worker timing. Mutate chunks on the main thread only; worker jobs may receive snapshots after relighting completes.
- Seed initial light during generation through the same rules or a documented equivalent that produces identical results at chunk boundaries. Define one policy for light fields in saves and network chunk payloads: serialize a versioned authoritative field, or deterministically rebuild it from blocks before exposing a chunk. Do not leave stale or unversioned light data ambiguous.

### Integration

- Route every authoritative `SetBlock` mutation through a relight request that covers break and place operations, including local hosted play and replicated server edits. Clients must not invent divergent lighting.
- Collect every changed resident chunk and invalidate its mesh through the existing `ChunkRenderer` owner/boundary path. Preserve edit-priority meshing and render-thread-only GPU uploads.
- Maintain the current missing-neighbor meshing rule: an unresident neighbor is occluding and produces a provisional mesh. When it later arrives, light and mesh boundaries must reconcile deterministically.
- Expose concise F3 metrics for pending relight work, changed voxels/chunks, and last edit-to-light completion time. No console-only proof.

## Acceptance Criteria

- [ ] Breaking an opaque roof block over a one-block-deep cavity produces nonzero, attenuated skylight in the cavity and visibly removes the current near-black small-hole artifact.
- [ ] Placing the same block removes skylight from the enclosed cavity; no stale bright voxel remains.
- [ ] A roof opening at $x=15/16$ and at a vertical chunk boundary relights every connected resident voxel on both sides, with no seams or dependency on edit order.
- [ ] Sky propagation attenuates correctly through each declared transmissive block type; emitted block light propagates and is removed correctly from a declared light-emitting block.
- [ ] Initial generated lighting, edited lighting, loaded-save lighting, and received network chunks conform to the documented authoritative policy and cannot diverge between host and client.
- [ ] Tests cover add/remove propagation, attenuation, boundary crossing, deterministic repeatability, generation/load policy, and mesh invalidation after a relight. At least one test fails if propagation is reduced to the former single-column implementation.
- [ ] A real desktop smoke run demonstrates break/place relighting in a shallow hole, a cave entrance, and a chunk-boundary opening; F3 shows relight activity and the edited mesh refreshes promptly.
- [ ] `ARCHITECTURE.md` and `DIAGRAMS.md` describe the light authority, queue/thread model, and edit flow. Any asset needed for a light-emitting block is registered in `ASSET_REQUESTS.md`.

## Verification Commands

```text
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
build/app/Debug/voxels_app.exe
```

Expected result: the build is warning-free, all tests pass, and the non-headless application remains open for a human to inspect the three lighting scenarios before closing it.

## Completion Evidence

- Changed: world-light API, block-light contract, generation/edit/network integration, mesh invalidation, tests, and flow documentation.
- Observed: the exact locations and before/after lighting behavior for the hole, cave entrance, and chunk seam.
- Results: full CTest counts, deterministic-test evidence, relight latency metrics, and smoke-run observation.
- Known gaps: soft directional shadows and celestial lighting remain owned by items 20 and 21.
