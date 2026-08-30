/**
 * @file test_block_interaction.cpp
 * @brief Side-effect tests for the playable block interaction loop.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voxels/gameplay/block_interaction.hpp"
#include "voxels/gameplay/inventory.hpp"
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
    voxels::Player player;
    player.state.position = {2.5f, 1.28f, 0.5f};
    player.state.yaw = 0.0f;
    player.state.inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 2};
    const voxels::gameplay::BlockInteraction interaction;
    const auto broken = interaction.BreakBlock(world, player, 5.0f);
    REQUIRE(broken.success);
    REQUIRE(world.GetBlock(broken.targetPosition) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
    const auto placed = interaction.PlaceBlock(world, player, 5.0f);
    REQUIRE(placed.success);
    REQUIRE(world.GetBlock(placed.adjacentPosition) == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
}