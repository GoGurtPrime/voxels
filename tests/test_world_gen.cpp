/**
 * @file test_world_gen.cpp
 * @brief Automated regression tests for the procedural world generation pipeline.
 *
 * @details Covers determinism, seed variation, cave carving, and safe-spawn placement for the
 *          terrain generation pipeline and world spawn calculation logic.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <future>
#include <unordered_map>

#include "voxels/core/job_system.hpp"
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

TEST_CASE("WorldGen.ComposedWorldSpawnIsAlwaysDryAcrossSeeds", "[world][generation][spawn]") {
    // Reproduces the full generation-to-player-spawn path for several seeds (including known
    // ocean-heavy ones) and asserts every composed world yields dry, player-clear support -
    // never a fallback position floating over water.
    constexpr std::array<std::uint64_t, 4> kSeeds = {20260830u, 7u, 424242u, 99999999u};
    for (const std::uint64_t seed : kSeeds) {
        voxels::WorldOptions options{.seed = seed};
        voxels::World world;
        world.Initialize(options);
        voxels::WorldGenerator generator(options);
        for (int z = -3; z <= 3; ++z) {
            for (int x = -3; x <= 3; ++x) {
                for (int y = 0; y <= 3; ++y) {
                    world.GetOrCreateChunk({x, y, z}) = generator.GenerateChunk({x, y, z});
                }
            }
        }

        const auto found = voxels::TryFindSafeSpawn(world);
        REQUIRE(found.has_value());
        const voxels::Vec3I spawn = *found;
        const voxels::Vec3 playerCenter{static_cast<float>(spawn.x) + 0.5f, static_cast<float>(spawn.y) + 1.9f,
                                        static_cast<float>(spawn.z) + 0.5f};
        INFO("seed = " << seed << " spawn = (" << spawn.x << ", " << spawn.y << ", " << spawn.z << ")");
        REQUIRE(voxels::IsSafePlayerSpawn(world, playerCenter));
        REQUIRE(world.GetBlock({spawn.x, spawn.y - 1, spawn.z}) != static_cast<voxels::BlockId>(voxels::BlockType::Air));
        REQUIRE(world.GetBlock({spawn.x, spawn.y - 1, spawn.z}) != static_cast<voxels::BlockId>(voxels::BlockType::Water));
        REQUIRE(voxels::FindSafeSpawn(world) == spawn);
    }
}

TEST_CASE("WorldGen.SpawnSearchNeverFallsBackOverWaterWhenAreaIsAllOcean", "[world][generation][spawn]") {
    // A world where every loaded column is deep water (no terrain support anywhere) must be
    // reported as a failed search, not silently answered with an unsafe sky-over-water position.
    voxels::WorldOptions options{.seed = 1u};
    voxels::World world;
    world.Initialize(options);
    for (int z = -2; z <= 2; ++z) {
        for (int x = -2; x <= 2; ++x) {
            for (int y = 0; y <= 1; ++y) {
                voxels::Chunk chunk = world.GetOrCreateChunk({x, y, z});
                for (std::uint32_t lz = 0; lz < chunk.GetDepth(); ++lz) {
                    for (std::uint32_t lx = 0; lx < chunk.GetWidth(); ++lx) {
                        for (std::uint32_t ly = 0; ly < chunk.GetHeight(); ++ly) {
                            chunk.SetBlock(static_cast<int>(lx), static_cast<int>(ly), static_cast<int>(lz),
                                          static_cast<voxels::BlockId>(voxels::BlockType::Water));
                        }
                    }
                }
                world.GetOrCreateChunk({x, y, z}) = std::move(chunk);
            }
        }
    }

    REQUIRE_FALSE(voxels::TryFindSafeSpawn(world, 0, 0, 8).has_value());
    REQUIRE_FALSE(voxels::FindAnyLoadedDrySpawn(world).has_value());
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

TEST_CASE("Gen.DeterminismHoldsAcrossWorkerCountsAndOrder", "[world][generation]") {
    const voxels::WorldOptions options{.seed = 0x9f73a48bu};
    const std::array<voxels::ChunkCoordinate, 12> coordinates = {{{-2, 0, -1}, {-1, 1, 0}, {0, 2, 1}, {1, 3, -2},
                                                                     {2, 4, 2}, {-3, 5, 3}, {3, 6, -3}, {-4, 7, 4},
                                                                     {4, 0, -4}, {-5, 1, 5}, {5, 2, -5}, {0, 3, 0}}};
    const voxels::WorldGenerator generator(options);
    std::unordered_map<voxels::ChunkCoordinate, std::vector<std::uint8_t>, voxels::ChunkCoordinateHash> baseline;
    for (const auto& coordinate : coordinates) baseline.emplace(coordinate, generator.GenerateChunk(coordinate).SerializeRLE());

    for (const std::size_t workerCount : {std::size_t{1}, std::size_t{2}, std::size_t{8}, std::size_t{16}}) {
        voxels::JobSystem jobs(workerCount);
        std::vector<std::future<voxels::Chunk>> futures;
        futures.reserve(coordinates.size());
        for (auto coordinate = coordinates.rbegin(); coordinate != coordinates.rend(); ++coordinate) {
            futures.push_back(jobs.EnqueueWithResult([options, coordinate = *coordinate] {
                return voxels::WorldGenerator(options).GenerateChunk(coordinate);
            }));
        }
        for (auto& future : futures) {
            const voxels::Chunk chunk = future.get();
            REQUIRE(chunk.SerializeRLE() == baseline.at(chunk.GetCoordinate()));
        }
        jobs.Shutdown();
    }
}

TEST_CASE("Gen.BiomesAndBedrockAreStable", "[world][generation]") {
    const voxels::WorldGenerator generator({.seed = 773849u});
    for (int z = -128; z <= 128; z += 8) {
        for (int x = -128; x <= 128; x += 8) {
            const voxels::TerrainColumn first = generator.SampleColumn(x, z);
            const voxels::TerrainColumn second = generator.SampleColumn(x, z);
            REQUIRE(first.surfaceY == second.surfaceY);
            REQUIRE(first.biome == second.biome);
            REQUIRE(first.surfaceY >= 5);
            REQUIRE(first.surfaceY <= 118);
        }
    }
    for (int x = -2; x <= 2; ++x) {
        const voxels::Chunk chunk = generator.GenerateChunk({x, 0, 0});
        for (int z = 0; z < 16; ++z) for (int localX = 0; localX < 16; ++localX) {
            REQUIRE(chunk.GetBlock(localX, 0, z) == static_cast<voxels::BlockId>(voxels::BlockType::Bedrock));
        }
    }
}

TEST_CASE("Gen.LegacyVersionOneRemainsStable", "[world][generation]") {
    const voxels::WorldOptions legacyOptions{.seed = 773849u, .generatorVersion = 1};
    const voxels::Chunk first = voxels::WorldGenerator(legacyOptions).GenerateChunk({2, 1, -3});
    const voxels::Chunk second = voxels::WorldGenerator(legacyOptions).GenerateChunk({2, 1, -3});
    const voxels::Chunk current = voxels::WorldGenerator({.seed = 773849u, .generatorVersion = 2}).GenerateChunk({2, 1, -3});

    REQUIRE(first.SerializeRLE() == second.SerializeRLE());
    REQUIRE(first.SerializeRLE() != current.SerializeRLE());
}
