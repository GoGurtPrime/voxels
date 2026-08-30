#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

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

    REQUIRE(loading.GetPhase() == voxels::GenerationPhase::Complete);
    REQUIRE(loading.GetProgress() == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(loading.GetWorld().HasChunk({0, 0, 0}));

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
