/**
 * @file test_item_drop.cpp
 * @brief Ground item-drop physics/pickup, break-drop spawning, and manual-drop behaviour.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voxels/app/game_session.hpp"
#include "voxels/gameplay/item_drop.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/world.hpp"

TEST_CASE("ItemDropSimulation.FallsAndRestsOnSolidGround", "[gameplay][item_drop]") {
    voxels::World world;
    world.SetBlock({0, 0, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();

    voxels::gameplay::ItemDropSimulation drops;
    const auto id = drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 3}, {0.5f, 5.0f, 0.5f}, {0.0f, 0.0f, 0.0f});
    REQUIRE(id != 0);

    for (int step = 0; step < 300; ++step) {
        drops.Update(world, &registry, 1.0f / 60.0f);
    }

    REQUIRE(drops.Drops().size() == 1);
    const auto& drop = drops.Drops().front();
    REQUIRE(drop.grounded);
    REQUIRE(drop.position.y == Catch::Approx(1.18f).margin(0.02f));
    REQUIRE(drop.velocity.y == Catch::Approx(0.0f));
}

TEST_CASE("ItemDropSimulation.CollectPickupsOnlyWithinRadiusAndAfterDelay", "[gameplay][item_drop]") {
    voxels::World world;
    world.SetBlock({0, -1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Stone), 1}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.5f);

    // Within radius, but the pickup delay has not elapsed yet.
    auto collected = drops.CollectPickups({0.1f, 0.0f, 0.0f});
    REQUIRE(collected.empty());
    REQUIRE(drops.Drops().size() == 1);

    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    // Sub-step like a real per-frame simulation so the item settles on the floor instead of
    // tunnelling through it in one unrealistically large Euler step.
    for (int step = 0; step < 36; ++step) {
        drops.Update(world, &registry, 1.0f / 60.0f);
    }

    // Too far away even after the delay elapses.
    collected = drops.CollectPickups({10.0f, 0.0f, 0.0f});
    REQUIRE(collected.empty());
    REQUIRE(drops.Drops().size() == 1);

    collected = drops.CollectPickups({0.2f, 0.0f, 0.0f});
    REQUIRE(collected.size() == 1);
    REQUIRE(collected.front().count == 1);
    REQUIRE(drops.Drops().empty());
}

TEST_CASE("ItemDropSimulation.DespawnsAfterFiveMinutes", "[gameplay][item_drop]") {
    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Stone), 1}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    voxels::World world;
    drops.Update(world, nullptr, voxels::gameplay::ItemDropSimulation::kDespawnSeconds + 0.1f);
    REQUIRE(drops.Drops().empty());
}

TEST_CASE("GameSession.BreakingCoalOreSpawnsAndCollectsADistinctCoalItem", "[gameplay][item_drop][game_session]") {
    voxels::World world;
    for (int x = -2; x <= 2; ++x) {
        for (int z = -3; z <= 1; ++z) {
            world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        }
    }
    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::CoalOre));

    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    const auto* coalOre = registry.GetDefinition("coal_ore");
    const auto* coal = registry.GetDefinition("coal");
    REQUIRE(coalOre != nullptr);
    REQUIRE(coal != nullptr);
    REQUIRE(coalOre->id != coal->id);

    voxels::InputManager input;
    input.BindAction("DestroyBlock", {"DestroyBlock", 1, 0, voxels::InputDeviceType::Mouse});

    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetInputManager(&input);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 0.5f});
    session.Initialize();

    input.InjectMouseButtonEvent(1, true);
    // Sub-step like the real frame loop (hardness 3.0s): a single multi-second Update would
    // also tunnel the freshly spawned ground drop through the floor in that same giant step.
    for (int step = 0; step < 200 && world.GetBlock({0, 2, -2}) != static_cast<voxels::BlockId>(voxels::BlockType::Air); ++step) {
        session.Update(1.0f / 60.0f);
    }
    REQUIRE(world.GetBlock({0, 2, -2}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));

    // The break spawned a ground drop rather than an instant inventory grant.
    bool foundCoalDrop = false;
    for (const auto& drop : session.GetItemDrops()) {
        if (drop.stack.blockId == coal->id) foundCoalDrop = true;
    }
    REQUIRE(foundCoalDrop);

    // Standing near the break site, subsequent updates let the drop fall and be auto-collected.
    // The break happened a few blocks away from spawn, so teleport the player next to the
    // ground drop the same way a walking player would arrive, then let physics settle it.
    session.GetPlayer().state.position = voxels::Vec3{0.5f, 1.9f, -1.5f};
    for (int step = 0; step < 240 && session.GetItemDrops().size() > 0; ++step) {
        input.InjectMouseButtonEvent(1, false);
        session.Update(1.0f / 60.0f);
    }
    REQUIRE(session.GetItemDrops().empty());

    int coalCount = 0;
    for (std::size_t slot = 0; slot < voxels::gameplay::Inventory::kSlotCount; ++slot) {
        const auto& stack = session.GetPlayer().state.inventory.GetSlot(slot);
        if (stack.blockId == coal->id) coalCount += stack.count;
    }
    REQUIRE(coalCount == 1);
    session.Shutdown();
}

TEST_CASE("GameSession.DropInventorySlotTossesItemsWithAShortSelfPickupDelay", "[gameplay][item_drop][game_session]") {
    voxels::World world;
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        }
    }
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 0.5f});
    session.Initialize();
    session.GetPlayer().state.inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 5};
    session.GetPlayer().state.inventory.SetSelectedSlot(0);

    const int dropped = session.DropInventorySlot(0, 5);
    REQUIRE(dropped == 5);
    REQUIRE(session.GetPlayer().state.inventory.GetSlot(0).IsEmpty());
    REQUIRE(session.GetItemDrops().size() == 1);

    // Immediately after dropping, the item is not yet eligible for pickup even though the
    // player is standing right where it landed.
    session.Update(1.0f / 60.0f);
    REQUIRE(session.GetItemDrops().size() == 1);

    // After the pickup delay elapses the item is available to collect again.
    for (int step = 0; step < 180 && !session.GetItemDrops().empty(); ++step) {
        session.Update(1.0f / 60.0f);
    }
    REQUIRE(session.GetItemDrops().empty());
    REQUIRE(session.GetPlayer().state.inventory.GetSlot(0).blockId == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    session.Shutdown();
}
