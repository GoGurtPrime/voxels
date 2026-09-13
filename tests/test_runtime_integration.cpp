#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "voxels/app/save_manager.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/platform/headless_platform.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/world.hpp"
#include "voxels/world/world_serialization.hpp"

namespace {

std::string TempRoot(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / ("voxels_runtime_" + name);
    std::filesystem::remove_all(path);
    return path.string();
}

} // namespace

TEST_CASE("WorldSave.RoundTrip", "[runtime][save]") {
    voxels::World world(16);
    world.SetBlock({0, 2, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({1, 2, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    world.SetBlock({0, 3, 1}, static_cast<voxels::BlockId>(voxels::BlockType::Coal));

    const std::string root = TempRoot("world_roundtrip");
    const auto savePath = std::filesystem::path(root) / "TestWorld";
    REQUIRE(voxels::SaveWorld(world, savePath));

    voxels::World loaded(16);
    REQUIRE(voxels::LoadWorld(loaded, savePath));
    REQUIRE(loaded.GetBlock({0, 2, 0}) == static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    REQUIRE(loaded.GetBlock({1, 2, 0}) == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    REQUIRE(loaded.GetBlock({0, 3, 1}) == static_cast<voxels::BlockId>(voxels::BlockType::Coal));

    std::filesystem::remove_all(savePath);
}

TEST_CASE("PlayerSave.RoundTrip", "[runtime][save]") {
    voxels::PlayerState state{};
    state.position = {12.5f, 26.0f, -4.25f};
    state.velocity = {1.5f, -0.25f, 2.0f};
    state.yaw = 90.0f;
    state.pitch = -15.5f;
    state.onGround = true;
    state.inventory.SetSelectedSlot(7);
    state.health = 12;
    state.inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Stone), 17};
    state.inventory.GetSlot(1) = {static_cast<voxels::BlockId>(voxels::BlockType::TreeTrunk), 3};
    state.inventory.GetSlot(2) = {static_cast<voxels::BlockId>(voxels::BlockType::Leaf), 64};

    const std::string root = TempRoot("player_roundtrip");
    const auto playerPath = std::filesystem::path(root) / "TestWorld" / "players" / "player_1.player";
    REQUIRE(voxels::SavePlayerState(playerPath, state));

    voxels::PlayerState loaded{};
    REQUIRE(voxels::LoadPlayerState(playerPath, loaded));
    REQUIRE(loaded == state);

    std::filesystem::remove_all(std::filesystem::path(root) / "TestWorld");
}

TEST_CASE("WorldSave.PreviewPathStaysInsideValidatedSaveDirectory", "[runtime][save]") {
    const std::filesystem::path root = TempRoot("preview_path");
    voxels::SaveManager saveManager(root);

    REQUIRE(saveManager.GetWorldPreviewPath("TestWorld") == root / "TestWorld" / "preview.png");
    REQUIRE(saveManager.GetWorldPreviewPath("../escape").empty());
    REQUIRE(saveManager.GetWorldPreviewPath("nested/world").empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("WorldSave.FailedPreviewRefreshPreservesPreviousImage", "[runtime][save]") {
    const std::filesystem::path root = TempRoot("preview_atomic");
    voxels::SaveManager saveManager(root);
    voxels::GameSave save{};
    save.saveName = "TestWorld";
    REQUIRE(saveManager.Save(save));
    REQUIRE(saveManager.SaveWorldPreview(save.saveName, [](const std::filesystem::path& path) {
        std::ofstream(path, std::ios::binary) << "first";
        return true;
    }));

    REQUIRE_FALSE(saveManager.SaveWorldPreview(save.saveName, [](const std::filesystem::path& path) {
        std::ofstream(path, std::ios::binary) << "broken";
        return false;
    }));
    std::ifstream preview(saveManager.GetWorldPreviewPath(save.saveName), std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(preview), std::istreambuf_iterator<char>()) == "first");
    preview.close();

    std::filesystem::remove_all(root);
}

TEST_CASE("GameLoop.RunsBoundedTicksHeadless", "[runtime][loop]") {
    voxels::HeadlessPlatform platform;
    REQUIRE(platform.Initialize({"HeadlessLoop", 640, 480, false, false}));
    REQUIRE_NOTHROW([&]() {
        for (int i = 0; i < 50; ++i) {
            platform.PollEvents(nullptr);
            platform.SwapBuffers();
        }
    }());
    platform.Shutdown();
}

TEST_CASE("ChunkStreaming.UnloadsOnlyCleanChunksBeyondHysteresis", "[runtime][streaming]") {
    voxels::World world(16);
    world.Initialize({.seed = 1u, .renderDistanceChunks = 2, .simulationDistanceChunks = 1});

    const auto chunkAtOrigin = voxels::ChunkCoordinate{0, 0, 0};
    const auto chunkAtNext = voxels::ChunkCoordinate{1, 0, 0};
    const auto farChunk = voxels::ChunkCoordinate{4, 0, 0};

    world.GetOrCreateChunk(chunkAtOrigin);
    world.GetOrCreateChunk(chunkAtNext);
    world.GetOrCreateChunk(farChunk);

    REQUIRE(world.HasChunk(chunkAtOrigin));
    REQUIRE(world.HasChunk(chunkAtNext));
    REQUIRE(world.HasChunk(farChunk));

    world.SetPlayerSpawn(chunkAtOrigin);
    const bool loaded = world.HasChunk(chunkAtOrigin) && world.HasChunk(chunkAtNext);
    REQUIRE(loaded);

    world.GetOrCreateChunk(farChunk);
    REQUIRE(world.HasChunk(farChunk));
    REQUIRE(world.LoadedChunkCount() >= 3);

    REQUIRE(world.UnloadCleanChunksOutsideRadius(chunkAtOrigin, 2) == 1);
    REQUIRE_FALSE(world.HasChunk(farChunk));

    world.GetOrCreateChunk(farChunk).SetBlock(0, 0, 0, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    REQUIRE(world.UnloadCleanChunksOutsideRadius(chunkAtOrigin, 2) == 0);
    REQUIRE(world.HasChunk(farChunk));
}
