/**
 * @file tool_catalogue.hpp
 * @brief Validated data-backed definitions for durable player tools.
 *
 * @details Resolves stable inventory item names through BlockRegistry before simulation begins.
 *          GameSession uses the resulting numeric lookup on the main simulation thread; no file
 *          access or string parsing occurs during player interaction. See ARCHITECTURE.md §6.4.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "voxels/gameplay/inventory.hpp"

namespace voxels::gameplay {

/// The authoritative action family supported by a tool definition.
enum class ToolKind { Axe, Pickaxe, Shovel, Sledgehammer, Sword, Hoe };

/// Immutable, validated stats for one inventory-only tool item.
struct ToolDefinition {
    std::string id;
    BlockId itemId = static_cast<BlockId>(BlockType::Air);
    ToolKind kind = ToolKind::Axe;
    std::string tier;
    std::uint16_t maxDurability = 0;
    std::uint8_t baseDamage = 0;
    float swingIntervalSeconds = 0.0f;
    std::unordered_map<std::string, float> miningSpeedMultipliers;
};

/// Loads tool content once and exposes constant-time selected-item lookups.
class ToolCatalogue {
public:
    /// Loads and validates a tool document from disk, resolving all ids through `registry`.
    void LoadFromFile(const std::filesystem::path& filePath, const BlockRegistry& registry);

    /// Parses and validates a tool document. Public for deterministic content tests.
    void LoadFromJsonString(std::string_view jsonContent, std::string_view sourceName,
                            const BlockRegistry& registry);

    /// Finds the tool represented by an inventory item id, or null when it is not a tool.
    [[nodiscard]] const ToolDefinition* FindByItem(BlockId itemId) const noexcept;

    /// Returns the configured multiplier for `blockTag`, falling back to the neutral value 1.
    [[nodiscard]] float MiningSpeedMultiplier(const ToolDefinition& tool,
                                              std::string_view blockTag) const noexcept;

    /// Resolves an uninitialized tool stack to its full durability for display and action use.
    [[nodiscard]] std::uint16_t CurrentDurability(const ItemStack& stack,
                                                  const ToolDefinition& tool) const noexcept;

    /// Decrements a tool only after a successful effect. Returns false when this use breaks it.
    [[nodiscard]] bool ConsumeDurability(ItemStack& stack, const ToolDefinition& tool) const noexcept;

    [[nodiscard]] const std::vector<ToolDefinition>& GetTools() const noexcept { return m_tools; }

private:
    std::vector<ToolDefinition> m_tools;
    std::unordered_map<BlockId, std::size_t> m_indexByItem;
};

} // namespace voxels::gameplay