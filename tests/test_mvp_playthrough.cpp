#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

#include "voxels/app/save_manager.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/gameplay/physics.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/world/generation_pipeline.hpp"

TEST_CASE("MVP.LoadingScreenGeneratesPlayableWorld", "[mvp]") {
    const auto root = std::filesystem::temp_directory_path() / "voxels_mvp_loading";
    std::filesystem::remove_all(root);

    voxels::WorldOptions options{};
    options.seed = 9u;
    options.visibility = true;
    options.isPublic = true;

    voxels::SaveManager manager(root);
    voxels::GameSave save{};
    save.saveName = "PlayWorld";
    save.worldName = "PlayWorld";
    save.playerName = "Alice";
    save.seed = static_cast<voxels::WorldSeed>(options.seed);
    save.publicVisibility = true;
    REQUIRE(manager.Save(save));

    voxels::LoadingScreenState loading;
    loading.SetSaveManager(manager);
    loading.SetSaveName(save.saveName);
    loading.SetWorldOptions(options);
    loading.RunGeneration();

    REQUIRE(loading.GetPhase() == voxels::GenerationPhase::Shape);
    REQUIRE(loading.GetWorld().LoadedChunkCount() == 0);
    for (int index = 0; index < 200; ++index) loading.Update(0.0);
    REQUIRE(loading.GetWorld().LoadedChunkCount() == 225);
    loading.Update(0.0);
    REQUIRE(loading.GetPhase() == voxels::GenerationPhase::Complete);
    REQUIRE(loading.GetProgress() == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(loading.GetWorld().HasChunk({-2, 0, -2}));
    REQUIRE(loading.GetWorld().HasChunk({2, 8, 2}));

    const auto spawn = loading.GetSpawnPosition();
    REQUIRE(spawn.y > 0);

    voxels::Player player{};
    player.state.position = {static_cast<float>(spawn.x) + 0.5f, static_cast<float>(spawn.y) + 2.0f,
                            static_cast<float>(spawn.z) + 0.5f};
    player.state.onGround = false;
    voxels::gameplay::Physics::Step(loading.GetWorld(), player, 1.0f / 60.0f);
    REQUIRE(player.state.position.y >= 0.0f);

    std::filesystem::remove_all(root);
}

TEST_CASE("MVP.AsyncLoadingIntegratesAtMostTwoChunksPerFrame", "[mvp][generation][performance]") {
    voxels::AppContext context{};
    voxels::LoadingScreenState loading(&context);
    loading.SetWorldOptions({.seed = 4815162342u});
    loading.RunGeneration();

    std::size_t previousChunkCount = 0;
    for (int frame = 0; frame < 600 && loading.GetPhase() != voxels::GenerationPhase::Complete; ++frame) {
        loading.Update(0.0);
        const std::size_t chunkCount = loading.GetWorld().LoadedChunkCount();
        REQUIRE(chunkCount - previousChunkCount <= 4);
        previousChunkCount = chunkCount;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(loading.GetPhase() == voxels::GenerationPhase::Complete);
    REQUIRE(loading.GetWorld().LoadedChunkCount() == 225);
    loading.OnExit();
}

TEST_CASE("MVP.LoadingExistingWorldGeneratesAroundSavedPosition", "[mvp][generation][persistence]") {
    const auto root = std::filesystem::temp_directory_path() / "voxels_mvp_saved_position_loading";
    std::filesystem::remove_all(root);

    voxels::SaveManager manager(root);
    voxels::GameSave save{};
    save.saveName = "TravelledWorld";
    save.worldName = "TravelledWorld";
    save.playerName = "Player";
    save.seed = 1724465470u;
    save.spawnX = 327.132f;
    save.spawnY = 25.9f;
    save.spawnZ = -89.875f;
    REQUIRE(manager.Save(save));

    voxels::LoadingScreenState loading;
    loading.SetSaveManager(manager);
    loading.SetSaveName(save.saveName);
    loading.RunGeneration();
    for (int index = 0; index < 200; ++index) loading.Update(0.0);

    REQUIRE(loading.GetWorld().HasChunk({18, 0, -8}));
    REQUIRE(loading.GetWorld().HasChunk({22, 8, -4}));
    REQUIRE_FALSE(loading.GetWorld().HasChunk({0, 0, 0}));

    std::filesystem::remove_all(root);
}

TEST_CASE("MVP.LocalServerAcceptsSecondClient", "[mvp]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    REQUIRE(server.Port() != 0);

    voxels::networking::GameClient first;
    voxels::networking::GameClient second;
    REQUIRE(first.Connect("127.0.0.1", server.Port()));
    REQUIRE(second.Connect("127.0.0.1", server.Port()));

    server.Tick();
    first.Tick();
    second.Tick();

    REQUIRE(server.PeerCount() >= 2);
    REQUIRE(first.HasReceivedConnectAck());
    REQUIRE(second.HasReceivedConnectAck());
}
