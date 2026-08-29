# Work Item 01: Testing and Build Infrastructure

## 🎯 Objective & Overview
Establish the foundational automated testing environment for `VoxelsEngine` by integrating Catch2 (v3) via CMake `FetchContent` / `find_package` and setting up CTest in the root CMake pipeline. Create a dedicated `tests/` directory with initial unit test suites to validate build output and contract stability.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `00_git_and_repository_hygiene`
* **Target Subsystems:** Root `CMakeLists.txt`, `engine/CMakeLists.txt`, `tests/` directory.

---

## 📋 Detailed Task Breakdown
1. **Update Root `CMakeLists.txt`:**
   * Enable CTest testing via `include(CTest)` and `enable_testing()`.
   * Add a `VOXELS_BUILD_TESTS` option (default `ON`).
   * Add Catch2 (v3) dependency discovery using `FetchContent` as fallback if `find_package(Catch2 3 QUIET)` fails:
     ```cmake
     include(FetchContent)
     FetchContent_Declare(
       Catch2
       GIT_REPOSITORY https://github.com/catchorg/Catch2.git
       GIT_TAG        v3.5.2
     )
     FetchContent_MakeAvailable(Catch2)
     ```
   * Include the `tests/` directory conditionally when `VOXELS_BUILD_TESTS` is enabled.

2. **Create `tests/` Test Architecture:**
   * Create `tests/CMakeLists.txt`.
   * Create `tests/main.cpp` providing the Catch2 test runner entry point.
   * Create initial smoke tests in `tests/test_sanity.cpp` to test engine instantiation and basic math stubs.

3. **Verify Cross-Platform CTest Targets:**
   * Ensure `ctest --test-dir build --output-on-failure` executes cleanly.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_sanity.cpp`
* **Test Cases:**
  * `SanityCheck.EngineInitialization`: Instantiates `voxels::Engine` and verifies status / version.
  * `SanityCheck.CMakeConfiguration`: Validates conditional macros (`VOXELS_ENABLE_VULKAN`, etc.).

---

## 👤 Human-in-the-Loop Actions Required
* **Network Access for Build System:** Ensure internet connectivity for `FetchContent` to download Catch2 during the initial CMake configure step if Catch2 is not installed locally.
* *No media or binary assets required for this work item.*

---

## 🔄 Verification & Self-Healing Protocol
1. **Configure:** Run `cmake -S . -B build -DVOXELS_BUILD_TESTS=ON`
2. **Build:** Run `cmake --build build`
3. **Test:** Run `ctest --test-dir build --output-on-failure`
4. **Self-Healing:** If build fails or Catch2 download fails, refine `CMakeLists.txt` `FetchContent` configuration until CTest passes with 100% success.
