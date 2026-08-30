# Work Item 13 — Sub-Voxel Model Format (.vmdl) & In-Game Model Rendering

**Phase:** E — Content Tooling · **Prerequisites:** 05, 12

---

## 1. Problem Statement

Every block in the world is a full cube. There is no way to express stairs, slabs, doors, torches, fences, tools, items, or creatures. The architecture calls for sub-voxel geometry, and the editor is supposed to author it, but no model format, codec, or renderer exists. Without this, the editor has nothing to produce and the game has nothing to consume.

## 2. Objective

Define and implement the `.vmdl` sub-voxel model format, its codec, and the runtime path that renders those models in the world and associates them with block and item types **through data alone**.

## 3. Scope

**In scope:** `.vmdl` specification and codec, model mesh baking, `ModelRegistry`, block `render_type: "model"` support, item view models, collision from model bounds, a set of authored sample models.

**Out of scope:** the editor GUI (15), asset packs (14), animation rigs beyond simple named transforms.

## 4. Implementation Tasks

1. **`.vmdl` binary specification** — document it in [ARCHITECTURE.md](../ARCHITECTURE.md) §6.6 and implement it in `engine/assets/vmdl_codec`:
   ```
   Header:  magic "VMDL", u16 version, u16 flags
            u8 gridX, gridY, gridZ            // micro-voxel resolution, 16^3 default (ADR-010)
            f32 pivot[3]                      // rotation/attachment origin in block units
            f32 boundsMin[3], boundsMax[3]    // authored collision box in block units
            u16 paletteCount, u32 voxelCount
            u16 elementCount, u16 attachmentCount
            u32 metadataOffset, u32 crc32
   Palette: paletteCount × { u8 r,g,b,a, u16 textureLayer, u8 emissive, u8 flags }
   Voxels:  RLE-encoded palette indices over the grid in x-fastest order (0 = empty)
   Elements: named sub-groups { name, voxel index range, local transform } for doors/lids/limbs
   Attachments: named points { name, position[3], rotation[3] } for held-item and effect anchors
   Metadata: UTF-8 JSON — author, tool version, tags, suggested block/item ids
   ```
   Little-endian, version-gated, CRC-validated, and **deterministic**: the same model always serializes to identical bytes.
2. **Codec:** `VmdlCodec::Load(std::span<const std::byte>)` / `Save(const VoxelModel&)`. Strict validation with descriptive errors (bad magic, unsupported version, palette index out of range, CRC mismatch, voxel count mismatch). Never trust the file — a malformed model must not read out of bounds. Unsupported newer versions are refused cleanly.
3. **`VoxelModel` runtime type:** palette, dense or sparse micro-voxel grid, elements, attachments, bounds, pivot; helpers `IsSolidAt`, `ComputeBounds`, `Rotate90`, `Mirror`.
4. **Model mesh baking:** reuse the greedy mesher over the micro-voxel grid, scaled by `1/gridSize`, emitting the same chunk vertex format so models share the chunk shader and atlas. Bake once at load, cache in a `ModelRegistry` keyed by `model_id`. Interior micro-voxels are culled; per-vertex AO is computed within the model.
5. **Block integration:** `render_type: "model"` plus `model_id` in `blocks.json` makes the chunk mesher emit the baked model's geometry at that block position instead of a cube, with the block's rotation/state applied. Adding a stair block becomes a data change with **zero C++ edits** — this is the acceptance test.
6. **Block state → transform:** a small block-state system (facing, half, open/closed) selects a model variant or a rotation. Placement computes facing from the player's yaw and the targeted face.
7. **Collision from models:** `boundsMin`/`boundsMax` (and optional multi-box collision for stairs) feed the physics broadphase so the player can stand on a slab and walk up stairs.
8. **Item view models:** the held-item corner render and hotbar icons use the model when one exists, falling back to the flat block-face icon otherwise.
9. **Standalone entity models:** `ModelRenderer` draws `.vmdl` instances at arbitrary world transforms — the path future creatures and dropped items will use. Prove it now by rendering dropped item entities as spinning miniature models.
10. **Sample models (authored programmatically and committed):** `stairs`, `slab`, `fence`, `door`, `torch`, `chest`, `pickaxe`, `player`. These both exercise the format and give the editor something to open on day one.

## 5. Acceptance Criteria

* Adding a stairs block to `blocks.json` referencing `models/stairs.vmdl` makes it placeable, correctly oriented, walkable, and mineable **without a single C++ change**.
* A torch model renders as a thin sub-block shape, not a cube.
* The player can stand on a slab at half height and walk up stairs.
* `.vmdl` files round-trip byte-exactly; a corrupted file produces a clear error and a placeholder model, not a crash.
* Dropped items visibly render as small spinning models in the world.

## 6. Automated Tests

`tests/test_voxel_models.cpp`:
* `Vmdl.RoundTripsAllSampleModelsByteExactly`.
* `Vmdl.SerializationIsDeterministicAcrossRuns`.
* `Vmdl.RejectsBadMagicUnsupportedVersionAndCrcMismatch`.
* `Vmdl.RejectsOutOfRangePaletteIndexWithoutOutOfBoundsRead` — fuzz a corrupted buffer, assert no UB and a clean error.
* `Vmdl.RleEncodingHandlesEmptySparseAndFullGrids`.
* `Model.BakedMeshCullsInteriorMicroVoxels` — a solid 16³ model bakes to exactly the outer shell.
* `Model.BakedMeshMatchesExpectedQuadCountForStairs`.
* `Model.Rotate90AndMirrorAreInvolutiveAndPreserveVoxelCount`.
* `Model.AttachmentPointsSurviveRotation`.
* `BlockState.FacingIsComputedFromPlayerYawForEachCardinal`.
* `Collision.SlabAndStairBoundsStopThePlayerAtCorrectHeights`.
* `Registry.DataDrivenStairBlockRendersModelGeometryWithNoCodeChange` — the headline test: load a synthetic `blocks.json` entry and assert the chunk mesh contains the model's geometry.

## 7. Anti-Shell Checks

- [ ] Models render in the running game, not only in tests.
- [ ] The block-to-model association is data, not a hard-coded mapping.
- [ ] The codec validates untrusted input rather than assuming well-formed files.

## 8. Assets & Human Actions

Sample models are generated by the agent. Tell the operator that once work item 15 lands they can author replacements in the editor and drop them into `app/assets/models/`. Register the model list in [ASSET_REQUESTS.md](../ASSET_REQUESTS.md).

## 9. Verification

Build, test, launch, place stairs/slabs/torches, stand on them, break them, and report what rendered and how collision felt.
