/**
 * @file test_gameplay.cpp
 * @brief Automated regression tests for the player physics, camera, and block interaction layer.
 *
 * @details Exercises the gameplay systems end-to-end with the same world, input, and collision
 *          primitives used in the live runtime: player AABB motion, gravity, grounded landing,
 *          and block break/place interaction against a voxel world.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "voxels/gameplay/block_interaction.hpp"
#include "voxels/gameplay/camera_controller.hpp"
#include "voxels/gameplay/physics.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/world/world.hpp"

TEST_CASE("Physics.GravityAndGroundedStop", "[gameplay][physics]") {
    voxels::World world;
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        }
    }

    voxels::Player player;
    player.state.position = voxels::Vec3{0.5f, 6.0f, 0.5f};
    player.state.velocity = voxels::Vec3{0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 180; ++i) {
        voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f);
    }

    REQUIRE(player.state.onGround);
    REQUIRE(player.state.velocity.y == Catch::Approx(0.0f).margin(0.05f));
    REQUIRE(player.state.position.y == Catch::Approx(1.9f).margin(0.05f));
}

TEST_CASE("Physics.JumpArc", "[gameplay][physics]") {
    voxels::World world;
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        }
    }

    voxels::Player player;
    const float startY = 1.9f;
    player.state.position = voxels::Vec3{0.5f, startY, 0.5f};
    player.state.onGround = true;
    player.state.velocity = voxels::Vec3{0.0f, 0.0f, 0.0f};

    voxels::gameplay::Physics::Jump(player);
    bool fell = false;
    for (int i = 0; i < 200; ++i) {
        voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f);
        if (player.state.velocity.y < 0.0f) {
            fell = true;
        }
    }

    REQUIRE(fell);
    REQUIRE(player.state.onGround);
    REQUIRE(player.state.position.y == Catch::Approx(startY).margin(0.08f));
}

TEST_CASE("Physics.JumpApexIncreasesWhenCeilingBlockIsRemoved", "[gameplay][physics]") {
    const auto simulateJumpApex = [](int ceilingY) {
        voxels::World world;
        world.SetBlock(voxels::Vec3I{0, 0, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        world.SetBlock(voxels::Vec3I{0, ceilingY, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

        voxels::Player player;
        player.state.position = voxels::Vec3{0.5f, 1.9f, 0.5f};
        player.state.onGround = true;

        voxels::gameplay::Physics::Jump(player);
        float apex = player.state.position.y;
        for (int step = 0; step < 200; ++step) {
            voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f);
            apex = std::max(apex, player.state.position.y);
        }
        return apex;
    };

    const float twoBlockOpeningApex = simulateJumpApex(3);
    const float threeBlockOpeningApex = simulateJumpApex(4);

    REQUIRE(twoBlockOpeningApex == Catch::Approx(2.1f).margin(0.02f));
    REQUIRE(threeBlockOpeningApex == Catch::Approx(3.1f).margin(0.02f));
    REQUIRE(threeBlockOpeningApex - twoBlockOpeningApex == Catch::Approx(1.0f).margin(0.02f));
}

TEST_CASE("Physics.HorizontalCollisionStopsAtWall", "[gameplay][physics]") {
    voxels::World world;
    world.SetBlock(voxels::Vec3I{2, 1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock(voxels::Vec3I{2, 0, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    voxels::Player player;
    player.state.position = voxels::Vec3{1.0f, 1.9f, 0.5f};
    player.state.velocity = voxels::Vec3{6.0f, 0.0f, 0.0f};

    voxels::gameplay::Physics::Step(world, player, 0.2f);

    REQUIRE(player.state.position.x <= 1.7f);
    REQUIRE(player.state.position.x >= 1.0f);
    REQUIRE(player.state.velocity.x == Catch::Approx(0.0f).margin(0.05f));
}

TEST_CASE("Physics.ModelBoundsLandOnSlabHeight", "[gameplay][physics]") {
    voxels::BlockRegistry registry;
    registry.LoadFromJsonString(R"({"blocks":[
      {"id":"air","numeric_id":0,"solid":false,"opaque":false},
      {"id":"slab","numeric_id":1,"solid":true,"opaque":true,"render_type":"model","collision_bounds":{"min":[0,0,0],"max":[1,0.5,1]}}
    ]})");
    voxels::World world;
    world.SetBlock({0, 0, 0}, 1);
    voxels::Player player;
    player.state.position = {0.5f, 3.0f, 0.5f};
    for (int step = 0; step < 120; ++step) {
        voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry);
    }
    REQUIRE(player.state.onGround);
    REQUIRE(player.state.position.y == Catch::Approx(1.4f).margin(0.05f));
}

TEST_CASE("Physics.WaterReducesHorizontalSpeedAndArrestsFreeFall", "[gameplay][physics][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    for (int x = -3; x <= 3; ++x) {
        for (int z = -3; z <= 3; ++z) {
            for (int y = -4; y <= 4; ++y) {
                world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
            }
        }
    }

    voxels::Player dryPlayer;
    dryPlayer.state.position = voxels::Vec3{0.5f, 10.0f, 0.5f};
    dryPlayer.state.velocity = voxels::Vec3{4.3f, -20.0f, 0.0f};
    voxels::gameplay::Physics::Step(world, dryPlayer, 1.0f / 60.0f, &registry);
    const float dryHorizontalDistance = std::abs(dryPlayer.state.position.x - 0.5f);

    voxels::Player wetPlayer;
    wetPlayer.state.position = voxels::Vec3{0.5f, 0.0f, 0.5f};
    wetPlayer.state.velocity = voxels::Vec3{4.3f, -20.0f, 0.0f};
    voxels::gameplay::Physics::Step(world, wetPlayer, 1.0f / 60.0f, &registry);

    REQUIRE(std::abs(wetPlayer.state.position.x - 0.5f) < dryHorizontalDistance);
    REQUIRE(wetPlayer.state.velocity.y >= -voxels::gameplay::Physics::kWaterTerminalVelocity - 0.01f);
    REQUIRE(wetPlayer.state.velocity.y > -20.0f);
}

TEST_CASE("Physics.SwimAscendLetsPlayerReachAndExitTheSurface", "[gameplay][physics][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            for (int y = 1; y <= 6; ++y) {
                world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
            }
        }
    }

    voxels::Player player;
    player.state.position = voxels::Vec3{0.5f, 2.5f, 0.5f};
    player.state.velocity = voxels::Vec3{0.0f};

    bool reachedOpenAir = false;
    for (int step = 0; step < 400; ++step) {
        const voxels::gameplay::SubmersionInfo submersion =
            voxels::gameplay::Physics::SampleSubmersion(world, player, &registry);
        voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
        if (!submersion.eyeSubmerged && player.state.position.y > 6.5f) {
            reachedOpenAir = true;
            break;
        }
    }

    REQUIRE(reachedOpenAir);
}

TEST_CASE("Physics.SubmersionSamplingCoversShallowWaterHeadOnlyAndShore", "[gameplay][physics][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    world.SetBlock({0, 0, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({0, 1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Water));

    SECTION("Shallow ankle-deep water only partially submerges the AABB") {
        voxels::Player player;
        player.state.position = voxels::Vec3{0.5f, 1.9f, 0.5f};
        const voxels::gameplay::SubmersionInfo submersion =
            voxels::gameplay::Physics::SampleSubmersion(world, player, &registry);
        REQUIRE(submersion.feetSubmerged);
        REQUIRE_FALSE(submersion.eyeSubmerged);
        REQUIRE(submersion.bodyFraction > 0.3f);
        REQUIRE(submersion.bodyFraction < 0.8f);
    }

    SECTION("Head-only submersion in a deep pool reports eye submerged") {
        world.SetBlock({0, 2, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
        voxels::Player player;
        player.state.position = voxels::Vec3{0.5f, 1.9f, 0.5f};
        const voxels::gameplay::SubmersionInfo submersion =
            voxels::gameplay::Physics::SampleSubmersion(world, player, &registry);
        REQUIRE(submersion.eyeSubmerged);
        REQUIRE(submersion.bodyFraction > 0.5f);
    }

    SECTION("Jumping from dry shore is unaffected by nearby water") {
        voxels::World dryWorld;
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                dryWorld.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            }
        }
        voxels::Player player;
        player.state.position = voxels::Vec3{0.5f, 1.9f, 0.5f};
        player.state.onGround = true;
        const voxels::gameplay::SubmersionInfo submersion =
            voxels::gameplay::Physics::SampleSubmersion(dryWorld, player, &registry);
        REQUIRE_FALSE(submersion.feetSubmerged);
        REQUIRE(submersion.bodyFraction == Catch::Approx(0.0f));

        voxels::gameplay::Physics::Jump(player);
        const float startY = player.state.position.y;
        bool fell = false;
        for (int step = 0; step < 200; ++step) {
            voxels::gameplay::Physics::Step(dryWorld, player, 1.0f / 60.0f, &registry);
            if (player.state.velocity.y < 0.0f) fell = true;
        }
        REQUIRE(fell);
        REQUIRE(player.state.onGround);
        REQUIRE(player.state.position.y == Catch::Approx(startY).margin(0.08f));
    }

    SECTION("Water below unloaded chunks never reports submersion") {
        voxels::World emptyWorld;
        voxels::Player player;
        player.state.position = voxels::Vec3{500.5f, 500.0f, 500.5f};
        const voxels::gameplay::SubmersionInfo submersion =
            voxels::gameplay::Physics::SampleSubmersion(emptyWorld, player, &registry);
        REQUIRE_FALSE(submersion.feetSubmerged);
        REQUIRE_FALSE(submersion.eyeSubmerged);
        REQUIRE(submersion.bodyFraction == Catch::Approx(0.0f));
    }
}

TEST_CASE("BlockInteraction.BreakAndPlace", "[gameplay][block_interaction]") {
    voxels::World world;
    world.SetBlock(voxels::Vec3I{3, 2, -1}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock(voxels::Vec3I{3, 2, -3}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();

    voxels::Player player;
    player.state.position = voxels::Vec3{3.5f, 2.0f, 1.5f};
    player.state.yaw = 0.0f;
    player.state.pitch = 0.0f;
    player.state.inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 4};

    const voxels::gameplay::BlockInteraction interaction;
    const voxels::gameplay::InteractionResult breakResult =
        interaction.BreakBlock(world, interaction.Target(world, player, registry), registry);
    REQUIRE(breakResult.success);
    REQUIRE(world.GetBlock(voxels::Vec3I{3, 2, -1}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));

    const voxels::gameplay::InteractionResult placeResult =
        interaction.PlaceBlock(world, player, interaction.Target(world, player, registry), registry);
    REQUIRE(placeResult.success);
    REQUIRE(world.GetBlock(voxels::Vec3I{3, 2, -2}) == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));

    const voxels::gameplay::InteractionResult rejected = interaction.PlaceBlock(world, player, {}, registry);
    REQUIRE_FALSE(rejected.success);
}

TEST_CASE("BlockInteraction.RejectsPlacingNonPlaceableItems", "[gameplay][block_interaction]") {
    voxels::World world;
    world.SetBlock(voxels::Vec3I{3, 2, -1}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();

    voxels::Player player;
    player.state.position = voxels::Vec3{3.5f, 2.0f, 1.5f};
    player.state.yaw = 0.0f;
    player.state.pitch = 0.0f;
    const voxels::BlockDefinition* coal = registry.GetDefinition("coal");
    REQUIRE(coal != nullptr);
    player.state.inventory.GetSlot(0) = {coal->id, 4};

    const voxels::gameplay::BlockInteraction interaction;
    const voxels::gameplay::InteractionResult placeResult =
        interaction.PlaceBlock(world, player, interaction.Target(world, player, registry), registry);
    REQUIRE_FALSE(placeResult.success);
}

TEST_CASE("CameraController.UpdatesMovementFromInput", "[gameplay][camera]") {
    voxels::GamePreferences preferences;
    preferences.mouseSensitivity = 0.5f;
    preferences.invertY = true;

    voxels::Player player;
    player.state.position = voxels::Vec3{0.0f, 2.0f, 0.0f};
    player.state.yaw = 0.0f;
    player.state.pitch = 0.0f;

    voxels::gameplay::CameraController controller;
    voxels::InputState input;
    input.moveForward = true;
    input.moveRight = true;
    input.jump = true;
    input.mouseX = 12.0f;
    input.mouseY = -8.0f;

    controller.Update(player, input, preferences, 1.0f / 60.0f);

    REQUIRE(player.state.yaw != Catch::Approx(0.0f));
    REQUIRE(player.state.pitch != Catch::Approx(0.0f));
    REQUIRE(player.state.velocity.x != Catch::Approx(0.0f));
    REQUIRE(player.state.velocity.z != Catch::Approx(0.0f));
    REQUIRE(player.state.velocity.y == Catch::Approx(0.0f));
}

TEST_CASE("CameraController.YawWrapsWithoutChangingTurnDirection", "[gameplay][camera]") {
    voxels::GamePreferences preferences;
    voxels::Player player;
    player.state.yaw = -3.13f;

    voxels::InputState input;
    input.mouseX = 20.0f;

    voxels::gameplay::CameraController controller;
    controller.Update(player, input, preferences, 1.0f / 60.0f);

    REQUIRE(player.state.yaw > 3.0f);
    REQUIRE(player.state.yaw <= 3.141593f);
}
