# Work Item 06: World & Voxel Core Data Structures

## 🎯 Objective & Overview
Implement the core block/item data structures, registry systems, sub-voxel geometry representations, chunk serialization/compression, and chunk streaming algorithms optimized for low-memory targets like the Sega Dreamcast (16MB RAM) as well as modern PC systems.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `02_core_engine_foundation`, `05_graphics_renderer_abstraction`
* **Target Subsystems:** `engine/include/voxels/world/`, `engine/src/world/`

---

## 📋 Detailed Task Breakdown
1. **Block & Item Registry (`voxels/world/block.hpp`, `voxels/world/item.hpp`):**
   * Block ID type (`uint16_t BlockId`) and custom state bitfields (light level, orientation, metadata).
   * Core block types: `Air` (0), `Stone` (1), `Dirt` (2), `Coal` (3), `Water` (4), `Tree Trunk` (5), `Leaf` (6).
   * Extend block definition to support custom state and logic callbacks (e.g. liquid flow, decay, redstone-like signals).
   * Items vs. Blocks abstraction: Blocks placeable in world, items consumable or craftable with state.

2. **Sub-Voxel Geometry System (`voxels/world/geometry.hpp`):**
   * Support non-full block geometries (stairs, slabs, fences, doors, custom sub-voxel models).
   * Sub-voxel collision shapes (AABB arrays inside a single $1\times1\times1$ block unit).
   * Mesh face culling algorithms (greedy meshing & occlusion culling between adjacent sub-voxels and neighbor chunks).

3. **Chunk Structure & Serialization (`voxels/world/chunk.hpp`):**
   * Chunk dimensions: $16 \times 16 \times 256$ (or $16 \times 16 \times 16$ sub-chunks for memory-constrained platforms).
   * Run-Length Encoding (RLE) or zlib/zstd compression for chunk serialization to disk/memory.
   * Internal light map buffers (block light and sky light).

4. **World Grid & Coordinate Space (`voxels/world/world.hpp`):**
   * Sparse spatial hash map of active loaded chunks (`std::unordered_map<ChunkPos, std::shared_ptr<Chunk>>`).
   * Raycasting & Voxel Traversal algorithm (Fast Voxel Traversal / Amanatides-Woo algorithm) for block picking and sub-voxel collision detection.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_world.cpp`
* **Test Cases:**
  * `BlockRegistry.RegistrationAndLookup`: Register block types, verify unique ID mapping and property lookup.
  * `Chunk.SetAndGetBlock`: Set blocks in a chunk at local coords $(x,y,z)$, read back state, test out-of-bounds safety.
  * `Chunk.SerializationRLE`: Compress chunk data with RLE, deserialize into new chunk, verify 100% block and metadata match.
  * `World.RaycastVoxelTraversal`: Cast ray from $(0.5, 10.5, 0.5)$ along vector $(0, -1, 0)$, verify hit detection on first solid block face.
  * `GreedyMeshing.FaceCulling`: Construct a $4\times4\times4$ solid block cube, verify interior faces are culled and outer geometry forms merged quads.

---

## 👤 Human-in-the-Loop Actions Required
* **Block Texture Atlas Sheet (Human Step):**
  1. Create or obtain a $256 \times 256$ PNG texture atlas containing $16 \times 16$ pixel tile textures for block faces:
     * `stone.png`, `dirt.png`, `coal_ore.png`, `water.png`, `tree_trunk_side.png`, `tree_trunk_top.png`, `leaf.png`.
  2. Save file as `app/assets/textures/block_atlas.png`.
  3. (Procedural Fallback: Engine texture loader will programmatically draw colored checkerboard patterns into a memory buffer if `block_atlas.png` is absent).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R WorldTest`
3. **Self-Healing:** Ensure zero memory allocations occur inside inner greedy meshing loops to maintain strict frame budgeting.
