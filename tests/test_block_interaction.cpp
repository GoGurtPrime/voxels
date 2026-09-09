/**
 * @file test_block_interaction.cpp
 * @brief Side-effect tests for the playable block interaction loop.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voxels/app/game_session.hpp"
#include "voxels/gameplay/block_interaction.hpp"
#include "voxels/gameplay/inventory.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/render/chunk_renderer.hpp"

TEST_CASE("Inventory.StacksMergeAndOverflow", "[interaction][inventory]") {
    voxels::gameplay::Inventory inventory;
    inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Stone), 60};
    REQUIRE(inventory.AddItem(static_cast<voxels::BlockId>(voxels::BlockType::Stone), 10) == 0);
    REQUIRE(inventory.GetSlot(0).count == 64);
    REQUIRE(inventory.GetSlot(1).blockId == static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    REQUIRE(inventory.GetSlot(1).count == 6);
}

TEST_CASE("Inventory.HotbarSelectionWraps", "[interaction][inventory]") {
    voxels::gameplay::Inventory inventory;
    inventory.CycleSelectedSlot(-1);
    REQUIRE(inventory.GetSelectedSlot() == 8);
    inventory.CycleSelectedSlot(2);
    REQUIRE(inventory.GetSelectedSlot() == 1);
    inventory.SetSelectedSlot(99);
    REQUIRE(inventory.GetSelectedSlot() == 8);
}

TEST_CASE("Raycast.HitsNearestSolidAndReportsFace", "[interaction][raycast]") {
    voxels::World world;
    world.SetBlock({2, 1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({4, 1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    const auto hit = world.Raycast({0.5f, 1.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 5.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.blockPosition == voxels::Vec3I{2, 1, 0});
    REQUIRE(hit.face == voxels::Face::NegX);
    REQUIRE(hit.distance == Catch::Approx(1.5f));
    REQUIRE_FALSE(world.Raycast({0.5f, 1.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 1.49f).hit);
}

TEST_CASE("Raycast.SkipsNonTargetableLiquids", "[interaction][raycast]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    world.SetBlock({1, 2, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
    world.SetBlock({2, 2, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::Player player;
    player.state.position = {0.5f, 1.28f, 0.5f};
    player.state.yaw = -1.5707963f;
    const voxels::gameplay::BlockInteraction interaction;
    const auto hit = interaction.Target(world, player, registry);
    REQUIRE(hit.hit);
    REQUIRE(hit.blockPosition == voxels::Vec3I{2, 2, 0});
}

TEST_CASE("BlockInteraction.BreakAndPlaceMutateWorld", "[interaction][break][place]") {
    voxels::World world;
    world.SetBlock({2, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({2, 2, -4}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::Player player;
    player.state.position = {2.5f, 1.28f, 0.5f};
    player.state.yaw = 0.0f;
    player.state.inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 2};
    const voxels::gameplay::BlockInteraction interaction;
    const auto broken = interaction.BreakBlock(world, interaction.Target(world, player, registry), registry);
    REQUIRE(broken.success);
    REQUIRE(world.GetBlock(broken.targetPosition) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
    const auto placed = interaction.PlaceBlock(world, player, interaction.Target(world, player, registry), registry);
    REQUIRE(placed.success);
    REQUIRE(world.GetBlock(placed.adjacentPosition) == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
}

TEST_CASE("BlockInteraction.BreakUsesTheLiquidSkippingTarget", "[interaction][break][liquid]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    world.SetBlock({0, 2, -1}, static_cast<voxels::BlockId>(voxels::BlockType::Water));
    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    voxels::Player player;
    player.state.position = {0.5f, 1.28f, 0.5f};
    player.state.yaw = 0.0f;
    const voxels::gameplay::BlockInteraction interaction;
    const voxels::RaycastHit target = interaction.Target(world, player, registry);

    REQUIRE(target.blockPosition == voxels::Vec3I{0, 2, -2});
    REQUIRE(interaction.BreakBlock(world, target, registry).success);
    REQUIRE(world.GetBlock({0, 2, -1}) == static_cast<voxels::BlockId>(voxels::BlockType::Water));
    REQUIRE(world.GetBlock({0, 2, -2}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
}

TEST_CASE("BlockInteraction.RejectsUnbreakableBlocks", "[interaction][break]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Bedrock));

    voxels::Player player;
    player.state.position = {0.5f, 1.28f, 0.5f};
    player.state.yaw = 0.0f;
    const voxels::gameplay::BlockInteraction interaction;
    const voxels::RaycastHit target = interaction.Target(world, player, registry);

    REQUIRE_FALSE(interaction.BreakBlock(world, target, registry).success);
    REQUIRE(world.GetBlock({0, 2, -2}) == static_cast<voxels::BlockId>(voxels::BlockType::Bedrock));
}

TEST_CASE("GameSession.BreakProgressResetsForTargetChangesAndRelease", "[interaction][progress]") {
    voxels::World world;
    for (int x = -1; x <= 1; ++x) {
        for (int z = -3; z <= 1; ++z) {
            world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        }
    }
    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::InputManager input;
    input.BindAction("DestroyBlock", {"DestroyBlock", 1, 0, voxels::InputDeviceType::Mouse});

    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetInputManager(&input);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 0.5f});
    session.Initialize();

    input.InjectMouseButtonEvent(1, true);
    session.Update(0.5f);
    REQUIRE(session.GetBreakProgress() == Catch::Approx(1.0f / 3.0f));

    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    session.Update(0.1f);
    REQUIRE(session.GetBreakProgress() == Catch::Approx(0.2f));

    input.InjectMouseButtonEvent(1, false);
    session.Update(0.1f);
    REQUIRE(session.GetBreakProgress() == 0.0f);
    session.Shutdown();
}