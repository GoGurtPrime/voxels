/**
 * @file crafting_service.hpp
 * @brief Validated recipe catalogue and authoritative transactional crafting service.
 *
 * @details Loads recipe content before gameplay begins, resolves stable item ids through the
 *          BlockRegistry, and mutates fixed inventories atomically. See ARCHITECTURE.md §6.4.
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "voxels/gameplay/inventory.hpp"

namespace voxels {
class BlockRegistry;
}

namespace voxels::gameplay {

/// One resolved ingredient requirement for a crafting recipe.
struct RecipeIngredient {
    std::string itemId;
    BlockId blockId = static_cast<BlockId>(BlockType::Air);
    int count = 0;
};

/// Validated data-backed crafting recipe exposed to gameplay and the player HUD.
struct RecipeDefinition {
    std::string id;
    std::string icon;
    std::string category;
    std::vector<RecipeIngredient> ingredients;
    std::string outputItemId;
    BlockId outputBlockId = static_cast<BlockId>(BlockType::Air);
    int outputCount = 0;
};

/// Loads the recipe catalogue once and applies recipes to inventories atomically.
class CraftingService {
public:
    /// Loads and validates `recipes` from a JSON file. Throws std::runtime_error containing the
    /// recipe id and offending field when content is invalid or references an unknown item.
    void LoadFromFile(const std::filesystem::path& filePath, const BlockRegistry& registry);

    /// Parses and validates a JSON document. Primarily supports deterministic content tests.
    void LoadFromJsonString(std::string_view jsonContent, std::string_view sourceName,
                            const BlockRegistry& registry);

    /// Returns the stable ordered recipe catalogue.
    [[nodiscard]] const std::vector<RecipeDefinition>& GetRecipes() const noexcept { return m_recipes; }

    /// Finds a recipe by stable id, returning nullptr for unknown ids.
    [[nodiscard]] const RecipeDefinition* FindRecipe(std::string_view recipeId) const noexcept;

    /// Returns whether all ingredients are present and all output items fit without mutation.
    [[nodiscard]] bool CanCraft(const Inventory& inventory, std::string_view recipeId) const noexcept;

    /// Atomically consumes ingredients and adds output. Returns false with no inventory change
    /// for an unknown recipe, insufficient ingredients, or insufficient output capacity.
    [[nodiscard]] bool Craft(Inventory& inventory, std::string_view recipeId) const noexcept;

private:
    std::vector<RecipeDefinition> m_recipes;
};

} // namespace voxels::gameplay