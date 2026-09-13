#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "voxels/gameplay/crafting_service.hpp"
#include "voxels/world/block.hpp"

namespace {

constexpr std::string_view kValidRecipes = R"({"recipes":[{"id":"planks","icon":"planks","category":"construction","ingredients":[{"item":"wood_log","count":1}],"output_item":"planks","output_count":4}]})";

} // namespace

TEST_CASE("CraftingService loads validated recipes and commits atomically", "[Crafting][Inventory]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::CraftingService service;
    service.LoadFromJsonString(kValidRecipes, "valid-recipes.json", registry);
    REQUIRE(service.GetRecipes().size() == 1);

    voxels::gameplay::Inventory inventory;
    const auto* log = registry.GetDefinition("wood_log");
    const auto* planks = registry.GetDefinition("planks");
    REQUIRE(log != nullptr);
    REQUIRE(planks != nullptr);
    inventory.GetSlot(0) = {log->id, 1};
    REQUIRE(service.Craft(inventory, "planks"));
    REQUIRE(inventory.GetSlot(0).blockId == planks->id);
    REQUIRE(inventory.GetSlot(0).count == 4);
}

TEST_CASE("CraftingService leaves inventory untouched for missing inputs or output capacity", "[Crafting][Inventory]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::CraftingService service;
    service.LoadFromJsonString(
        R"({"recipes":[{"id":"planks","icon":"planks","category":"construction","ingredients":[{"item":"wood_log","count":1}],"output_item":"planks","output_count":65}]})",
        "capacity-recipes.json", registry);
    const auto* log = registry.GetDefinition("wood_log");
    const auto* stone = registry.GetDefinition("stone");
    REQUIRE(log != nullptr);
    REQUIRE(stone != nullptr);

    voxels::gameplay::Inventory insufficient;
    const voxels::gameplay::Inventory beforeInsufficient = insufficient;
    REQUIRE_FALSE(service.Craft(insufficient, "planks"));
    REQUIRE(insufficient == beforeInsufficient);

    voxels::gameplay::Inventory full;
    full.GetSlot(0) = {log->id, 1};
    for (std::size_t slot = 1; slot < voxels::gameplay::Inventory::kSlotCount; ++slot) {
        full.GetSlot(slot) = {stone->id, voxels::gameplay::Inventory::kStackLimit};
    }
    const voxels::gameplay::Inventory beforeFull = full;
    REQUIRE_FALSE(service.Craft(full, "planks"));
    REQUIRE(full == beforeFull);
}

TEST_CASE("CraftingService identifies invalid recipe field and recipe id", "[Crafting]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::gameplay::CraftingService service;
    const auto requireError = [&service, &registry](std::string_view source, std::string_view expectedRecipe,
                                                    std::string_view expectedField) {
        try {
            service.LoadFromJsonString(source, "invalid-recipes.json", registry);
            FAIL("Expected invalid recipe catalogue to throw");
        } catch (const std::runtime_error& error) {
            REQUIRE(std::string(error.what()).find(expectedRecipe) != std::string::npos);
            REQUIRE(std::string(error.what()).find(expectedField) != std::string::npos);
        }
    };
    SECTION("rejects unknown categories") {
        requireError(R"({"recipes":[{"id":"bad_category","icon":"x","category":"unknown","ingredients":[{"item":"wood_log","count":1}],"output_item":"planks","output_count":1}]})",
                     "bad_category", "category");
    }
    SECTION("rejects duplicate ids") {
        requireError(R"({"recipes":[{"id":"duplicate","icon":"x","category":"construction","ingredients":[{"item":"wood_log","count":1}],"output_item":"planks","output_count":1},{"id":"duplicate","icon":"x","category":"construction","ingredients":[{"item":"wood_log","count":1}],"output_item":"planks","output_count":1}]})",
                     "duplicate", "id");
    }
    SECTION("rejects non-positive counts") {
        requireError(R"({"recipes":[{"id":"bad_count","icon":"x","category":"construction","ingredients":[{"item":"wood_log","count":0}],"output_item":"planks","output_count":1}]})",
                     "bad_count", "count");
    }
}