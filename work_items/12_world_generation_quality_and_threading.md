# Work Item 12 — World Generation Quality & Threaded Pipeline

**Phase:** D — Depth & Polish · **Prerequisites:** 11 (MVP gate passed)

---

## 1. Problem Statement

Generation works and is deterministic, but the output is thin: a single-noise heightmap, threshold caves that produce disconnected blobs rather than explorable tunnels, no ore distribution, no biome variation, no water table beyond a flat sea level, and no beaches or surface variety. Generation also runs synchronously and has never been proven deterministic under parallel execution.

## 2. Objective

A world worth exploring — varied terrain, connected cave systems, sensible ore placement, and lakes/beaches — generated on worker threads with byte-for-byte determinism regardless of thread count or chunk order.

## 3. Scope

**In scope:** `engine/worldgen/`, noise upgrades, phase set, biome-lite system, structure placement, threaded execution and determinism guarantees, generator versioning.

**Out of scope:** mobs, dimensions, structures beyond trees and small features.

## 4. Implementation Tasks

1. **Noise upgrades:** Simplex/OpenSimplex in addition to Perlin, domain warping, ridged multifractal for mountains, and a fast 2D Voronoi/cellular for biome cells. All seeded from a single `u64` world seed via well-mixed sub-seeds (`hash(seed, PHASE_ID, chunkX, chunkY, chunkZ)`), never from a shared mutable RNG.
2. **Biome-lite system:** temperature + humidity noise fields select a biome per column — `plains`, `forest`, `hills`, `mountains`, `desert`, `beach`, `ocean`. A biome supplies surface/filler blocks, height parameters, tree density/type, grass tint, and ambience id. Biome boundaries are **blended** over several blocks so terrain does not step.
3. **Terrain shape:** continentalness + erosion + peaks-and-valleys style layered noise producing plains, rolling hills, and genuine mountains; a global sea level with water fill, sand beaches at the waterline, and dirt/grass elsewhere. Bedrock floor at `y = 0` so the player cannot fall out of the world.
4. **Cave systems:** replace threshold blobs with **worm/tunnel caves** — seeded per-region cave starts that walk a noise-perturbed path with varying radius, plus a sparse large-cavern pass from 3D ridged noise. Caves must connect, must reach the surface occasionally, and must not flood the ocean (no carving into a water column from below).
5. **Ore pass:** per-ore depth bands, vein sizes, and rarity from data (`app/assets/data/ores.json`) — coal shallow and common, iron mid, precious deep and rare. Vein shapes are small blobs, not single blocks.
6. **Vegetation:** deterministic per-column Poisson-ish placement of biome-appropriate trees with 2–3 trunk/canopy variants, plus grass/flowers as `cross`-render blocks. Trees must not generate half-buried or floating and must handle chunk-boundary overhang correctly (deferred block writes queued into neighbouring chunks).
7. **Lighting phase:** skylight column propagation and block-light flood fill, with correct propagation across chunk boundaries and re-propagation on block edits (a torch or a dug tunnel updates lighting locally, not by re-meshing the world).
8. **Threaded, deterministic execution:** phases run on the `JobSystem`, chunk-parallel. Cross-chunk writes (tree overhang, cave tunnels, light spill) go through a deterministic deferred-write queue that is applied in a sorted, order-independent way. **Determinism must hold with 1 worker and with 16.**
9. **Generator versioning:** `generatorVersion` in `level.json`. Changing generation behavior bumps the version; existing worlds keep their original generator so a player's world does not change shape under them.
10. **Spawn quality:** spawn on a habitable biome surface (not ocean, not inside a mountain), on solid non-liquid ground, with clearance — searched deterministically outward from origin.
11. **Tuning harness:** `voxels_app --gen-preview --seed=<n> --out=<file.png>` renders a top-down colored heightmap/biome map of an N×N chunk area to a PNG so terrain can be evaluated without playing. Invaluable for tuning and for the operator to review.

## 5. Acceptance Criteria

* Flying/walking across a generated world shows recognizably different biomes with blended transitions, mountains that read as mountains, oceans with beaches, and no floating or half-buried trees.
* Digging down reliably finds connected, explorable cave systems and depth-appropriate ore veins.
* The same seed produces an identical world on every run and on every thread count — verified, not assumed.
* Generation of the spawn neighbourhood completes fast enough that the loading screen is short, and in-flight generation never stalls a frame.
* `--gen-preview` produces a legible map image.

## 6. Automated Tests

`tests/test_world_generation.cpp`:
* `Gen.SameSeedProducesByteIdenticalChunks` — across 50 seeds and 50 chunk coordinates.
* `Gen.DeterminismHoldsAcrossWorkerCounts` — generate the same region with 1, 2, 8, and 16 workers; byte-compare.
* `Gen.DeterminismIsIndependentOfChunkGenerationOrder` — shuffled order, identical result.
* `Gen.DifferentSeedsProduceDifferentTerrain` — statistical divergence assertion.
* `Gen.BiomeSelectionIsStableAndBlendedAtBoundaries` — no single-column biome flicker.
* `Gen.NoFloatingOrHalfBuriedTrees` — sample many columns, assert trunk base sits on solid ground.
* `Gen.TreesCrossingChunkBoundariesAreCompleteInBothChunks`.
* `Gen.CavesAreConnectedAndReachableFromTheSurface` — flood-fill connectivity over a sampled region.
* `Gen.CavesDoNotBreachOceanFloor` — no air pocket directly beneath a water column.
* `Gen.OreDistributionMatchesConfiguredDepthBandsAndRarity` — statistical bounds per ore.
* `Gen.BedrockFloorIsUnbreakableAndContinuous`.
* `Gen.SkylightPropagatesCorrectlyAcrossChunkBoundaries`.
* `Gen.BlockEditTriggersLocalLightRepropagationOnly` — asserts the bounded update region.
* `Gen.SpawnIsOnHabitableSurfaceAcrossManySeeds`.

## 7. Anti-Shell Checks

- [ ] Every phase mutates real chunk data and is registered in the running pipeline.
- [ ] No phase is data-driven in name only (config must actually change output).
- [ ] Deferred cross-chunk writes are applied, not dropped at boundaries.

## 8. Assets & Human Actions

* `app/assets/data/ores.json` and biome parameter data are authored by the agent.
* Offer the operator `--gen-preview` images for a few seeds so they can direct terrain feel (flatter, more mountainous, more caves).

## 9. Verification

Build, test, launch, explore, and report: biomes seen, cave systems found, ores found at which depths, and attach/describe a `--gen-preview` render.
