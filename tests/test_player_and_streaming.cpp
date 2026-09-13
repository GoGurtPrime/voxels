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

TEST_CASE("InGameState.ResumeDoesNotTeleportPlayerToInitialSpawn", "[player][streaming][app]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas(16, 16);
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");

    voxels::GameSave save{};
    save.spawnX = 8.5f;
    save.spawnY = 100.0f;
    save.spawnZ = 8.5f;

    voxels::InGameState state;
    state.SetBlockRegistry(&registry);
    state.SetTextureAtlas(&atlas);
    state.SetWorldOptions({.seed = 123u, .renderDistanceChunks = 4, .simulationDistanceChunks = 3});
    state.SetActiveSave(save);
    state.OnEnter();

    const float spawnY = state.GetPlayer().state.position.y;
    for (int i = 0; i < 10; ++i) {
        state.Update(1.0 / 60.0);
    }
    const float simulatedY = state.GetPlayer().state.position.y;
    REQUIRE(simulatedY < spawnY);

    state.OnResume();
    const float resumedY = state.GetPlayer().state.position.y;
    REQUIRE(resumedY == Catch::Approx(simulatedY).margin(0.001f));

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

TEST_CASE("GameSession.HoldingSwimUpAtTheSurfaceDoesNotSpamSplashSounds", "[player][audio][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    for (int x = -3; x <= 3; ++x) {
        for (int z = -3; z <= 3; ++z) {
            world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            for (int y = 1; y <= 6; ++y) {
                world.SetBlock({x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
            }
        }
    }
    voxels::InputManager input;
    input.BindAction("Jump", voxels::InputBinding{"", static_cast<int>(' '), 0, voxels::InputDeviceType::Keyboard});

    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetInputManager(&input);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 2.5f, 0.5f});
    session.Initialize();

    input.InjectKeyEvent(static_cast<int>(' '), true);
    for (int i = 0; i < 600; ++i) { // 10s at 60Hz: reaches and holds the tread ceiling
        session.Update(1.0f / 60.0f);
    }

    int splashCount = 0;
    for (const auto& event : session.GetSoundEvents()) {
        if (event.type == voxels::GameplaySoundEventType::Splash) ++splashCount;
    }
    // Bare bodyFraction>0 crossings right at the tread ceiling used to fire dozens of splash
    // events per second; hysteresis + a cooldown should keep this to at most one real transition.
    REQUIRE(splashCount <= 1);
}

TEST_CASE("GameSession.WalkingInAShallowPuddleWhileHoldingJumpDoesNotSpamSplashSounds",
         "[player][audio][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    for (int x = -3; x <= 3; ++x) {
        for (int z = -3; z <= 3; ++z) {
            world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            world.SetBlock({x, 1, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
        }
    }
    voxels::InputManager input;
    input.BindAction("Jump", voxels::InputBinding{"", static_cast<int>(' '), 0, voxels::InputDeviceType::Keyboard});
    input.BindAction("MoveForward", voxels::InputBinding{"", static_cast<int>('w'), 0, voxels::InputDeviceType::Keyboard});

    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetInputManager(&input);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 0.5f});
    session.Initialize();

    input.InjectKeyEvent(static_cast<int>(' '), true);
    input.InjectKeyEvent(static_cast<int>('w'), true);
    for (int i = 0; i < 180; ++i) { // 3s at 60Hz while standing/walking in a one-block puddle
        session.Update(1.0f / 60.0f);
    }

    int splashCount = 0;
    for (const auto& event : session.GetSoundEvents()) {
        if (event.type == voxels::GameplaySoundEventType::Splash) ++splashCount;
    }
    // A shallow puddle must not engage the swim ceiling: holding jump there should behave like a
    // normal, comparatively slow land-jump cadence rather than a rapid buoyant bounce loop, so the
    // splash count over 3 seconds stays low instead of in the dozens.
    REQUIRE(splashCount <= 3);
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
