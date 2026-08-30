/**
 * @file test_world.cpp
 * @brief Automated tests for Work Item 05: block registry, chunk storage/serialization,
 *        world raycasting, and greedy meshing face culling.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voxels/world/block.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/geometry.hpp"
#include "voxels/world/world.hpp"

TEST_CASE("BlockRegistry.RegistrationAndLookup", "[world][block]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();

    REQUIRE(registry.Count() >= 7);

    const auto* stoneById = registry.GetDefinition(static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    REQUIRE(stoneById != nullptr);
    REQUIRE(stoneById->name == "stone");
    REQUIRE(stoneById->isSolid);
    REQUIRE_FALSE(stoneById->isTransparent);

    const auto* stoneByName = registry.GetDefinition("stone");
    REQUIRE(stoneByName != nullptr);
    REQUIRE(stoneByName->id == static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    const auto* water = registry.GetDefinition("water");
    REQUIRE(water != nullptr);
    REQUIRE_FALSE(water->isSolid);
    REQUIRE(water->isTransparent);

    REQUIRE(registry.IsRegistered(static_cast<voxels::BlockId>(voxels::BlockType::Leaf)));
    REQUIRE_FALSE(registry.IsRegistered(static_cast<voxels::BlockId>(1000)));
    REQUIRE(registry.GetDefinition("does_not_exist") == nullptr);

    // Unique id mapping: every registered block resolves back to a distinct id.
    for (const auto* name : {"air", "stone", "dirt", "coal_ore", "water", "wood_log", "leaves"}) {
        const auto* definition = registry.GetDefinition(name);
        REQUIRE(definition != nullptr);
        REQUIRE(registry.GetDefinition(definition->id)->name == name);
    }
}

TEST_CASE("Chunk.SetAndGetBlock", "[world][chunk]") {
    voxels::Chunk chunk(voxels::ChunkCoordinate{0, 0, 0});

    REQUIRE(chunk.GetBlock(5, 5, 5) == static_cast<voxels::BlockId>(voxels::BlockType::Air));

    REQUIRE(chunk.SetBlock(5, 5, 5, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    REQUIRE(chunk.GetBlock(5, 5, 5) == static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    // Neighboring voxels remain untouched.
    REQUIRE(chunk.GetBlock(5, 5, 6) == static_cast<voxels::BlockId>(voxels::BlockType::Air));

    SECTION("Out-of-bounds reads and writes are safe") {
        REQUIRE(chunk.GetBlock(-1, 0, 0) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
        REQUIRE(chunk.GetBlock(16, 0, 0) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
        REQUIRE_FALSE(chunk.SetBlock(-1, 0, 0, static_cast<voxels::BlockId>(voxels::BlockType::Dirt)));
        REQUIRE_FALSE(chunk.SetBlock(0, 16, 0, static_cast<voxels::BlockId>(voxels::BlockType::Dirt)));
        REQUIRE_FALSE(chunk.InBounds(0, 0, 100));
    }

    SECTION("Light maps are independently addressable") {
        REQUIRE(chunk.SetBlockLight(1, 2, 3, 12));
        REQUIRE(chunk.GetBlockLight(1, 2, 3) == 12);
        REQUIRE(chunk.SetSkyLight(1, 2, 3, 15));
        REQUIRE(chunk.GetSkyLight(1, 2, 3) == 15);
        REQUIRE(chunk.GetBlockLight(0, 0, 0) == 0);
    }
}

TEST_CASE("Chunk.SerializationRLE", "[world][chunk]") {
    voxels::Chunk chunk(voxels::ChunkCoordinate{2, 0, -3});

    for (int z = 0; z < 16; ++z) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                voxels::BlockId value = static_cast<voxels::BlockId>(voxels::BlockType::Air);
                if (y < 4) {
                    value = static_cast<voxels::BlockId>(voxels::BlockType::Stone);
                } else if (y == 4) {
                    value = static_cast<voxels::BlockId>(voxels::BlockType::Dirt);
                }
                chunk.SetBlock(x, y, z, value);
            }
        }
    }
    chunk.SetBlock(7, 10, 7, static_cast<voxels::BlockId>(voxels::BlockType::Coal));

    const std::vector<std::uint8_t> compressed = chunk.SerializeRLE();
    REQUIRE_FALSE(compressed.empty());
    // Layered fill plus a single outlier should compress far below the raw voxel count.
    REQUIRE(compressed.size() < (16u * 16u * 16u * sizeof(voxels::BlockId)));

    const voxels::Chunk restored =
        voxels::Chunk::DeserializeRLE(compressed, voxels::ChunkCoordinate{2, 0, -3});

    for (int z = 0; z < 16; ++z) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                REQUIRE(restored.GetBlock(x, y, z) == chunk.GetBlock(x, y, z));
            }
        }
    }
}

TEST_CASE("World.RaycastVoxelTraversal", "[world][raycast]") {
    voxels::World world;
    world.SetBlock(voxels::Vec3I{0, 8, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    const voxels::RaycastHit hit =
        world.Raycast(voxels::Vec3{0.5f, 10.5f, 0.5f}, voxels::Vec3{0.0f, -1.0f, 0.0f}, 20.0f);

    REQUIRE(hit.hit);
    REQUIRE(hit.blockPosition == voxels::Vec3I{0, 8, 0});
    REQUIRE(hit.face == voxels::Face::PosY);
    REQUIRE(hit.distance == Catch::Approx(1.5f));

    SECTION("Misses when no block is within range") {
        const voxels::RaycastHit miss =
            world.Raycast(voxels::Vec3{5.5f, 10.5f, 5.5f}, voxels::Vec3{0.0f, -1.0f, 0.0f}, 5.0f);
        REQUIRE_FALSE(miss.hit);
    }
}

TEST_CASE("World.BlockAccessAcrossChunkBoundaries", "[world][chunk]") {
    voxels::World world;

    REQUIRE(world.SetBlock(voxels::Vec3I{-1, -1, -1}, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    REQUIRE(world.GetBlock(voxels::Vec3I{-1, -1, -1}) == static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    REQUIRE(world.GetBlock(voxels::Vec3I{0, 0, 0}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
    REQUIRE(world.LoadedChunkCount() == 1);
}

TEST_CASE("World.SkylightRebuildUpdatesTheEditedColumnAcrossSections", "[world][lighting]") {
    voxels::World world;
    const auto stone = static_cast<voxels::BlockId>(voxels::BlockType::Stone);
    const auto air = static_cast<voxels::BlockId>(voxels::BlockType::Air);
    REQUIRE(world.SetBlock({15, 8, 0}, stone));
    REQUIRE(world.SetBlock({15, 24, 0}, air));
    REQUIRE(world.RebuildSkyLightAround({15, 7, 0}) > 0);
    REQUIRE(world.GetChunks().at({0, 0, 0})->GetSkyLight(15, 7, 0) == 0);
    REQUIRE(world.GetChunks().at({0, 1, 0})->GetSkyLight(15, 8, 0) == 15);

    REQUIRE(world.SetBlock({15, 8, 0}, air));
    const std::size_t openedTouched = world.RebuildSkyLightAround({15, 7, 0});
    REQUIRE(openedTouched > 0);
    REQUIRE(world.GetChunks().at({0, 0, 0})->GetSkyLight(15, 7, 0) == 15);
    REQUIRE(openedTouched < 300);

    REQUIRE(world.SetBlock({15, 8, 0}, stone));
    REQUIRE(world.RebuildSkyLightAround({15, 7, 0}) > 0);
    REQUIRE(world.GetChunks().at({0, 0, 0})->GetSkyLight(15, 7, 0) == 0);
}

TEST_CASE("GreedyMeshing.FaceCulling", "[world][geometry]") {
    constexpr int kSize = 4;
    std::vector<std::uint8_t> grid(static_cast<std::size_t>(kSize) * kSize * kSize, 1);

    const std::vector<voxels::MeshQuad> quads = voxels::GreedyMeshFaces(grid, kSize, kSize, kSize);

    // A fully solid cube has no interior faces; only the six outer faces survive, each
    // merged into a single 4x4 quad.
    REQUIRE(quads.size() == 6);
    for (const auto& quad : quads) {
        REQUIRE(quad.width == kSize);
        REQUIRE(quad.height == kSize);
    }

    SECTION("A single carved-out voxel exposes new interior faces") {
        grid[(1 * kSize + 1) * kSize + 1] = 0; // hollow out voxel (1,1,1)
        const std::vector<voxels::MeshQuad> quadsWithHole = voxels::GreedyMeshFaces(grid, kSize, kSize, kSize);
        REQUIRE(quadsWithHole.size() > quads.size());
    }
}
