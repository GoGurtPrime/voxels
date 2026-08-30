#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voxels/app/game_session.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/gameplay/camera_controller.hpp"
#include "voxels/gameplay/physics.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/world.hpp"

TEST_CASE("Player.SpawnsOnGroundAndTracksCamera", "[player][streaming]") {
    voxels::World world(16);
    world.Initialize({.seed = 42u, .renderDistanceChunks = 4, .simulationDistanceChunks = 3});
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            for (int y = 0; y < 2; ++y) {
                world.SetBlock({x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            }
        }
    }

    voxels::GameSession session;
    session.SetWorld(&world);
    session.SetPlayerSpawn(voxels::Vec3{0.0f, 2.0f, 0.0f});
    session.Initialize();

    REQUIRE(session.GetPlayer().state.position.y >= 2.0f);
    REQUIRE(session.GetPlayer().state.onGround);
    REQUIRE(session.GetCamera().position.y >= 1.6f);
}

TEST_CASE("GameSession.StreamsChunksAroundPlayer", "[player][streaming]") {
    voxels::GameSession session;
    session.SetWorld(&session.GetWorld());
    session.SetPlayerSpawn(voxels::Vec3I{0, 1, 0});
    session.Initialize();

    session.GetPlayer().state.position = {48.0f, 10.0f, 0.0f};
    session.Update(1.0f / 60.0f);

    REQUIRE(session.GetWorld().LoadedChunkCount() >= 1);
    REQUIRE_FALSE(session.GetCamera().position.x == Catch::Approx(0.0f));
}

TEST_CASE("InGameState.InitializesVisibleWorldRenderer", "[player][streaming]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas(16, 16);
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");

    voxels::InGameState state;
    state.SetBlockRegistry(&registry);
    state.SetTextureAtlas(&atlas);
    state.SetWorldOptions({.seed = 123u, .renderDistanceChunks = 4, .simulationDistanceChunks = 3});

    state.OnEnter();

    REQUIRE(state.GetChunkRenderer() != nullptr);
    REQUIRE(state.GetWorld().LoadedChunkCount() > 0);
    state.OnExit();
}

TEST_CASE("Input.RebindingMoveForwardChangesResultingIntent", "[player][input]") {
    voxels::InputManager manager;
    manager.BindAction("MoveForward", voxels::InputBinding{"", 87, 0, voxels::InputDeviceType::Keyboard});
    manager.InjectKeyEvent(87, true);
    REQUIRE(manager.GetInputState().moveForward);

    manager.BindAction("MoveForward", voxels::InputBinding{"", 83, 0, voxels::InputDeviceType::Keyboard});
    manager.InjectKeyEvent(87, false);
    manager.InjectKeyEvent(83, true);
    voxels::InputState rebound = manager.GetInputState();
    REQUIRE(rebound.moveForward);
    REQUIRE_FALSE(rebound.moveBackward);
}

TEST_CASE("GameSession.SelectedItemLabelTracksSelectedSlotContents", "[player][inventory]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::InputManager input;
    input.BindAction("Hotbar2", voxels::InputBinding{"", static_cast<int>('2'), 0, voxels::InputDeviceType::Keyboard});

    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetInputManager(&input);
    session.Initialize();
    session.GetPlayer().state.inventory.GetSlot(1) = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1};

    input.InjectKeyEvent(static_cast<int>('2'), true);
    session.Update(1.0f / 60.0f);
    REQUIRE(session.GetSelectedItemLabel() == "Dirt");
    REQUIRE(session.GetSelectedItemLabelAge() == Catch::Approx(0.0f));

    input.InjectKeyEvent(static_cast<int>('2'), false);
    session.Update(0.5f);
    REQUIRE(session.GetSelectedItemLabelAge() == Catch::Approx(0.5f));
}
