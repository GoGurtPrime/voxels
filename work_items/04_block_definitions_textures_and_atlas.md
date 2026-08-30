# Work Item 04 — Block Definitions, Textures & Atlas

**Phase:** B — Making the World Visible · **Prerequisites:** 03

---

## 1. Problem Statement

Blocks exist only as ids with solid/transparent flags. There is no visual data — no textures, no per-face UVs, no material properties, no hardness, no sounds. `app/assets/` contains two JSON files and nothing else. There is no image decoding and no atlas.

## 2. Objective

A **data-driven block catalogue** plus a runtime texture atlas, so the renderer can look up what any block face should look like, and so new blocks can be added without touching C++.

## 3. Scope

**In scope:** `world/block_registry`, `assets/texture_loader`, `render/texture_atlas`, `app/assets/data/blocks.json`, `app/assets/textures/blocks/*.png`, procedural texture generation.

**Out of scope:** meshing (05), `.vmdl` custom shapes (13), pack bundling (15).

## 4. Implementation Tasks

1. **`blocks.json` schema** at `app/assets/data/blocks.json`, one entry per block:
   ```jsonc
   {
     "id": "stone",
     "numeric_id": 1,
     "display_name": "Stone",
     "solid": true,
     "opaque": true,
     "liquid": false,
     "hardness": 1.5,
     "light_emission": 0,
     "textures": { "all": "blocks/stone" },      // or per-face: top/bottom/north/south/east/west
     "render_type": "cube",                        // "cube" | "cross" | "liquid" | "model"
     "model_id": null,                             // set in work item 13
     "sounds": { "break": "sfx/break_stone", "step": "sfx/step_stone", "place": "sfx/place_generic" },
     "drops": [{ "item": "stone", "count": 1 }]
   }
   ```
   Ship the launch set: `air`, `stone`, `dirt`, `grass`, `sand`, `gravel`, `water`, `coal_ore`, `iron_ore`, `wood_log`, `leaves`, `planks`, `glass`, `bedrock`.
2. **`BlockRegistry` load path:** parse `blocks.json` at boot, validate (unique ids, referenced textures resolvable, numeric ids dense), and expose `GetDefinition(BlockId)` returning face texture indices, physical flags, hardness, light emission, and sound ids. A malformed entry is a **hard startup error with the offending id and line**, not a silent skip.
3. **Image loading:** `assets/texture_loader` using `stb_image` — decode PNG to RGBA8, with dimension validation (power-of-two, square, consistent across the block set).
4. **`TextureAtlas`** (`engine/render/texture_atlas`):
   * Build a GL 2D **array texture** (one layer per block texture) — avoids mip bleeding between tiles entirely and keeps sampling trivial. Fall back to a single 2D atlas grid with padded tiles if array textures are unavailable.
   * Generate mipmaps with nearest filtering at the base level (crisp voxel look), anisotropy from settings.
   * `LayerFor(std::string_view textureName)` → index used by the mesher's vertex format.
   * Emit a debug dump of the atlas to `<userdata>/logs/atlas_dump.png` when `--dump-atlas` is passed.
5. **Procedural texture generation (filler content, committed to disk):** a `TextureForge` utility that generates a distinct, *deliberate-looking* 16×16 RGBA texture per block type — value-noise granite for stone, speckled brown for dirt, grass blades on the top face, wood grain rings, translucent blue for water, ore flecks over stone. Write them with `stb_image_write` to `app/assets/textures/blocks/` if the file is absent, and commit the results. The world must look intentional, not magenta.
6. **Missing-texture behavior:** a referenced texture that cannot be resolved gets the classic magenta/black checker, a warning log, and an [ASSET_REQUESTS.md](../ASSET_REQUESTS.md) entry — the game keeps running.
7. **Tinting hooks:** grass top and leaves carry a biome tint color multiplier in the block definition, applied in the fragment shader, so a later biome pass needs no mesher change.

## 5. Acceptance Criteria

* `app/assets/textures/blocks/` contains real, committed PNG files for every launch block.
* The atlas builds at boot; `--dump-atlas` produces a visually correct image containing every block texture.
* Adding a new block to `blocks.json` with an existing texture makes it available to the runtime **with no C++ change**.
* A deliberately corrupted `blocks.json` produces a clear, actionable startup error naming the entry.

## 6. Automated Tests

`tests/test_block_catalogue.cpp`:
* `BlockRegistry.LoadsAllLaunchBlocksWithExpectedFlags` — count, uniqueness, dense numeric ids, spot-check flags.
* `BlockRegistry.RejectsDuplicateOrMalformedDefinitions` — asserts a descriptive error, not a silent skip.
* `BlockRegistry.PerFaceTextureResolutionFallsBackToAll`.
* `TextureLoader.DecodesGeneratedPngRoundTrip` — forge → write → decode → pixel equality.
* `TextureAtlas.AssignsUniqueLayerPerTextureAndIsStableAcrossRuns` — layer assignment is deterministic (packs must be reproducible).
* `TextureAtlas.MissingTextureResolvesToCheckerPlaceholder`.
* `TextureForge.GeneratesDeterministicTexturesForSeed`.

## 7. Anti-Shell Checks

- [ ] Textures are real files on disk, not runtime-only buffers.
- [ ] Block visual data is read from JSON, not hard-coded in a `switch`.
- [ ] The atlas is uploaded to a real GL texture object.

## 8. Assets & Human Actions

Add the full block-texture row set to [ASSET_REQUESTS.md](../ASSET_REQUESTS.md) (16×16 RGBA8 PNG each), noting that procedurally forged versions are shipping and can be replaced file-for-file at any time with no code change. List the exact filenames in the completion report so the operator can drop in art immediately.

## 9. Verification

Build, test, launch with `--dump-atlas`, and **describe the dumped atlas** and the textures now visible on the work item 03 test scene.
