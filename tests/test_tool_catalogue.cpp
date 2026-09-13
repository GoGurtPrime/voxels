/**
 * @file test_tool_catalogue.cpp
 * @brief Content validation and lookup tests for durable tool definitions.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>

#include "voxels/gameplay/tool_catalogue.hpp"
#include "voxels/app/game_session.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/render/held_item_renderer.hpp"
#include "voxels/render/texture_forge.hpp"

namespace {

voxels::BlockRegistry MakeRegistry() {
    voxels::BlockRegistry registry;
    voxels::BlockDefinition air;
    air.type = voxels::BlockType::Air;
    air.id = 0;
    air.name = "air";
    air.displayName = "Air";
    air.isSolid = false;
    air.isOpaque = false;
    air.isTransparent = true;
    air.isPlaceable = false;
    registry.RegisterBlock(std::move(air));

    voxels::BlockDefinition stone;
    stone.type = voxels::BlockType::Stone;
    stone.id = 1;
    stone.name = "stone";
    stone.displayName = "Stone";
    registry.RegisterBlock(std::move(stone));

    voxels::BlockDefinition pickaxe;
    pickaxe.type = static_cast<voxels::BlockType>(2);
    pickaxe.id = 2;
    pickaxe.name = "stone_pickaxe";
    pickaxe.displayName = "Stone Pickaxe";
    pickaxe.isSolid = false;
    pickaxe.isOpaque = false;
    pickaxe.isTransparent = true;
    pickaxe.isPlaceable = false;
    registry.RegisterBlock(std::move(pickaxe));
    return registry;
}

constexpr const char* kToolsJson = R"({"tools":[{"id":"stone_pickaxe","item":"stone_pickaxe","kind":"pickaxe","tier":"stone","max_durability":132,"base_damage":3,"swing_interval_seconds":0.55,"mining_speed_multipliers":{"stone":3.0,"dirt":0.8}}]})";

std::filesystem::path ShippedToolsPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "app" / "assets" / "data" / "tools.json";
}

} // namespace

TEST_CASE("ToolCatalogue.ResolvesValidatedToolItemsAndTags", "[tool][catalogue]") {
    const voxels::BlockRegistry registry = MakeRegistry();
    voxels::gameplay::ToolCatalogue catalogue;
    catalogue.LoadFromJsonString(kToolsJson, "tools.json", registry);

    const auto* tool = catalogue.FindByItem(2);
    REQUIRE(tool != nullptr);
    REQUIRE(tool->kind == voxels::gameplay::ToolKind::Pickaxe);
    REQUIRE(tool->maxDurability == 132);
    REQUIRE(catalogue.MiningSpeedMultiplier(*tool, "stone") == Catch::Approx(3.0f));
    REQUIRE(catalogue.MiningSpeedMultiplier(*tool, "wood") == Catch::Approx(1.0f));

    voxels::gameplay::ItemStack stack{2, 1};
    REQUIRE(catalogue.CurrentDurability(stack, *tool) == 132);
    REQUIRE(catalogue.ConsumeDurability(stack, *tool));
    REQUIRE(stack.durability == 131);
    stack.durability = 1;
    REQUIRE_FALSE(catalogue.ConsumeDurability(stack, *tool));
    REQUIRE(stack.IsEmpty());
}

TEST_CASE("ToolCatalogue.NamesOffendingToolAndField", "[tool][catalogue]") {
    const voxels::BlockRegistry registry = MakeRegistry();
    voxels::gameplay::ToolCatalogue catalogue;
    try {
        catalogue.LoadFromJsonString(
            R"({"tools":[{"id":"bad_pick","item":"missing","kind":"pickaxe","tier":"stone","max_durability":1,"base_damage":1,"swing_interval_seconds":1,"mining_speed_multipliers":{"stone":1}}]})",
            "invalid-tools.json", registry);
        FAIL("Expected invalid tool content to throw.");
    } catch (const std::runtime_error& exception) {
        REQUIRE(std::string(exception.what()).find("tool 'bad_pick' field 'item'") != std::string::npos);
    }
}

TEST_CASE("ToolCatalogue.LoadsEveryShippedToolFamilyAndTier", "[tool][catalogue][assets]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::ToolCatalogue catalogue;
    catalogue.LoadFromFile(ShippedToolsPath(), registry);

    REQUIRE(catalogue.GetTools().size() == 12);
    for (const char* id : {"stone_axe", "iron_pickaxe", "stone_shovel", "iron_sledgehammer", "stone_sword", "iron_hoe"}) {
        const voxels::BlockDefinition* definition = registry.GetDefinition(id);
        REQUIRE(definition != nullptr);
        REQUIRE(catalogue.FindByItem(definition->id) != nullptr);
    }
}

TEST_CASE("GameSession.ToolsAccelerateMiningAndTillOnlyAfterSuccessfulMutation", "[tool][gameplay]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::ToolCatalogue catalogue;
    catalogue.LoadFromFile(ShippedToolsPath(), registry);
    const auto* pickaxe = registry.GetDefinition("stone_pickaxe");
    const auto* hoe = registry.GetDefinition("stone_hoe");
    const auto* tilledSoil = registry.GetDefinition("tilled_soil");
    REQUIRE(pickaxe != nullptr);
    REQUIRE(hoe != nullptr);
    REQUIRE(tilledSoil != nullptr);

    voxels::World world;
    for (int x = -1; x <= 1; ++x) {
        for (int z = -3; z <= 1; ++z) world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    }
    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    voxels::InputManager input;
    input.BindAction("DestroyBlock", {"DestroyBlock", 1, 0, voxels::InputDeviceType::Mouse});
    input.BindAction("PlaceBlock", {"PlaceBlock", 3, 0, voxels::InputDeviceType::Mouse});

    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetToolCatalogue(&catalogue);
    session.SetInputManager(&input);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 0.5f});
    session.Initialize();
    session.GetPlayer().state.inventory.GetSlot(0) = {pickaxe->id, 1};

    input.InjectMouseButtonEvent(1, true);
    session.Update(0.25f);
    REQUIRE(session.GetBreakProgress() == Catch::Approx(0.5f));
    session.Update(0.26f);
    REQUIRE(world.GetBlock({0, 2, -2}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
    REQUIRE(session.GetPlayer().state.inventory.GetSlot(0).durability == 131);
    input.InjectMouseButtonEvent(1, false);
    session.Update(0.01f);

    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    session.GetPlayer().state.inventory.GetSlot(0) = {hoe->id, 1};
    input.InjectMouseButtonEvent(3, true);
    session.Update(0.01f);
    REQUIRE(world.GetBlock({0, 2, -2}) == tilledSoil->id);
    REQUIRE(session.GetPlayer().state.inventory.GetSlot(0).durability == 131);
    session.Shutdown();
}

TEST_CASE("ToolContent.DefinesSolidAxeMiningAndTilledSoilMaterials", "[tool][assets]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::ToolCatalogue catalogue;
    catalogue.LoadFromFile(ShippedToolsPath(), registry);
    const auto* axe = registry.GetDefinition("stone_axe");
    const auto* tilled = registry.GetDefinition("tilled_soil");
    REQUIRE(axe != nullptr);
    REQUIRE(tilled != nullptr);
    const auto* tool = catalogue.FindByItem(axe->id);
    REQUIRE(tool != nullptr);
    REQUIRE(catalogue.MiningSpeedMultiplier(*tool, "stone") > 1.0f);
    REQUIRE(tilled->GetFaceTexture(voxels::Face::PosY) == "blocks/tilled_soil_top");
    REQUIRE(tilled->GetFaceTexture(voxels::Face::PosX) == "blocks/dirt");

    const voxels::ImageData top = voxels::TextureForge::GenerateTexture("blocks/tilled_soil_top", 7U);
    const voxels::ImageData dirt = voxels::TextureForge::GenerateTexture("blocks/dirt", 7U);
    REQUIRE(top.pixels != dirt.pixels);
    const std::size_t dryOffset = (static_cast<std::size_t>(4) * 16U + 4U) * 4U;
    const std::size_t wetOffset = (static_cast<std::size_t>(2) * 16U + 4U) * 4U;
    REQUIRE(top.pixels[wetOffset] < top.pixels[dryOffset]);
}

TEST_CASE("HeldItemRenderer.BuildsArmAndDistinctToolSilhouettes", "[tool][render]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas;
    atlas.RegisterTexture("blocks/dirt", voxels::TextureForge::GenerateTexture("dirt"));
    atlas.RegisterTexture("blocks/stone", voxels::TextureForge::GenerateTexture("stone"));
    REQUIRE(atlas.BuildGLTexture());
    const auto* axe = registry.GetDefinition("stone_axe");
    const auto* pickaxe = registry.GetDefinition("stone_pickaxe");
    REQUIRE(axe != nullptr);
    REQUIRE(pickaxe != nullptr);
    const auto axeMesh = voxels::graphics::BuildHeldItemMesh({axe->id, 1}, registry, atlas, false, 0.0f);
    const auto pickaxeMesh = voxels::graphics::BuildHeldItemMesh({pickaxe->id, 1}, registry, atlas, false, 0.0f);
    REQUIRE(axeMesh.size() > 36U);
    REQUIRE(pickaxeMesh.size() == axeMesh.size());
    REQUIRE(axeMesh[72].position.x != pickaxeMesh[72].position.x);
}