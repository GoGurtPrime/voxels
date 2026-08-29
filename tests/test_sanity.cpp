/**
 * @file test_sanity.cpp
 * @brief Smoke tests and sanity verification for engine lifecycle and build configuration.
 * 
 * @details Validates core engine instantiation, status transitions, version semantics,
 *          CMake preprocessor macro definitions, and foundational data structures.
 */

#include <catch2/catch_test_macros.hpp>
#include "voxels/engine.hpp"
#include "voxels/world/geometry.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/core/game_types.hpp"

TEST_CASE("SanityCheck.EngineInitialization", "[sanity][engine]") {
    voxels::Engine engine;

    // Check initial status before initialization
    REQUIRE_FALSE(engine.isInitialized());
    REQUIRE(engine.getStatus() == voxels::EngineStatus::Uninitialized);
    REQUIRE(engine.getVersion() == "0.1.0");

    // Initialize engine
    REQUIRE(engine.initialize());
    REQUIRE(engine.isInitialized());
    REQUIRE(engine.getStatus() == voxels::EngineStatus::Initialized);

    // Shutdown engine
    engine.shutdown();
    REQUIRE_FALSE(engine.isInitialized());
    REQUIRE(engine.getStatus() == voxels::EngineStatus::Stopped);
}

TEST_CASE("SanityCheck.CMakeConfiguration", "[sanity][config]") {
    // Verify compile-time macros configured by root CMake pipeline
#if defined(VOXELS_ENABLE_VULKAN)
    REQUIRE(VOXELS_ENABLE_VULKAN == 1);
#endif

#if defined(VOXELS_ENABLE_DX12)
    REQUIRE(VOXELS_ENABLE_DX12 == 1);
#endif

#if defined(VOXELS_ENABLE_METAL)
    REQUIRE(VOXELS_ENABLE_METAL == 1);
#endif

#if defined(VOXELS_ENABLE_DREAMCAST)
    REQUIRE(VOXELS_ENABLE_DREAMCAST == 1);
#endif

    // Verify engine version constants
    REQUIRE(voxels::Engine::kVersionMajor == 0);
    REQUIRE(voxels::Engine::kVersionMinor == 1);
    REQUIRE(voxels::Engine::kVersionPatch == 0);
    REQUIRE(voxels::Engine::kVersionString == "0.1.0");
}

TEST_CASE("SanityCheck.CoreDataStructures", "[sanity][core]") {
    SECTION("Geometry and BoundingBox") {
        voxels::Vec3I minCorner{0, 0, 0};
        voxels::Vec3I maxCorner{16, 16, 16};
        voxels::BoundingBox box{minCorner, maxCorner};

        REQUIRE(box.min.x == 0);
        REQUIRE(box.min.y == 0);
        REQUIRE(box.min.z == 0);
        REQUIRE(box.max.x == 16);
        REQUIRE(box.max.y == 16);
        REQUIRE(box.max.z == 16);

        voxels::GeometryVoxel voxel{minCorner, 1, true};
        REQUIRE(voxel.size == 1);
        REQUIRE(voxel.fullBlock == true);
    }

    SECTION("ChunkCoordinate and ChunkData") {
        voxels::ChunkData chunk;
        REQUIRE(chunk.width == 16);
        REQUIRE(chunk.height == 16);
        REQUIRE(chunk.depth == 16);
        REQUIRE_FALSE(chunk.dirty);

        chunk.coordinate = voxels::ChunkCoordinate{1, 0, -1};
        REQUIRE(chunk.coordinate.x == 1);
        REQUIRE(chunk.coordinate.y == 0);
        REQUIRE(chunk.coordinate.z == -1);
    }
}
