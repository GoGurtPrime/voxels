/**
 * @file crafting_service.cpp
 * @brief Validates recipe JSON content and performs atomic inventory crafting mutations.
 *
 * @details Recipe content is loaded before gameplay starts so fixed simulation ticks only use
 *          resolved ids and bounded inventory scans. See DIAGRAMS.md diagram 5.
 */

#include "voxels/gameplay/crafting_service.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "voxels/world/block.hpp"

namespace voxels::gameplay {
namespace {

constexpr int kMaximumRecipeItemCount = Inventory::kSlotCount * Inventory::kStackLimit;

[[nodiscard]] std::string FieldError(std::string_view sourceName, std::string_view recipeId,
                                     std::string_view field, std::string_view reason) {
    return std::string(sourceName) + ": recipe '" + std::string(recipeId) + "' field '" +
           std::string(field) + "' " + std::string(reason);
}

[[nodiscard]] int CountItem(const Inventory& inventory, BlockId blockId) noexcept {
    int total = 0;
    for (const ItemStack& stack : inventory.Slots()) {
        if (!stack.IsEmpty() && stack.blockId == blockId) total += stack.count;
    }
    return total;
}

[[nodiscard]] bool RemoveItem(Inventory& inventory, BlockId blockId, int count) noexcept {
    int remaining = count;
    for (ItemStack& stack : inventory.Slots()) {
        if (remaining == 0) break;
        if (stack.IsEmpty() || stack.blockId != blockId) continue;
        const int removed = std::min(remaining, stack.count);
        stack.count -= removed;
        remaining -= removed;
        if (stack.count == 0) stack = {};
    }
    return remaining == 0;
}

[[nodiscard]] bool IsKnownCategory(std::string_view category) noexcept {
    return category == "construction" || category == "food" || category == "tools" ||
           category == "furniture" || category == "smelters";
}

[[nodiscard]] std::string RequiredString(const nlohmann::json& object, std::string_view sourceName,
                                         std::string_view recipeId, std::string_view field) {
    const auto found = object.find(std::string(field));
    if (found == object.end() || !found->is_string() || found->get_ref<const std::string&>().empty()) {
        throw std::runtime_error(FieldError(sourceName, recipeId, field, "must be a non-empty string"));
    }
    return found->get<std::string>();
}

[[nodiscard]] int RequiredCount(const nlohmann::json& object, std::string_view sourceName,
                                std::string_view recipeId, std::string_view field) {
    const auto found = object.find(std::string(field));
    if (found == object.end() || !found->is_number_integer()) {
        throw std::runtime_error(FieldError(sourceName, recipeId, field, "must be an integer"));
    }
    const int count = found->get<int>();
    if (count <= 0 || count > kMaximumRecipeItemCount) {
        throw std::runtime_error(FieldError(sourceName, recipeId, field, "must be within inventory bounds"));
    }
    return count;
}

} // namespace

void CraftingService::LoadFromFile(const std::filesystem::path& filePath, const BlockRegistry& registry) {
    std::ifstream file(filePath);
    if (!file) throw std::runtime_error("Unable to read recipe catalogue '" + filePath.string() + "'.");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    LoadFromJsonString(buffer.str(), filePath.string(), registry);
}

void CraftingService::LoadFromJsonString(std::string_view jsonContent, std::string_view sourceName,
                                         const BlockRegistry& registry) {
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(jsonContent);
    } catch (const nlohmann::json::exception& exception) {
        throw std::runtime_error(std::string(sourceName) + ": invalid JSON: " + exception.what());
    }
    const auto recipesField = document.find("recipes");
    if (recipesField == document.end() || !recipesField->is_array()) {
        throw std::runtime_error(std::string(sourceName) + ": field 'recipes' must be an array");
    }

    std::unordered_set<std::string> ids;
    std::vector<RecipeDefinition> loaded;
    loaded.reserve(recipesField->size());
    for (const nlohmann::json& entry : *recipesField) {
        if (!entry.is_object()) throw std::runtime_error(std::string(sourceName) + ": recipe entry must be an object");
        RecipeDefinition recipe;
        recipe.id = RequiredString(entry, sourceName, "<unknown>", "id");
        if (!ids.insert(recipe.id).second) {
            throw std::runtime_error(FieldError(sourceName, recipe.id, "id", "is duplicated"));
        }
        recipe.icon = RequiredString(entry, sourceName, recipe.id, "icon");
        recipe.category = RequiredString(entry, sourceName, recipe.id, "category");
        if (!IsKnownCategory(recipe.category)) {
            throw std::runtime_error(FieldError(sourceName, recipe.id, "category", "is unknown"));
        }
        recipe.outputItemId = RequiredString(entry, sourceName, recipe.id, "output_item");
        const BlockDefinition* output = registry.GetDefinition(recipe.outputItemId);
        if (output == nullptr) {
            throw std::runtime_error(FieldError(sourceName, recipe.id, "output_item", "references unknown item '" + recipe.outputItemId + "'"));
        }
        recipe.outputBlockId = output->id;
        recipe.outputCount = RequiredCount(entry, sourceName, recipe.id, "output_count");

        const auto ingredientsField = entry.find("ingredients");
        if (ingredientsField == entry.end() || !ingredientsField->is_array() || ingredientsField->empty()) {
            throw std::runtime_error(FieldError(sourceName, recipe.id, "ingredients", "must be a non-empty array"));
        }
        std::unordered_set<BlockId> ingredients;
        for (const nlohmann::json& ingredientEntry : *ingredientsField) {
            if (!ingredientEntry.is_object()) {
                throw std::runtime_error(FieldError(sourceName, recipe.id, "ingredients", "contains a non-object entry"));
            }
            RecipeIngredient ingredient;
            ingredient.itemId = RequiredString(ingredientEntry, sourceName, recipe.id, "item");
            const BlockDefinition* input = registry.GetDefinition(ingredient.itemId);
            if (input == nullptr) {
                throw std::runtime_error(FieldError(sourceName, recipe.id, "ingredients.item", "references unknown item '" + ingredient.itemId + "'"));
            }
            ingredient.blockId = input->id;
            ingredient.count = RequiredCount(ingredientEntry, sourceName, recipe.id, "count");
            if (!ingredients.insert(ingredient.blockId).second) {
                throw std::runtime_error(FieldError(sourceName, recipe.id, "ingredients.item", "is duplicated"));
            }
            recipe.ingredients.push_back(std::move(ingredient));
        }
        loaded.push_back(std::move(recipe));
    }
    m_recipes = std::move(loaded);
}

const RecipeDefinition* CraftingService::FindRecipe(std::string_view recipeId) const noexcept {
    const auto found = std::find_if(m_recipes.begin(), m_recipes.end(), [recipeId](const RecipeDefinition& recipe) {
        return recipe.id == recipeId;
    });
    return found == m_recipes.end() ? nullptr : &*found;
}

bool CraftingService::CanCraft(const Inventory& inventory, std::string_view recipeId) const noexcept {
    const RecipeDefinition* recipe = FindRecipe(recipeId);
    if (recipe == nullptr) return false;
    for (const RecipeIngredient& ingredient : recipe->ingredients) {
        if (CountItem(inventory, ingredient.blockId) < ingredient.count) return false;
    }
    Inventory probe = inventory;
    for (const RecipeIngredient& ingredient : recipe->ingredients) {
        if (!RemoveItem(probe, ingredient.blockId, ingredient.count)) return false;
    }
    return probe.AddItem(recipe->outputBlockId, recipe->outputCount) == 0;
}

bool CraftingService::Craft(Inventory& inventory, std::string_view recipeId) const noexcept {
    if (!CanCraft(inventory, recipeId)) return false;
    const RecipeDefinition* recipe = FindRecipe(recipeId);
    Inventory updated = inventory;
    for (const RecipeIngredient& ingredient : recipe->ingredients) {
        if (!RemoveItem(updated, ingredient.blockId, ingredient.count)) return false;
    }
    if (updated.AddItem(recipe->outputBlockId, recipe->outputCount) != 0) return false;
    inventory = updated;
    return true;
}

} // namespace voxels::gameplay