/**
 * @file test_item_drop.cpp
 * @brief Ground item-drop physics/pickup, break-drop spawning, and manual-drop behaviour.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

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

TEST_CASE("ItemDropSimulation.SweptCollisionStopsAtWalls", "[gameplay][item_drop][collision]") {
    voxels::World world;
    world.SetBlock({1, 1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();

    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1},
                {0.5f, 1.5f, 0.5f}, {30.0f, 0.0f, 0.0f});
    drops.Update(world, &registry, 1.0f / 20.0f);

    REQUIRE(drops.Drops().size() == 1);
    REQUIRE(drops.Drops().front().position.x <= Catch::Approx(1.0f - voxels::gameplay::ItemDropSimulation::kDropRadius).margin(0.001f));
    REQUIRE(drops.Drops().front().velocity.x == Catch::Approx(0.0f));
}

TEST_CASE("ItemDropSimulation.CollectPickupsOnlyWithinRadiusAndAfterDelay", "[gameplay][item_drop]") {
    voxels::World world;
    world.SetBlock({0, 0, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Stone), 1}, {0.5f, 1.18f, 0.5f}, {0.0f, 0.0f, 0.0f}, 0.5f);

    // Within radius, but the pickup delay has not elapsed yet.
    voxels::gameplay::Inventory inventory;
    auto collected = drops.CollectPickups({0.6f, 1.18f, 0.5f}, inventory);
    REQUIRE(collected.empty());
    REQUIRE(drops.Drops().size() == 1);

    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    // Sub-step like a real per-frame simulation so the item settles on the floor instead of
    // tunnelling through it in one unrealistically large Euler step.
    for (int step = 0; step < 36; ++step) {
        drops.Update(world, &registry, 1.0f / 60.0f);
    }

    // Too far away even after the delay elapses.
    collected = drops.CollectPickups({10.0f, 0.0f, 0.0f}, inventory);
    REQUIRE(collected.empty());
    REQUIRE(drops.Drops().size() == 1);

    collected = drops.CollectPickups({0.6f, 1.18f, 0.5f}, inventory);
    REQUIRE(collected.size() == 1);
    REQUIRE(collected.front().count == 1);
    REQUIRE(inventory.GetSlot(0).count == 1);
    REQUIRE(drops.Drops().empty());
}

TEST_CASE("ItemDropSimulation.CollidesWithCeilingsAndCorners", "[gameplay][item_drop][collision]") {
    voxels::World world;
    world.SetBlock({0, 2, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({1, 1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({0, 1, 1}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1},
                {0.5f, 1.5f, 0.5f}, {30.0f, 30.0f, 30.0f});

    drops.Update(world, &registry, 1.0f / 20.0f);

    REQUIRE(drops.Drops().front().position.x < 1.0f);
    REQUIRE(drops.Drops().front().position.y < 2.0f);
    REQUIRE(drops.Drops().front().position.z < 1.0f);
    REQUIRE(drops.Drops().front().velocity.x == Catch::Approx(0.0f));
    REQUIRE(drops.Drops().front().velocity.y == Catch::Approx(0.0f));
    REQUIRE(drops.Drops().front().velocity.z == Catch::Approx(0.0f));
}

TEST_CASE("ItemDropSimulation.RecoversFromNewlyPlacedBlocksAndDespawnsWithoutASurface",
          "[gameplay][item_drop][recovery]") {
    voxels::World world;
    world.SetBlock({0, 0, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1},
                {0.75f, 1.18f, 0.5f}, {});
    world.SetBlock({0, 1, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    drops.ResolveAfterBlockEdit(world, &registry, {0, 1, 0});
    drops.Update(world, &registry, 1.0f / 60.0f);

    REQUIRE(drops.Drops().size() == 1);
    REQUIRE(drops.Drops().front().position.x > 1.18f);
    REQUIRE(drops.Drops().front().velocity.x > 0.0f);
    REQUIRE(drops.GetMetrics().recoveredDrops == 1);

    voxels::gameplay::ItemDropSimulation lostDrops;
    lostDrops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1},
                    {20.5f, -10.0f, 20.5f}, {});
    lostDrops.Update(world, &registry, 1.0f / 60.0f);
    REQUIRE(lostDrops.Drops().empty());
    REQUIRE(lostDrops.GetMetrics().despawnedDrops == 1);
}

TEST_CASE("ItemDropSimulation.MagnetStrengthIncreasesWithProximityAfterDelay",
          "[gameplay][item_drop][magnet]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1}, {2.0f, 4.0f, 0.0f}, {}, 0.5f);
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1}, {7.0f, 4.0f, 0.0f}, {});
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1}, {2.0f, 4.0f, 0.0f}, {});
    const voxels::Vec3 player{0.0f, 4.0f, 0.0f};

    drops.Update(world, &registry, 1.0f / 60.0f, &player);

    REQUIRE(drops.Drops()[0].velocity.x == Catch::Approx(0.0f));
    REQUIRE(drops.Drops()[1].velocity.x < 0.0f);
    REQUIRE(drops.Drops()[2].velocity.x < 0.0f);
    REQUIRE(std::abs(drops.Drops()[2].velocity.x) > std::abs(drops.Drops()[1].velocity.x));
}

TEST_CASE("ItemDropSimulation.FullInventoryRetainsOriginalDropWithoutDuplication",
          "[gameplay][item_drop][inventory]") {
    voxels::gameplay::Inventory inventory;
    for (auto& slot : inventory.Slots()) {
        slot = {static_cast<voxels::BlockId>(voxels::BlockType::Stone), voxels::gameplay::Inventory::kStackLimit};
    }
    voxels::gameplay::ItemDropSimulation drops;
    const std::uint32_t dropId = drops.Spawn(
        {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 7}, {0.0f, 1.0f, 0.0f}, {});

    REQUIRE(drops.CollectPickups({0.0f, 1.0f, 0.0f}, inventory).empty());
    REQUIRE(drops.Drops().size() == 1);
    REQUIRE(drops.Drops().front().id == dropId);
    REQUIRE(drops.Drops().front().stack.count == 7);

    inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 62};
    const auto partialPickup = drops.CollectPickups({0.0f, 1.0f, 0.0f}, inventory);
    REQUIRE(partialPickup.size() == 1);
    REQUIRE(partialPickup.front().count == 2);
    REQUIRE(inventory.GetSlot(0).count == 64);
    REQUIRE(drops.Drops().front().id == dropId);
    REQUIRE(drops.Drops().front().stack.count == 5);
}

TEST_CASE("ItemDropSimulation.DespawnsAfterFiveMinutes", "[gameplay][item_drop]") {
    voxels::gameplay::ItemDropSimulation drops;
    drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Stone), 1}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    voxels::World world;
    drops.Update(world, nullptr, voxels::gameplay::ItemDropSimulation::kDespawnSeconds + 0.1f);
    REQUIRE(drops.Drops().empty());
}

TEST_CASE("ItemDropSimulation.UpdatesOneThousandDropsWithinBudget", "[gameplay][item_drop][performance]") {
    voxels::World world;
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::ItemDropSimulation drops;
    for (int index = 0; index < 1000; ++index) {
        const int x = 10 + index % 100;
        const int z = 10 + index / 100;
        world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        drops.Spawn({static_cast<voxels::BlockId>(voxels::BlockType::Stone), 1},
                    {static_cast<float>(x) + 0.5f, 1.18f, static_cast<float>(z) + 0.5f}, {});
    }
    const voxels::Vec3 player{0.0f, 10.0f, 0.0f};

    drops.Update(world, &registry, 1.0f / 60.0f, &player);
    drops.Update(world, &registry, 1.0f / 60.0f, &player);

    CAPTURE(drops.GetMetrics().updateMicroseconds);
    REQUIRE(drops.GetMetrics().activeDrops == 1000);
    REQUIRE(drops.GetMetrics().updateMicroseconds <= 350.0);
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

TEST_CASE("GameSession.ShutdownCannotCarryDropsIntoAnotherWorld", "[gameplay][item_drop][game_session]") {
    voxels::World firstWorld;
    voxels::GameSession session(&firstWorld);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 0.5f});
    session.Initialize();
    session.GetPlayer().state.inventory.GetSlot(0) = {
        static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 1};
    REQUIRE(session.DropInventorySlot(0, 1) == 1);
    REQUIRE(session.GetItemDrops().size() == 1);

    session.Shutdown();

    REQUIRE(session.GetItemDrops().empty());
    REQUIRE(session.GetItemDropMetrics().activeDrops == 0);
}
