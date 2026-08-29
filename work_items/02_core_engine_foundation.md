# Work Item 02: Core Engine Foundation & Preferences

## 🎯 Objective & Overview
Implement the core utility infrastructure of the engine: logging system, configuration/preferences serialization (supporting JSON/INI file loading & saving), platform-aware preferences abstraction, memory pools/allocators, and GLM-based math utility wrappers.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `01_testing_and_build_infrastructure`
* **Target Subsystems:** `engine/include/voxels/core/`, `engine/src/core/`

---

## 📋 Detailed Task Breakdown
1. **Thread-Safe Logging System (`voxels/core/logger.hpp`):**
   * Multi-level logger (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`).
   * Supports console output (with platform color codes) and file logging (`engine.log`).
   * Asynchronous or mutex-guarded thread-safe queue.

2. **Game Preferences & Config Serialization (`voxels/core/preferences.hpp`):**
   * Implement `GamePreferences` struct with settings:
     * Window mode (Windowed, Borderless, Fullscreen).
     * Resolution (width, height, refresh rate).
     * Quality settings (Render distance, simulation distance, FOV, anti-aliasing, shadow quality).
     * Audio volumes (Master, Music, SFX).
     * Keybindings map.
   * Platform-awareness: Enforce constraints (e.g., Dreamcast platform target disables window/fullscreen toggling and locks resolution to 640x480).
   * Save and load settings to `config.json` or `config.ini` in user data directory.

3. **Math & Geometry Helpers (`voxels/core/math.hpp`):**
   * Wrapper around GLM types (`glm::vec3`, `glm::ivec3`, `glm::mat4`, `glm::quat`).
   * Specialized coordinate utility functions for voxel grid alignment, chunk coordinate calculations (`worldPosToChunkPos`, `worldPosToLocalBlockPos`), and bounding box intersections (AABB).

4. **Memory Allocation Stubs (`voxels/core/memory.hpp`):**
   * Block allocator / pool allocator interfaces for high-frequency chunk and block allocations.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_core.cpp`
* **Test Cases:**
  * `Logger.LevelFiltering`: Verify logs below current threshold are omitted.
  * `Preferences.SaveAndLoad`: Verify preferences serialize to disk and deserialize with equality.
  * `Preferences.PlatformConstraints`: Verify setting fullscreen on Dreamcast target forces windowed/fixed config.
  * `Math.ChunkCoordinateTransform`: Test negative and positive floating-point world positions map accurately to chunk coordinates and local block indices (0-15).
  * `Memory.PoolAllocator`: Verify fixed-size block pool allocation and deallocation.

---

## 👤 Human-in-the-Loop Actions Required
* **Default Configuration Override (Optional):** If custom default keybindings or branding settings are desired, place a default `default_config.json` into `app/assets/config/`.
* **Step-by-step instructions:**
  1. Create directory `app/assets/config/` if it does not exist.
  2. Optionally add `default_config.json` with JSON structure matching `GamePreferences`.

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R CoreTest`
3. **Self-Healing:** Ensure zero memory leaks or file I/O exceptions. Fix any rounding bugs in coordinate transformation math.
