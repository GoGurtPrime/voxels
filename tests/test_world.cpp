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
#include "voxels/render/celestial_lighting.hpp"
#include "voxels/render/sky_renderer.hpp"

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

TEST_CASE("World.LightingPropagatesEmissionAcrossChunkBoundariesAndRemovesIt", "[world][lighting]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::BlockDefinition lamp{};
    lamp.id = 100;
    lamp.name = "test_lamp";
    lamp.displayName = "Test Lamp";
    lamp.isOpaque = true;
    lamp.lightEmission = 15;
    registry.RegisterBlock(lamp);

    voxels::World world;
    REQUIRE(world.SetBlock({15, 8, 0}, lamp.id));
    REQUIRE(world.SetBlock({16, 8, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Air)));
    const voxels::LightingUpdate lit = world.RebuildLightingAround({15, 8, 0}, registry, 4);
    REQUIRE(lit.touchedVoxels > 0);
    REQUIRE(world.GetBlockLight({15, 8, 0}) == 15);
    REQUIRE(world.GetBlockLight({16, 8, 0}) == 14);
    REQUIRE(lit.dirtyChunks.size() >= 2);

    REQUIRE(world.SetBlock({15, 8, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Air)));
    static_cast<void>(world.RebuildLightingAround({15, 8, 0}, registry, 4));
    REQUIRE(world.GetBlockLight({15, 8, 0}) == 0);
    REQUIRE(world.GetBlockLight({16, 8, 0}) == 0);
}

TEST_CASE("World.SkylightBleedsLaterallyBelowOverhangs", "[world][lighting]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::World world;
    REQUIRE(world.SetBlock({0, 10, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    REQUIRE(world.SetBlock({1, 9, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Air)));
    static_cast<void>(world.RebuildLightingAround({0, 9, 0}, registry, 3));
    REQUIRE(world.GetSkyLight({1, 9, 0}) == 15);
    REQUIRE(world.GetSkyLight({0, 9, 0}) == 14);
}

TEST_CASE("World.LightingUsesRegistryOpacity", "[world][lighting]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::World world;
    REQUIRE(world.SetBlock({0, 9, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    REQUIRE(world.SetBlock({0, 8, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Air)));
    static_cast<void>(world.RebuildLightingAround({0, 8, 0}, registry, 0));
    REQUIRE(world.GetSkyLight({0, 9, 0}) == 0);
    REQUIRE(world.GetSkyLight({0, 8, 0}) == 0);

    REQUIRE(world.SetBlock({0, 9, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Glass)));
    static_cast<void>(world.RebuildLightingAround({0, 8, 0}, registry, 0));
    REQUIRE(world.GetSkyLight({0, 9, 0}) == 15);
    REQUIRE(world.GetSkyLight({0, 8, 0}) == 15);
}

TEST_CASE("World.LightingRecomputationIsDeterministicForReplicatedEdits", "[world][lighting][networking]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::BlockDefinition lamp{};
    lamp.id = 100;
    lamp.name = "replicated_lamp";
    lamp.displayName = "Replicated Lamp";
    lamp.isOpaque = true;
    lamp.lightEmission = 12;
    registry.RegisterBlock(lamp);
    voxels::World host;
    voxels::World client;
    for (voxels::World* world : {&host, &client}) {
        REQUIRE(world->SetBlock({15, 8, 0}, lamp.id));
        REQUIRE(world->SetBlock({16, 9, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
        REQUIRE(world->SetBlock({16, 8, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Air)));
    }

    const voxels::LightingUpdate hostUpdate = host.RebuildLightingAround({15, 8, 0}, registry, 4);
    const voxels::LightingUpdate clientUpdate = client.RebuildLightingAround({15, 8, 0}, registry, 4);
    REQUIRE(clientUpdate.touchedVoxels == hostUpdate.touchedVoxels);
    REQUIRE(clientUpdate.dirtyChunks == hostUpdate.dirtyChunks);
    for (int z = -4; z <= 4; ++z) {
        for (int y = 4; y <= 12; ++y) {
            for (int x = 11; x <= 19; ++x) {
                const voxels::Vec3I position{x, y, z};
                REQUIRE(client.GetSkyLight(position) == host.GetSkyLight(position));
                REQUIRE(client.GetBlockLight(position) == host.GetBlockLight(position));
            }
        }
    }
}

TEST_CASE("World.EditRelightingIsBoundedAndReportsOnlyFinalChanges", "[world][lighting][performance]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    constexpr std::size_t kMaximumDefaultEditVoxels = 9U * 9U * 256U;
    voxels::World world;
    for (int chunkZ = -1; chunkZ <= 1; ++chunkZ) {
        for (int chunkY = 0; chunkY <= 8; ++chunkY) {
            for (int chunkX = -1; chunkX <= 1; ++chunkX) {
                world.GetOrCreateChunk({chunkX, chunkY, chunkZ});
            }
        }
    }
    REQUIRE(world.SetBlock({0, 62, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    REQUIRE(world.SetBlock({0, 63, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    static_cast<void>(world.RebuildLightingAround({0, 63, 0}, registry));

    REQUIRE(world.SetBlock({0, 63, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Air)));
    const voxels::LightingUpdate edit = world.RebuildLightingAround({0, 63, 0}, registry);
    REQUIRE(edit.examinedVoxels <= kMaximumDefaultEditVoxels);
    REQUIRE(edit.touchedVoxels > 0);
    REQUIRE(edit.dirtyChunks.size() <= 2);

    const voxels::LightingUpdate stable = world.RebuildLightingAround({0, 63, 0}, registry);
    REQUIRE(stable.examinedVoxels <= kMaximumDefaultEditVoxels);
    REQUIRE(stable.touchedVoxels == 0);
    REQUIRE(stable.dirtyChunks.empty());
}

TEST_CASE("CelestialLighting.CyclesDeterministicallyAndAlwaysDayPinsNoon", "[render][lighting]") {
    using voxels::graphics::EvaluateCelestialLighting;
    using voxels::graphics::kDayDurationSeconds;
    const auto noon = EvaluateCelestialLighting(kDayDurationSeconds * 0.25f, false);
    const auto midnight = EvaluateCelestialLighting(kDayDurationSeconds * 0.75f, false);
    const auto wrappedNoon = EvaluateCelestialLighting(kDayDurationSeconds * 1.25f, false);
    const auto alwaysDay = EvaluateCelestialLighting(kDayDurationSeconds * 0.75f, true);

    REQUIRE(glm::length(noon.sunDirection) == Catch::Approx(1.0f));
    REQUIRE(noon.sunDirection.y > 0.9f);
    REQUIRE(midnight.sunDirection.y < -0.9f);
    REQUIRE(glm::length(noon.ambientColor) > glm::length(midnight.ambientColor) * 4.0f);
    REQUIRE(wrappedNoon.skyColor == noon.skyColor);
    REQUIRE(alwaysDay.sunDirection == noon.sunDirection);
    REQUIRE(alwaysDay.skyColor == noon.skyColor);
    REQUIRE(voxels::graphics::NormalizeDayTime(-1.0f) == Catch::Approx(kDayDurationSeconds - 1.0f));
}

TEST_CASE("Sky.FogRangeReservesTheFinalLoadedChunk", "[render][sky][fog]") {
    const auto fog = voxels::graphics::FogRangeForRenderDistance(8);
    REQUIRE(fog.endBlocks == Catch::Approx(112.0f));
    REQUIRE(fog.startBlocks == Catch::Approx(67.2f));
    REQUIRE(fog.startBlocks < fog.endBlocks);

    const auto minimumFog = voxels::graphics::FogRangeForRenderDistance(1);
    REQUIRE(minimumFog.endBlocks == Catch::Approx(16.0f));
    REQUIRE(minimumFog.startBlocks == Catch::Approx(9.6f));
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
