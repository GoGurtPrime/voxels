# Work Item 07: Extensible World Generation Pipeline

## 🎯 Objective & Overview
Implement an extensible multi-phase procedural world generation pipeline capable of generating terrain deterministically based on seed inputs and user world configuration options (Seed, Peaceful, Permadeath, Always Sunny/Top-Noon, Visibility, Sandbox Mode). Implement safe spawn calculation to place the player safely on top of solid surface ground.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `06_world_voxel_core`
* **Target Subsystems:** `engine/include/voxels/world/generation_pipeline.hpp`, `engine/src/world/generation_pipeline.cpp`

---

## 📋 Detailed Task Breakdown
1. **World Options & Generation Context (`voxels/world/world_options.hpp`):**
   * Struct `WorldOptions`:
     * `uint64_t seed`
     * `bool peaceful`
     * `bool permadeath`
     * `bool alwaysSunny`
     * `bool sandboxMode`
     * `int renderDistanceChunks`
     * `int simulationDistanceChunks`

2. **Procedural Noise Engine (`voxels/world/noise.hpp`):**
   * Fast Simplex / Perlin 2D & 3D noise generator wrappers initialized with seed.
   * Multi-octave fractal noise support (FDm / Ridged Multifractal).

3. **Multi-Phase Generation Pipeline (`voxels/world/generation_pipeline.hpp`):**
   * Extensible pipeline interface `IGenerationPhase`.
   * **Phase 1: Shape/Terrain Phase (`TerrainShapePhase`):**
     * Computes surface heightmaps, bedrock layer, deep stone layer, top dirt/grass layer, and sea level water placement ($y \le 62$).
   * **Phase 2: Cave Generation Phase (`CavePhase`):**
     * 3D worm noise / perlin noise thresholding to carve out underground cave tunnels and coal ore veins inside stone layers.
   * **Phase 3: Vegetation & Feature Phase (`VegetationPhase`):**
     * Scans surface grass blocks and decorates terrain with trees (wood trunk pillars topped with leaf block canopy clusters).

4. **Safe Spawn Calculator (`voxels/world/spawn_calculator.hpp`):**
   * Calculates surface height at world origin $(0, z)$, verifies non-water ground, places player 2 blocks above surface.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_world_gen.cpp`
* **Test Cases:**
  * `WorldGen.Determinism`: Generate chunk $(0, 0)$ twice with seed `12345`, verify identical 3D block array output.
  * `WorldGen.SeedVariation`: Generate chunk $(0, 0)$ with seed `12345` and seed `54321`, verify block array outputs differ.
  * `WorldGen.CaveCarving`: Verify air blocks exist underground ($y < 40$) inside chunk bounds when cave phase is enabled.
  * `WorldGen.SafeSpawnFinding`: Execute safe spawn calculator on generated world, verify spawn point $(x, y, z)$ is above solid ground and not submerged in water or trapped inside solid rock.

---

## 👤 Human-in-the-Loop Actions Required
* **World Preset Configuration File (Human Step):**
  1. Create pre-configured world generation presets (e.g., `default_world.json`, `flat_sandbox.json`, `amplified.json`).
  2. Place files into `app/assets/config/presets/`.
  3. (Procedural Fallback: The pipeline defaults to `WorldOptions` struct field defaults if preset files are omitted).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R WorldGenTest`
3. **Self-Healing:** Ensure multi-threaded chunk generation phases use thread-safe noise sampling without shared state mutation race conditions.
