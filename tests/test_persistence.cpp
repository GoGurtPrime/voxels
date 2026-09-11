/**
 * @file test_persistence.cpp
 * @brief Regression tests for durable world and settings persistence.
 *
 * @details Exercises filesystem-visible persistence behavior and chunk snapshot contracts
 *          used by the application save flow in ARCHITECTURE.md section 6.5.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "voxels/gameplay/player.hpp"
#include "voxels/app/save_manager.hpp"
#include "voxels/core/preferences.hpp"
#include "voxels/graphics/renderer.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/world.hpp"
#include "voxels/world/world_clock.hpp"
#include "voxels/world/world_serialization.hpp"

TEST_CASE("Save.ChunkEditsTrackDirtyState", "[persistence]") {
    voxels::Chunk chunk({0, 0, 0});

    REQUIRE_FALSE(chunk.IsDirty());
    REQUIRE(chunk.SetBlock(2, 3, 4, static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    REQUIRE(chunk.IsDirty());

    chunk.ClearDirty();
    REQUIRE(chunk.SetBlockLight(2, 3, 4, 12));
    REQUIRE_FALSE(chunk.IsDirty());

    const auto restored = voxels::Chunk::DeserializeRLE(chunk.SerializeRLE(), {0, 0, 0});
    REQUIRE_FALSE(restored.IsDirty());
}

TEST_CASE("Region.MultipleChunksRoundTripThroughOneRegionFile", "[persistence]") {
    const auto root = std::filesystem::temp_directory_path() / "voxels_region_round_trip";
    std::filesystem::remove_all(root);
    voxels::World source;
    source.GetOrCreateChunk({0, 0, 0}).SetBlock(1, 2, 3, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    source.GetOrCreateChunk({1, 0, 0}).SetBlock(4, 5, 6, static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    source.GetOrCreateChunk({0, 1, 0}).SetBlock(7, 8, 9, static_cast<voxels::BlockId>(voxels::BlockType::Sand));

    REQUIRE(voxels::SaveWorld(source, root));
    REQUIRE(std::filesystem::exists(root / "regions" / "r.0.0.vrg"));

    voxels::World restored;
    REQUIRE(voxels::LoadWorld(restored, root));
    REQUIRE(restored.GetOrCreateChunk({0, 0, 0}).GetBlock(1, 2, 3) == static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    REQUIRE(restored.GetOrCreateChunk({1, 0, 0}).GetBlock(4, 5, 6) == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    REQUIRE(restored.GetOrCreateChunk({0, 1, 0}).GetBlock(7, 8, 9) == static_cast<voxels::BlockId>(voxels::BlockType::Sand));
    std::filesystem::remove_all(root);
}

TEST_CASE("Region.CorruptRegionIsSkippedWithoutChangingResidentWorld", "[persistence]") {
    const auto root = std::filesystem::temp_directory_path() / "voxels_region_corruption";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "regions");
    std::ofstream(root / "regions" / "r.0.0.vrg", std::ios::binary) << "not a region";
    voxels::World world;
    world.GetOrCreateChunk({0, 0, 0}).SetBlock(0, 0, 0, static_cast<voxels::BlockId>(voxels::BlockType::Grass));

    REQUIRE(voxels::LoadWorld(world, root));
    REQUIRE(world.GetOrCreateChunk({0, 0, 0}).GetBlock(0, 0, 0) == static_cast<voxels::BlockId>(voxels::BlockType::Grass));
    std::filesystem::remove_all(root);
}

TEST_CASE("Save.PlayerStateRoundTripsIncludingInventory", "[persistence]") {
    const auto file = std::filesystem::temp_directory_path() / "voxels_player_round_trip.dat";
    std::filesystem::remove(file);
    voxels::PlayerState source{};
    source.position = {2.5f, 61.25f, -9.0f};
    source.velocity = {0.3f, -1.2f, 4.0f};
    source.yaw = 91.0f;
    source.pitch = -18.0f;
    source.health = 73.0f;
    source.inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Stone), 23};
    source.inventory.SetSelectedSlot(0);

    REQUIRE(voxels::SavePlayerState(file, source));
    voxels::PlayerState restored{};
    REQUIRE(voxels::LoadPlayerState(file, restored));
    REQUIRE(restored == source);
    std::filesystem::remove(file);
}

TEST_CASE("Save.LevelMetadataRoundTripsAndRejectsTraversal", "[persistence]") {
    const auto root = std::filesystem::temp_directory_path() / "voxels_level_metadata";
    std::filesystem::remove_all(root);
    voxels::SaveManager manager(root);
    voxels::GameSave source{};
    source.saveName = "world_one";
    source.worldName = "World One";
    source.playerName = "Player";
    source.seed = 77231;
    source.createdUtc = "2026-08-30T00:00:00Z";
    source.lastPlayedAt = "2026-08-30T01:00:00Z";
    source.playTimeSeconds = 3600;
    source.worldTick = 9876543210123ULL;
    source.spawnX = 12.5f;
    source.spawnY = 43.0f;
    source.spawnZ = -8.5f;
    source.sandboxMode = true;

    REQUIRE(manager.Save(source));
    voxels::GameSave loaded{};
    REQUIRE(manager.Load(source.saveName, loaded));
    REQUIRE(loaded.schemaVersion == 2);
    REQUIRE(loaded.worldName == source.worldName);
    REQUIRE(loaded.seed == source.seed);
    REQUIRE(loaded.playTimeSeconds == source.playTimeSeconds);
    REQUIRE(loaded.worldTick == source.worldTick);
    REQUIRE(loaded.spawnX == source.spawnX);
    REQUIRE(loaded.sandboxMode);
    REQUIRE_FALSE(manager.Save(voxels::GameSave{.saveName = "../../outside"}));
    REQUIRE_FALSE(manager.DeleteSave("../../world_one"));
    REQUIRE(std::filesystem::exists(root / source.saveName / "level.json"));
    std::filesystem::remove_all(root);
}

TEST_CASE("Save.LegacyLevelMetadataDefaultsWorldClockToDawn", "[persistence][world_clock]") {
    const std::string legacyMetadata = R"({
"schemaVersion":1,
"displayName":"Legacy World",
"saveName":"legacy_world",
"playerName":"Player",
"seed":42,
"createdUtc":"2026-01-01T00:00:00Z",
"lastPlayedUtc":"2026-01-01T00:00:00Z",
"playTimeSeconds":0,
"spawnX":0,"spawnY":64,"spawnZ":0,
"generatorVersion":1,"engineVersion":"0.0.0",
"peaceful":0,"permadeath":0,"alwaysSunny":1,"sandboxMode":0,
"renderDistanceChunks":8,"simulationDistanceChunks":4,"publicVisibility":1
})";

    const voxels::GameSave migrated = voxels::SaveManager::FromMetaText(legacyMetadata);
    REQUIRE(migrated.saveName == "legacy_world");
    REQUIRE(migrated.schemaVersion == 1);
    REQUIRE(migrated.worldTick == voxels::kInitialWorldTick);
}

TEST_CASE("Settings.PreserveUnknownKeysAcrossAtomicRewrite", "[persistence]") {
    const auto path = std::filesystem::temp_directory_path() / "voxels_settings_preservation.json";
    std::ofstream(path) << "{\"futureSetting\":\"retained\",\"renderDistance\":3}";
    voxels::PreferencesManager manager(path);
    voxels::GamePreferences preferences = manager.Load();
    preferences.renderDistance = 11;
    manager.Save(preferences);
    std::ifstream input(path);
    const std::string text(std::istreambuf_iterator<char>(input), {});
    REQUIRE(text.find("futureSetting") != std::string::npos);
    REQUIRE(text.find("retained") != std::string::npos);
    REQUIRE(manager.Load().renderDistance == 11);
    REQUIRE_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    input.close();
    std::filesystem::remove(path);
}

TEST_CASE("Settings.RendererSelectionPersistsAndRespectsPlatformAvailability", "[persistence][graphics]") {
    const auto path = std::filesystem::temp_directory_path() / "voxels_renderer_settings.json";
    std::filesystem::remove(path);
    voxels::PreferencesManager windowsManager(path, voxels::PlatformType::Windows);
    voxels::GamePreferences preferences{};
    preferences.rendererBackend = voxels::RendererBackend::OpenGL;
    windowsManager.Save(preferences);
    REQUIRE(windowsManager.Load().rendererBackend == voxels::RendererBackend::OpenGL);

    REQUIRE(voxels::PreferencesManager::ResolveRendererBackend(voxels::RendererBackend::Automatic,
                                                               voxels::PlatformType::Windows) ==
            voxels::RendererBackend::Direct3D11);
    REQUIRE(voxels::PreferencesManager::ResolveRendererBackend(voxels::RendererBackend::Automatic,
                                                               voxels::PlatformType::Linux) ==
            voxels::RendererBackend::OpenGL);
    REQUIRE(voxels::PreferencesManager::ResolveRendererBackend(voxels::RendererBackend::Direct3D11,
                                                               voxels::PlatformType::MacOS) ==
            voxels::RendererBackend::OpenGL);

    const auto linuxBackends = voxels::graphics::AvailableRendererBackends(voxels::PlatformType::Linux);
    REQUIRE(linuxBackends.size() == 1);
    REQUIRE(linuxBackends.front() == voxels::RendererBackend::OpenGL);
    const auto windowsBackends = voxels::graphics::AvailableRendererBackends(voxels::PlatformType::Windows);
    REQUIRE_FALSE(windowsBackends.empty());
#if defined(VOXELS_HAS_DX11)
    REQUIRE(windowsBackends.front() == voxels::RendererBackend::Direct3D11);
#endif
    std::filesystem::remove(path);
}