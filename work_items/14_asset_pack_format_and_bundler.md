# Work Item 14 — Asset Pack Format (.vpk), Bundler & Content Pipeline

**Phase:** E — Content Tooling · **Prerequisites:** 04, 11, 13

---

## 1. Problem Statement

Content is scattered as loose files with no manifest, no validation, no versioning, and no shipping container. The existing `.vpk` code is partial archive I/O with no defined format, no integrity checking, and no producer. There is no bundling stage between authoring and the game, so there is no way to ship a coherent content set — which is exactly what the editor is supposed to produce.

## 2. Objective

A defined, validated, reproducible **asset pack** format and a bundler that turns an authored content directory into a single `core.vpk` the game mounts at boot — with loose-file override for development.

## 3. Scope

**In scope:** `.vpk` format specification, writer/reader, manifest and validation, CLI bundler, `AssetManager` mount/resolution, pack versioning, hot-reload in development.

**Out of scope:** the editor's bundling GUI (15), CDN/patching, encryption.

## 4. Implementation Tasks

1. **`.vpk` format specification** (documented in [ARCHITECTURE.md](../ARCHITECTURE.md) §6.6, implemented in `engine/assets/vpk_archive`):
   ```
   Header:  magic "VPK1", u16 formatVersion, u16 flags
            u32 entryCount, u64 tocOffset, u64 tocSize
            u32 contentVersion, u32 headerCrc32
   TOC:     entryCount × { u16 pathLength, char path[], u8 assetType, u8 compression,
                           u64 dataOffset, u64 storedSize, u64 originalSize, u32 crc32 }
   Blobs:   raw or deflate-compressed payloads, 16-byte aligned
   Manifest: a reserved TOC entry "manifest.json" — pack name, version, build timestamp,
             tool version, engine compatibility range, entry summary by type, content hash
   ```
   TOC entries are sorted by path so the same input directory always produces a byte-identical pack (reproducible builds).
2. **Writer:** `VpkWriter` — add file/buffer, deduplicate identical payloads by content hash, optional per-entry deflate (skip when compression doesn't help), compute CRCs, emit the manifest, write atomically.
3. **Reader:** `VpkArchive` — memory-map or buffered read, TOC lookup by path (hash map), `ReadEntry(path)` returning decompressed bytes, CRC verification (always in debug, opt-in in release), and clean errors for truncated/corrupt archives. Must safely reject a hostile archive: bounds-check every offset/size before reading; never trust the TOC.
4. **Bundler tool** — a CLI mode `voxels_editor --bundle --input=<content_dir> --output=<pack.vpk>` (also invocable as a CMake target `bundle_assets`):
   * Walks the content directory, classifies assets by extension/location (`textures/`, `audio/`, `models/`, `shaders/`, `data/`, `fonts/`).
   * **Validates before writing** — this is the point of the stage:
     - every texture referenced by `blocks.json` exists and has valid dimensions;
     - every sound id referenced by `blocks.json`/`sounds.json` resolves to a real clip;
     - every `model_id` referenced resolves to a valid `.vmdl` that passes codec validation;
     - block numeric ids are unique and dense; no duplicate asset paths;
     - shaders compile (syntax-check pass);
     - no orphan assets (warn, don't fail).
   * Emits a human-readable build report: entry count, size before/after compression, per-type breakdown, warnings, and the content hash.
   * **Any validation error fails the bundle with a precise message** — a broken pack must never ship.
5. **`AssetManager` mounting (ADR-012):** at boot, mount loose `assets/` first (development override), then every `assets/packs/*.vpk` in deterministic order. Resolution order: loose → packs (later packs override earlier) → procedural placeholder + warning + [ASSET_REQUESTS.md](../ASSET_REQUESTS.md) entry. Log which source satisfied each asset when `--log-asset-sources` is passed.
6. **Typed loaders:** `LoadTexture`, `LoadSound`, `LoadModel`, `LoadShader`, `LoadJson` — each returning a shared handle with reference counting and a single-instance cache. Loads happen on job workers where the format allows; GPU upload stays on the main thread.
7. **Development hot-reload:** watch the loose `assets/` directory (poll by mtime is fine) and reload changed textures, models, sounds, shaders, and `blocks.json` live, rebuilding the atlas and re-meshing affected chunks. Enabled by `--dev` and in debug builds only.
8. **Pack compatibility:** the manifest declares an engine compatibility range. Mounting an incompatible pack produces a clear startup error naming the pack and the required engine version.
9. **Ship the core pack:** wire `bundle_assets` into the build so `app/assets/packs/core.vpk` is produced from the authored content, and verify the game runs identically from the pack with the loose directory removed.

## 5. Acceptance Criteria

* `voxels_editor --bundle` produces `core.vpk` and a build report; running the bundler twice on unchanged input yields **byte-identical** packs.
* The game runs, looks, and sounds identical whether loading from loose files or from `core.vpk` only.
* Removing a texture referenced by `blocks.json` makes the bundle **fail with a message naming the block and the missing file** — not silently ship a broken pack.
* Editing a loose block texture with `--dev` updates the world without restarting.
* A truncated or tampered `.vpk` is rejected with a clear error and no crash.

## 6. Automated Tests

`tests/test_asset_pipeline.cpp`:
* `Vpk.WriteReadRoundTripPreservesEveryEntryExactly`.
* `Vpk.OutputIsByteIdenticalForIdenticalInput` — reproducibility.
* `Vpk.DeduplicatesIdenticalPayloads` — two identical files share one blob.
* `Vpk.CompressionRoundTripsAndSkipsIncompressiblePayloads`.
* `Vpk.RejectsTruncatedArchiveCorruptTocAndCrcMismatch`.
* `Vpk.HostileTocOffsetsAreBoundsCheckedAndRejected` — fuzz offsets/sizes; assert no out-of-bounds read.
* `Bundler.FailsOnMissingTextureReferencedByBlockDefinition` — asserts the message names the block and file.
* `Bundler.FailsOnMissingSoundOrModelReference`.
* `Bundler.FailsOnDuplicateNumericBlockId`.
* `Bundler.WarnsButSucceedsOnOrphanAssets`.
* `Bundler.ReportContainsAccurateCountsAndContentHash`.
* `AssetManager.LooseFilesOverrideMountedPacks`.
* `AssetManager.LaterPackOverridesEarlierPack`.
* `AssetManager.MissingAssetResolvesToPlaceholderAndLogsWarning`.
* `AssetManager.IncompatiblePackVersionIsRefusedWithNamedError`.
* `AssetManager.HandlesAreRefCountedAndCachedSingleInstance`.
* `HotReload.ChangedTextureRebuildsAtlasAndDirtiesAffectedChunks`.

## 7. Anti-Shell Checks

- [ ] The game actually boots from the pack; the pack path is not test-only.
- [ ] Validation genuinely fails the bundle rather than warning and continuing.
- [ ] No asset load path bypasses `AssetManager` resolution order.

## 8. Assets & Human Actions

Explain to the operator how to add content: drop files into `app/assets/`, run the bundler (or the `bundle_assets` build target), and ship `core.vpk`. Document the exact directory layout in the completion report.

## 9. Verification

Build, test, run the bundler, delete the loose `assets/` directory, launch the game from the pack alone, and report that it is visually and audibly identical.
