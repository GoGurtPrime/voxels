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

TEST_CASE("Physics.WalkingIntoShallowWaterWhileHoldingSpaceSinksInsteadOfHovering", "[gameplay][physics][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    // Dry shore floor at y=0 (top=1); the water side is a basin with no floor near the surface -
    // stepping off the shore edge must sink the player in, not hold them at the dry walking height.
    for (int x = -5; x <= 5; ++x) {
        for (int z = -1; z <= 1; ++z) {
            if (x < 0) {
                world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            } else {
                world.SetBlock(voxels::Vec3I{x, -5, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
                for (int y = -4; y <= 1; ++y) {
                    world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
                }
            }
        }
    }

    voxels::Player player;
    player.state.position = voxels::Vec3{-1.5f, 1.9f, 0.5f}; // standing on dry ground
    player.state.velocity = voxels::Vec3{0.0f};
    player.state.onGround = true;

    const float startY = player.state.position.y;
    bool everSankBelowStart = false;
    for (int step = 0; step < 300; ++step) {
        player.state.velocity.x = 2.0f; // walk forward into the water
        voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
        if (player.state.position.x > 0.0f && player.state.position.y < startY - 0.05f) {
            everSankBelowStart = true;
        }
    }

    REQUIRE(everSankBelowStart); // must actually sink in, not hover at the dry walking height
    REQUIRE(player.state.position.y < startY); // settles lower than the dry shore, not on top of the water
}

TEST_CASE("Physics.SwimAscendTreadsAtTheSurfaceInsteadOfWalkingOnTopOfOpenWater", "[gameplay][physics][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    // A wide open pool: no shore within drifting range, so nothing to climb onto. Wide enough
    // that ~5 seconds of horizontal drift never reaches an edge.
    for (int x = -30; x <= 30; ++x) {
        for (int z = -30; z <= 30; ++z) {
            world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            for (int y = 1; y <= 6; ++y) {
                world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
            }
        }
    }

    voxels::Player player;
    player.state.position = voxels::Vec3{0.5f, 2.5f, 0.5f};
    player.state.velocity = voxels::Vec3{0.0f};

    float maxHeightReached = player.state.position.y;
    for (int step = 0; step < 300; ++step) {
        player.state.velocity.x = 2.0f; // simulate holding a movement key while treading water
        voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
        maxHeightReached = std::max(maxHeightReached, player.state.position.y);
    }

    const voxels::gameplay::SubmersionInfo finalSubmersion =
        voxels::gameplay::Physics::SampleSubmersion(world, player, &registry);
    REQUIRE_FALSE(player.state.onGround);
    REQUIRE(maxHeightReached > 6.0f); // rises close to the tread ceiling (surface=7, ceiling~6.38)
    REQUIRE(maxHeightReached < 6.6f); // ...but never reaches/passes the y=7 open-air surface
    REQUIRE(finalSubmersion.bodyFraction > 0.5f); // stays mostly submerged, not floating on top
}

TEST_CASE("Physics.SwimClimbsOutOntoAOneBlockLedgeButNotATwoBlockWall", "[gameplay][physics][swimming]") {
    const auto buildWorld = [](int ledgeHeightAboveSurface) {
        voxels::World world;
        // Water pool for x <= 0; dry ground for x >= 1, its top `ledgeHeightAboveSurface`
        // blocks above the water surface (y=3 is the first air cell above the water).
        for (int z = -1; z <= 1; ++z) {
            for (int x = -3; x <= 0; ++x) {
                world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
                for (int y = 1; y <= 2; ++y) {
                    world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
                }
            }
            for (int x = 1; x <= 3; ++x) {
                for (int y = 0; y < 3 + ledgeHeightAboveSurface; ++y) {
                    world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
                }
            }
        }
        return world;
    };
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();

    SECTION("A single-block-high shore is climbable while swimming towards it") {
        voxels::World world = buildWorld(1);
        voxels::Player player;
        player.state.position = voxels::Vec3{-1.5f, 2.0f, 0.5f};
        player.state.velocity = voxels::Vec3{0.0f};

        bool climbedOut = false;
        float maxSingleTickRise = 0.0f;
        for (int step = 0; step < 500; ++step) {
            player.state.velocity.x = 2.0f;
            const float yBefore = player.state.position.y;
            voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
            maxSingleTickRise = std::max(maxSingleTickRise, player.state.position.y - yBefore);
            if (player.state.onGround && player.state.position.x > 1.0f) {
                climbedOut = true;
                break;
            }
        }
        REQUIRE(climbedOut);
        // The final unstick-onto-the-ledge correction is bounded to a small residual gap, not a
        // multi-block teleport - the bulk of the climb happens gradually.
        REQUIRE(maxSingleTickRise < 0.6f);
    }

    SECTION("A two-block-high wall cannot be swim-climbed") {
        voxels::World world = buildWorld(2);
        voxels::Player player;
        player.state.position = voxels::Vec3{-1.5f, 2.0f, 0.5f};
        player.state.velocity = voxels::Vec3{0.0f};

        for (int step = 0; step < 500; ++step) {
            player.state.velocity.x = 2.0f;
            voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
        }
        REQUIRE_FALSE(player.state.onGround);
        REQUIRE(player.state.position.x < 1.0f);
    }

    SECTION("A shoreline flush with the water surface is climbable") {
        voxels::World world = buildWorld(0); // land top == water surface height, no step at all
        voxels::Player player;
        player.state.position = voxels::Vec3{-1.5f, 2.0f, 0.5f};
        player.state.velocity = voxels::Vec3{0.0f};

        bool climbedOut = false;
        for (int step = 0; step < 500; ++step) {
            player.state.velocity.x = 2.0f;
            voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
            if (player.state.onGround && player.state.position.x > 1.0f) {
                climbedOut = true;
                break;
            }
        }
        REQUIRE(climbedOut);
    }

    SECTION("Breaking the top block of a two-block wall opens a climbable one-block step") {
        voxels::World world = buildWorld(2);
        for (int z = -1; z <= 1; ++z) {
            for (int x = 1; x <= 3; ++x) {
                world.SetBlock(voxels::Vec3I{x, 4, z}, static_cast<voxels::BlockId>(voxels::BlockType::Air));
            }
        }
        voxels::Player player;
        player.state.position = voxels::Vec3{-1.5f, 2.0f, 0.5f};
        player.state.velocity = voxels::Vec3{0.0f};

        bool climbedOut = false;
        for (int step = 0; step < 500; ++step) {
            player.state.velocity.x = 2.0f;
            voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
            if (player.state.onGround && player.state.position.x > 1.0f) {
                climbedOut = true;
                break;
            }
        }
        REQUIRE(climbedOut);
    }
}

TEST_CASE("Physics.FallingIntoDeepWaterSinksNaturallyWithoutTeleportingToTheSurfaceEquilibrium",
         "[gameplay][physics][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    for (int x = -3; x <= 3; ++x) {
        for (int z = -3; z <= 3; ++z) {
            world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            for (int y = 1; y <= 10; ++y) {
                world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
            }
        }
    }
    // Surface is at y=11; the tread equilibrium sits well below it (~9.8), so a player who just
    // touched the surface from above starts far above that equilibrium.

    voxels::Player player;
    player.state.position = voxels::Vec3{0.5f, 11.1f, 0.5f}; // feet just touching the surface
    player.state.velocity = voxels::Vec3{0.0f, -25.0f, 0.0f}; // falling fast, as if from a height

    const float positionBeforeEntry = player.state.position.y;
    voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
    const float singleTickDrop = positionBeforeEntry - player.state.position.y;

    // A natural sink (bounded by kWaterTerminalVelocity) moves at most ~0.06 blocks in one 1/60s
    // tick; a forced snap straight down to the tread equilibrium would move roughly 1.9 blocks in
    // that same single tick.
    REQUIRE(singleTickDrop < 0.3f);
    REQUIRE(singleTickDrop >= 0.0f);
}

TEST_CASE("Physics.SwimmingCrossesASubmergedOneBlockBumpAnywhereUnderwater", "[gameplay][physics][swimming]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    for (int x = -3; x <= 5; ++x) {
        for (int z = -1; z <= 1; ++z) {
            world.SetBlock(voxels::Vec3I{x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
            for (int y = 1; y <= 2; ++y) {
                world.SetBlock(voxels::Vec3I{x, y, z}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
            }
        }
    }
    // A one-block bump on the sea floor directly ahead, fully submerged (surface is at y=3).
    for (int z = -1; z <= 1; ++z) {
        world.SetBlock(voxels::Vec3I{2, 1, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    }

    voxels::Player player;
    player.state.position = voxels::Vec3{0.5f, 2.0f, 0.5f};
    player.state.velocity = voxels::Vec3{0.0f};

    bool crossedBump = false;
    float maxSingleTickRise = 0.0f;
    for (int step = 0; step < 300; ++step) {
        player.state.velocity.x = 2.0f;
        const float yBefore = player.state.position.y;
        voxels::gameplay::Physics::Step(world, player, 1.0f / 60.0f, &registry, /*swimAscend=*/true);
        maxSingleTickRise = std::max(maxSingleTickRise, player.state.position.y - yBefore);
        if (player.state.position.x > 2.5f) {
            crossedBump = true;
            break;
        }
    }

    REQUIRE(crossedBump);
    REQUIRE(maxSingleTickRise < 0.6f);
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
