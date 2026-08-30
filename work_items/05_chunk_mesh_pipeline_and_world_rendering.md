# Work Item 05 — Chunk Mesh Pipeline & World Rendering

**Phase:** B — Making the World Visible · **Prerequisites:** 03, 04

---

## 1. Problem Statement

`world/geometry.cpp` contains a working greedy mesher whose output has never reached a GPU. There is no chunk mesh cache, no vertex format, no upload path, no culling, no lighting, and no transparent pass. Generated worlds are invisible.

## 2. Objective

Close the gap between "the world exists in memory" and "the world is on screen": a threaded mesher feeding a GPU mesh cache, with lighting, ambient occlusion, frustum culling, and a correct transparent water pass.

## 3. Scope

**In scope:** vertex format, mesher upgrade (neighbour-aware, lighting/AO, transparency split), `render/chunk_renderer`, mesh job scheduling, GPU upload budgeting, `chunk.glsl`.

**Out of scope:** chunk streaming around a moving player (06), custom block models (13).

## 4. Implementation Tasks

1. **Vertex format** (`render/chunk_vertex.hpp`) — packed for bandwidth:
   `position` (local chunk coords, quantized), `normal`/face index (3 bits), `uv`, `atlasLayer` (u16), `ao` (2 bits per vertex), `skyLight`/`blockLight` (4 bits each), `tint` index. Document the packing; keep an unpacked debug variant behind a flag.
2. **Neighbour-aware meshing.** The mesher takes the chunk plus its six neighbours so boundary faces are culled correctly. A chunk whose neighbours are not yet resident is meshed **provisionally** and re-meshed when they arrive — never rendered with a wall of faces at the seam.
3. **Lighting & AO in the mesher:**
   * Per-vertex ambient occlusion from the three neighbouring voxels at each corner (standard 0–3 AO term), with the anti-flip quad-triangulation rule.
   * Sample the chunk's skylight/blocklight values (already stored on `Chunk`) into the vertex.
   * A directional "sun" term plus per-face brightness multiplier (top brightest, bottom darkest) computed in the shader.
4. **Transparency split.** The mesher emits two index ranges: opaque and transparent. Water and glass go to the transparent range. Adjacent identical liquid faces are culled against each other; liquid-to-air faces are not. Water renders at a slightly reduced height for a surface look.
5. **`ChunkRenderer`** (`engine/render/chunk_renderer`):
   * `chunkCoord → GpuChunkMesh { vao, vbo, ibo, opaqueRange, transparentRange, aabb }` cache.
   * Dirty set + priority queue ordered by distance to camera.
   * `EnqueueMeshJob(chunk)` → `JobSystem` worker produces `ChunkMeshData`; main thread drains the completed queue and uploads, **capped at N chunks or M milliseconds per frame** (ADR-008, §8 budget).
   * Buffer reuse: keep freed VBO/IBO objects in a size-bucketed pool instead of churning GL objects.
   * `Render(camera)`: frustum-cull by chunk AABB, submit opaque draws front-to-back, then transparent draws back-to-front.
6. **Block edit invalidation.** `World::SetBlock` marks the owning chunk dirty plus any neighbour whose boundary the edit touched. A single block change must re-mesh at most 4 chunks.
7. **`chunk.glsl`:** atlas array sampling, AO multiply, sky/block light mix, directional light, distance fog matched to render distance, alpha blending for the transparent pass with depth-write off.
8. **Remove the temporary test scene** from work item 03. The world is now the scene.
9. **Metrics:** expose loaded/meshed/visible chunk counts, draw calls, triangles, mesh-job queue depth, and last-frame upload time for the F3 overlay.

## 5. Acceptance Criteria

* A generated world is **visibly rendered**: textured terrain with hills, exposed stone, dirt/grass surface, trees, water pools, and cave openings.
* Interior faces are not drawn; chunk seams show no missing or doubled faces.
* Water is translucent and correctly sorted behind/in front of terrain.
* Ambient occlusion darkens block corners and crevices; the world reads as three-dimensional rather than flat.
* Meshing never blocks the frame — moving through freshly generated terrain produces no visible hitch.
* At render distance 8 the frame stays within the §8 performance budget.

## 6. Automated Tests

`tests/test_chunk_meshing.cpp`:
* `Mesher.SingleBlockProducesSixQuads`.
* `Mesher.SolidChunkProducesOnlyBoundaryFaces` — a fully solid 16³ chunk yields exactly the six outer faces after greedy merging.
* `Mesher.GreedyMergingReducesQuadCountForFlatPlane` — an assert on the exact merged quad count, not just "fewer".
* `Mesher.NeighbourAwarenessCullsSharedBoundaryFaces` — two adjacent solid chunks produce no faces on the shared plane.
* `Mesher.TransparentBlocksGoToTransparentRangeOnly`.
* `Mesher.AmbientOcclusionValuesMatchExpectedCornerCases` — table-driven over the 8 neighbour configurations.
* `Mesher.IsDeterministicForIdenticalChunkData` — same input, byte-identical vertex buffer.
* `ChunkRenderer.BlockEditMarksOwnerAndAffectedNeighbours` — edits in the interior dirty 1 chunk; edits on a corner dirty at most 4.
* `ChunkRenderer.UploadBudgetIsRespected` — with 100 pending meshes, at most N upload per frame.
* `[.gpu] ChunkRenderer.RendersKnownChunkToOffscreenTarget` — readback asserts non-background pixels where terrain should be.

## 7. Anti-Shell Checks

- [ ] Mesh data reaches real GL buffers; no path stops at "computed the quads".
- [ ] `world/` still does not include `graphics/`.
- [ ] The temporary render-test scene is deleted, not left behind a flag.

## 8. Assets & Human Actions

None beyond the textures from work item 04. Report measured chunk counts, draw calls, and FPS in the completion report.

## 9. Verification

Build, test, launch, and **describe the rendered world** with a screenshot-grade description: terrain shape, texturing, water, AO, chunk count, FPS.
