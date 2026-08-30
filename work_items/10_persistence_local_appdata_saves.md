# Work Item 10 — Persistence: Local AppData Saves & Settings

**Phase:** C — The Application Shell · **Prerequisites:** 06, 09

---

## 1. Problem Statement

`SaveManager` writes to a filesystem path with no defined root, no region format, no schema versioning, no atomicity, and no chunk-level persistence. Edits the player makes to the world are lost. Settings do not persist to a per-user location. The product requirement is explicit: **the world saves to local app data**.

## 2. Objective

Durable, versioned, atomic persistence of worlds, players, and settings under the OS user-data directory (ADR-011), with a save/load round-trip that is bit-exact for world state.

## 3. Scope

**In scope:** save root layout, `level.json`, `player.dat`, region file format, chunk dirty tracking, autosave, atomic writes, schema versioning/migration, corruption recovery, settings persistence, save enumeration/deletion.

**Out of scope:** cloud saves, multiplayer state sync (16), asset packs (15).

## 4. Implementation Tasks

1. **Save root layout** under `voxels::Paths::UserDataDir()`:
   ```
   VoxelsEngine/
     settings.json
     logs/voxels_<timestamp>.log
     saves/<world_folder>/
       level.json
       player.dat
       regions/r.<rx>.<rz>.vrg
       screenshots/
   ```
   The world folder name is sanitized from the display name with a numeric suffix on collision; the display name lives in `level.json`.
2. **`level.json`:** `schemaVersion`, `displayName`, `seed`, `WorldOptions`, `createdUtc`, `lastPlayedUtc`, `playTimeSeconds`, `spawnPoint`, `generatorVersion`, `engineVersion`.
3. **`player.dat`** (binary, versioned): position, velocity, yaw/pitch, health, game mode, selected hotbar slot, full inventory contents.
4. **Region file format `.vrg`:** 32×32 chunk columns per file. Header (magic `VRG1`, version, sector-aligned offset/length table, per-chunk timestamp + CRC32), then per-chunk payloads using the existing RLE section encoder. Support in-place rewrite with a free-sector list, and compaction when fragmentation exceeds a threshold.
5. **Atomic writes:** every file is written to `<name>.tmp`, flushed, then `std::filesystem::rename`d over the target. A crash mid-save must leave the previous save intact. Never write directly over a live file.
6. **Dirty tracking:** `Chunk` carries a dirty flag set by any block edit or lighting change. Only dirty chunks are serialized. On unload, dirty chunks flush before the chunk is freed.
7. **Autosave:** configurable interval (default 120 s), executed on a job worker against a snapshot so the frame does not stall; toast notification on completion. Also save on pause-menu quit, on window close, and on state exit.
8. **Load path:** `level.json` restores seed + options into the generation pipeline so unmodified chunks regenerate identically instead of being stored; region hits decode; region misses generate. This keeps saves small and is why generation determinism (work item 12) is load-bearing.
9. **Schema versioning & migration:** every format carries a version. An older version runs a registered migration; a newer version is refused with a clear "this world was created by a newer build" message. A corrupt chunk is skipped with a warning and regenerated rather than crashing the game; a corrupt `level.json` routes to `ErrorScreen`.
10. **Settings persistence:** `Preferences` load/save `settings.json` in the user root, including the full key/gamepad binding map. Missing keys take defaults; unknown keys are preserved on rewrite so a downgrade does not destroy settings.
11. **Save management UI wiring:** `WorldSelectState` shows real sizes and timestamps; delete removes the folder only after typed confirmation and only inside the saves root (path-traversal guarded).
12. **Screenshots:** `F2` writes a PNG to the world's `screenshots/` folder with a toast — cheap, and it proves the render target readback path works.

## 5. Acceptance Criteria

* Build a structure, quit, relaunch, load the world — **the structure is exactly where it was**, along with player position, look direction, inventory, and hotbar selection.
* Killing the process mid-autosave leaves the prior save loadable.
* Deleting a region file causes only that area to regenerate; the rest of the world is intact.
* Settings survive restarts; deleting `settings.json` restores defaults without a crash.
* Saves live under `%LOCALAPPDATA%\VoxelsEngine\saves\` on Windows (or the platform equivalent) — verified by inspection.
* A 10-minute play session's save completes in well under a second and never stutters the frame.

## 6. Automated Tests

`tests/test_persistence.cpp`:
* `Region.ChunkRoundTripIsBitExact` — write a randomized chunk, read it back, byte-compare.
* `Region.MultipleChunksInOneRegionFileWithCorrectOffsets`.
* `Region.RewriteGrowsAndReusesSectorsWithoutCorruption`.
* `Region.CorruptChunkPayloadIsSkippedAndRegenerated` — CRC mismatch does not throw.
* `Save.AtomicWriteLeavesPriorFileIntactOnSimulatedFailure`.
* `Save.OnlyDirtyChunksAreSerialized`.
* `Save.PlayerStateRoundTripsIncludingInventory`.
* `Save.LevelJsonRoundTripsSeedAndAllWorldOptions`.
* `Save.UnmodifiedChunksAreNotStoredAndRegenerateIdentically` — the determinism dependency, asserted directly.
* `Schema.OlderVersionMigratesAndNewerVersionIsRefusedGracefully`.
* `Settings.PersistAcrossReloadAndPreserveUnknownKeys`.
* `SaveManager.EnumerateListsMetadataAndDeleteIsPathTraversalSafe` — a save named `../../evil` cannot escape the saves root.
* `Autosave.RunsOnIntervalWithoutBlockingTheSimulation`.

## 7. Anti-Shell Checks

- [ ] Saves are written to the real OS user-data directory, not the build folder.
- [ ] Autosave is driven by the running frame loop, not only by tests.
- [ ] No save path writes non-atomically.

## 8. Assets & Human Actions

None. Report the exact save path on the operator's OS so they can find and back up their worlds.

## 9. Verification

Build, test, launch, **build something, quit, relaunch, and confirm it is still there**. Report the save directory path, the file sizes, and the observed load time.
