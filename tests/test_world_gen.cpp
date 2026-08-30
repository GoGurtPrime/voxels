/**
 * @file test_world_gen.cpp
 * @brief Automated regression tests for the procedural world generation pipeline.
 *
 * @details Covers determinism, seed variation, cave carving, and safe-spawn placement for the
 *          terrain generation pipeline and world spawn calculation logic.
 */

#include <catch2/catch_test_macros.hpp>

#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/spawn_calculator.hpp"
#include "voxels/world/world.hpp"

TEST_CASE("WorldGen.Determinism", "[world][generation]") {
    voxels::WorldOptions options{};
    options.seed = 12345u;
    options.renderDistanceChunks = 3;
    options.simulationDistanceChunks = 3;

    voxels::WorldGenerator generator(options);
    const voxels::Chunk chunkA = generator.GenerateChunk({0, 0, 0});
    const voxels::Chunk chunkB = generator.GenerateChunk({0, 0, 0});

    REQUIRE(chunkA.GetWidth() == chunkB.GetWidth());
    REQUIRE(chunkA.GetHeight() == chunkB.GetHeight());
    REQUIRE(chunkA.GetDepth() == chunkB.GetDepth());

    for (std::uint32_t z = 0; z < chunkA.GetDepth(); ++z) {
        for (std::uint32_t y = 0; y < chunkA.GetHeight(); ++y) {
            for (std::uint32_t x = 0; x < chunkA.GetWidth(); ++x) {
                REQUIRE(chunkA.GetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)) ==
                        chunkB.GetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)));
            }
        }
    }
}

TEST_CASE("WorldGen.SeedVariation", "[world][generation]") {
    voxels::WorldOptions a{};
    a.seed = 12345u;
    a.renderDistanceChunks = 3;
    a.simulationDistanceChunks = 3;

    voxels::WorldOptions b{};
    b.seed = 54321u;
    b.renderDistanceChunks = 3;
    b.simulationDistanceChunks = 3;

    voxels::WorldGenerator generatorA(a);
    voxels::WorldGenerator generatorB(b);

    const voxels::Chunk chunkA = generatorA.GenerateChunk({0, 0, 0});
    const voxels::Chunk chunkB = generatorB.GenerateChunk({0, 0, 0});

    bool different = false;
    for (std::uint32_t z = 0; z < chunkA.GetDepth(); ++z) {
        for (std::uint32_t y = 0; y < chunkA.GetHeight(); ++y) {
            for (std::uint32_t x = 0; x < chunkA.GetWidth(); ++x) {
                if (chunkA.GetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)) !=
                    chunkB.GetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z))) {
                    different = true;
                    break;
                }
            }
            if (different) {
                break;
            }
        }
        if (different) {
            break;
        }
    }

    REQUIRE(different);
}

TEST_CASE("WorldGen.CaveCarving", "[world][generation]") {
    voxels::WorldOptions options{};
    options.seed = 12345u;
    options.renderDistanceChunks = 3;
    options.simulationDistanceChunks = 3;
    options.sandboxMode = false;

    voxels::WorldGenerator generator(options);
    const voxels::Chunk chunk = generator.GenerateChunk({0, 0, 0});

    bool foundCaveAir = false;
    for (std::uint32_t z = 0; z < chunk.GetDepth(); ++z) {
        for (std::uint32_t y = 0; y < 40u; ++y) {
            for (std::uint32_t x = 0; x < chunk.GetWidth(); ++x) {
                if (chunk.GetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)) ==
                    static_cast<voxels::BlockId>(voxels::BlockType::Air)) {
                    foundCaveAir = true;
                    break;
                }
            }
            if (foundCaveAir) {
                break;
            }
        }
        if (foundCaveAir) {
            break;
        }
    }

    REQUIRE(foundCaveAir);
}

TEST_CASE("WorldGen.SafeSpawnFinding", "[world][generation]") {
    voxels::WorldOptions options{};
    options.seed = 12345u;
    options.renderDistanceChunks = 3;
    options.simulationDistanceChunks = 3;

    voxels::WorldGenerator generator(options);
    const voxels::Chunk chunk = generator.GenerateChunk({0, 0, 0});

    const voxels::Vec3I spawn = voxels::FindSafeSpawn(chunk, 0, 0);

    INFO("spawn = (" << spawn.x << ", " << spawn.y << ", " << spawn.z << ")");
    REQUIRE(spawn.x >= 0);
    REQUIRE(spawn.x < static_cast<int>(chunk.GetWidth()));
    REQUIRE(spawn.z >= 0);
    REQUIRE(spawn.z < static_cast<int>(chunk.GetDepth()));
    REQUIRE(spawn.y > 0);
    REQUIRE(chunk.GetBlock(spawn.x, spawn.y, spawn.z) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
    REQUIRE(chunk.GetBlock(spawn.x, spawn.y - 1, spawn.z) != static_cast<voxels::BlockId>(voxels::BlockType::Air));
    REQUIRE(chunk.GetBlock(spawn.x, spawn.y - 1, spawn.z) != static_cast<voxels::BlockId>(voxels::BlockType::Water));
}

TEST_CASE("WorldGen.SpawnIsDeterministicAndUsesChunkOrigin", "[world][generation]") {
    voxels::WorldGenerator generator({.seed = 981723u});
    const voxels::Chunk first = generator.GenerateChunk({3, 1, -2});
    const voxels::Chunk second = generator.GenerateChunk({3, 1, -2});

    const voxels::Vec3I spawnA = voxels::FindSafeSpawn(first, 48, -32);
    const voxels::Vec3I spawnB = voxels::FindSafeSpawn(second, 48, -32);
    REQUIRE(spawnA == spawnB);
    REQUIRE(spawnA.x >= 48);
    REQUIRE(spawnA.x < 64);
    REQUIRE(spawnA.z >= -32);
    REQUIRE(spawnA.z < -16);
}

TEST_CASE("WorldGen.SafeWorldSpawnHasFlatSurfaceAndClearance", "[world][generation]") {
    voxels::WorldOptions options{.seed = 20260830u};
    voxels::World first;
    voxels::World second;
    first.Initialize(options);
    second.Initialize(options);
    voxels::WorldGenerator generator(options);
    for (int z = -2; z <= 2; ++z) {
        for (int x = -2; x <= 2; ++x) {
            for (int y = 0; y <= 3; ++y) {
                first.GetOrCreateChunk({x, y, z}) = generator.GenerateChunk({x, y, z});
                second.GetOrCreateChunk({x, y, z}) = generator.GenerateChunk({x, y, z});
            }
        }
    }
    const voxels::Vec3I spawnA = voxels::FindSafeSpawn(first);
    const voxels::Vec3I spawnB = voxels::FindSafeSpawn(second);
    const voxels::Vec3 playerCenter{static_cast<float>(spawnA.x) + 0.5f, static_cast<float>(spawnA.y) + 1.9f,
                                    static_cast<float>(spawnA.z) + 0.5f};
    REQUIRE(spawnA == spawnB);
    REQUIRE(voxels::IsSafePlayerSpawn(first, playerCenter));
    for (int z = -1; z <= 1; ++z) {
        for (int x = -1; x <= 1; ++x) {
            REQUIRE(first.GetBlock({spawnA.x + x, spawnA.y, spawnA.z + z}) != static_cast<voxels::BlockId>(voxels::BlockType::Air));
            REQUIRE(first.GetBlock({spawnA.x + x, spawnA.y + 1, spawnA.z + z}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
            REQUIRE(first.GetBlock({spawnA.x + x, spawnA.y + 2, spawnA.z + z}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
        }
    }
}

TEST_CASE("WorldGen.InitialPlayableCapIsNotFlooded", "[world][generation][water]") {
    voxels::WorldOptions options{};
    options.seed = 12345u;
    voxels::WorldGenerator generator(options);
    const voxels::Chunk upperChunk = generator.GenerateChunk({0, 2, 0});

    for (std::uint32_t z = 0; z < upperChunk.GetDepth(); ++z) {
        for (std::uint32_t x = 0; x < upperChunk.GetWidth(); ++x) {
            REQUIRE(upperChunk.GetBlock(static_cast<int>(x), 15, static_cast<int>(z)) ==
                    static_cast<voxels::BlockId>(voxels::BlockType::Air));
        }
    }
}
