/**
 * @file tool_catalogue.cpp
 * @brief Validates tool content and builds its fixed-tick lookup index.
 *
 * @details Rejects invalid authoring input before gameplay starts, preserving the simulation's
 *          bounded numeric lookup contract from ARCHITECTURE.md §6.4.
 */

#include "voxels/gameplay/tool_catalogue.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace voxels::gameplay {
namespace {

[[nodiscard]] std::string FieldError(std::string_view source, std::string_view toolId,
                                     std::string_view field, std::string_view reason) {
    return std::string(source) + ": tool '" + std::string(toolId) + "' field '" +
           std::string(field) + "' " + std::string(reason);
}

[[nodiscard]] std::string RequiredString(const nlohmann::json& value, std::string_view source,
                                         std::string_view toolId, std::string_view field) {
    const auto found = value.find(std::string(field));
    if (found == value.end() || !found->is_string() || found->get_ref<const std::string&>().empty()) {
        throw std::runtime_error(FieldError(source, toolId, field, "must be a non-empty string"));
    }
    return found->get<std::string>();
}

[[nodiscard]] ToolKind ParseToolKind(std::string_view value, std::string_view source,
                                     std::string_view toolId) {
    if (value == "axe") return ToolKind::Axe;
    if (value == "pickaxe") return ToolKind::Pickaxe;
    if (value == "shovel") return ToolKind::Shovel;
    if (value == "sledgehammer") return ToolKind::Sledgehammer;
    if (value == "sword") return ToolKind::Sword;
    if (value == "hoe") return ToolKind::Hoe;
    throw std::runtime_error(FieldError(source, toolId, "kind", "is unknown"));
}

[[nodiscard]] int RequiredInteger(const nlohmann::json& value, std::string_view source,
                                  std::string_view toolId, std::string_view field, int minimum,
                                  int maximum) {
    const auto found = value.find(std::string(field));
    if (found == value.end() || !found->is_number_integer()) {
        throw std::runtime_error(FieldError(source, toolId, field, "must be an integer"));
    }
    const int result = found->get<int>();
    if (result < minimum || result > maximum) {
        throw std::runtime_error(FieldError(source, toolId, field, "is outside supported bounds"));
    }
    return result;
}

[[nodiscard]] float RequiredPositiveNumber(const nlohmann::json& value, std::string_view source,
                                           std::string_view toolId, std::string_view field) {
    const auto found = value.find(std::string(field));
    if (found == value.end() || !found->is_number()) {
        throw std::runtime_error(FieldError(source, toolId, field, "must be a number"));
    }
    const float result = found->get<float>();
    if (!(result > 0.0f) || result > 1000.0f) {
        throw std::runtime_error(FieldError(source, toolId, field, "must be within supported bounds"));
    }
    return result;
}

} // namespace

void ToolCatalogue::LoadFromFile(const std::filesystem::path& filePath, const BlockRegistry& registry) {
    std::ifstream file(filePath);
    if (!file) throw std::runtime_error("Unable to read tool catalogue '" + filePath.string() + "'.");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    LoadFromJsonString(buffer.str(), filePath.string(), registry);
}

void ToolCatalogue::LoadFromJsonString(std::string_view jsonContent, std::string_view sourceName,
                                       const BlockRegistry& registry) {
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(jsonContent);
    } catch (const nlohmann::json::exception& exception) {
        throw std::runtime_error(std::string(sourceName) + ": invalid JSON: " + exception.what());
    }
    const auto toolsField = document.find("tools");
    if (toolsField == document.end() || !toolsField->is_array()) {
        throw std::runtime_error(std::string(sourceName) + ": field 'tools' must be an array");
    }

    std::vector<ToolDefinition> loaded;
    std::unordered_map<BlockId, std::size_t> index;
    std::unordered_set<std::string> ids;
    loaded.reserve(toolsField->size());
    for (const nlohmann::json& entry : *toolsField) {
        if (!entry.is_object()) throw std::runtime_error(std::string(sourceName) + ": tool entry must be an object");
        ToolDefinition tool;
        tool.id = RequiredString(entry, sourceName, "<unknown>", "id");
        if (!ids.insert(tool.id).second) throw std::runtime_error(FieldError(sourceName, tool.id, "id", "is duplicated"));
        const std::string itemName = RequiredString(entry, sourceName, tool.id, "item");
        const BlockDefinition* item = registry.GetDefinition(itemName);
        if (item == nullptr) throw std::runtime_error(FieldError(sourceName, tool.id, "item", "references unknown item '" + itemName + "'"));
        if (item->isPlaceable) throw std::runtime_error(FieldError(sourceName, tool.id, "item", "must reference an inventory-only item"));
        tool.itemId = item->id;
        if (!index.emplace(tool.itemId, loaded.size()).second) throw std::runtime_error(FieldError(sourceName, tool.id, "item", "is duplicated"));
        tool.kind = ParseToolKind(RequiredString(entry, sourceName, tool.id, "kind"), sourceName, tool.id);
        tool.tier = RequiredString(entry, sourceName, tool.id, "tier");
        tool.maxDurability = static_cast<std::uint16_t>(RequiredInteger(entry, sourceName, tool.id, "max_durability", 1, 65535));
        tool.baseDamage = static_cast<std::uint8_t>(RequiredInteger(entry, sourceName, tool.id, "base_damage", 1, 255));
        tool.swingIntervalSeconds = RequiredPositiveNumber(entry, sourceName, tool.id, "swing_interval_seconds");
        const auto multipliers = entry.find("mining_speed_multipliers");
        if (multipliers == entry.end() || !multipliers->is_object() || multipliers->empty()) {
            throw std::runtime_error(FieldError(sourceName, tool.id, "mining_speed_multipliers", "must be a non-empty object"));
        }
        for (auto multiplier = multipliers->begin(); multiplier != multipliers->end(); ++multiplier) {
            if (multiplier.key().empty() || !multiplier.value().is_number()) {
                throw std::runtime_error(FieldError(sourceName, tool.id, "mining_speed_multipliers", "contains an invalid entry"));
            }
            const float value = multiplier.value().get<float>();
            if (!(value > 0.0f) || value > 1000.0f) {
                throw std::runtime_error(FieldError(sourceName, tool.id, "mining_speed_multipliers", "must contain positive values"));
            }
            tool.miningSpeedMultipliers.emplace(multiplier.key(), value);
        }
        loaded.push_back(std::move(tool));
    }
    m_tools = std::move(loaded);
    m_indexByItem = std::move(index);
}

const ToolDefinition* ToolCatalogue::FindByItem(BlockId itemId) const noexcept {
    const auto found = m_indexByItem.find(itemId);
    return found == m_indexByItem.end() ? nullptr : &m_tools[found->second];
}

float ToolCatalogue::MiningSpeedMultiplier(const ToolDefinition& tool, std::string_view blockTag) const noexcept {
    const auto found = tool.miningSpeedMultipliers.find(std::string(blockTag));
    return found == tool.miningSpeedMultipliers.end() ? 1.0f : found->second;
}

std::uint16_t ToolCatalogue::CurrentDurability(const ItemStack& stack, const ToolDefinition& tool) const noexcept {
    return stack.durability == 0 ? tool.maxDurability : std::min(stack.durability, tool.maxDurability);
}

bool ToolCatalogue::ConsumeDurability(ItemStack& stack, const ToolDefinition& tool) const noexcept {
    const std::uint16_t current = CurrentDurability(stack, tool);
    if (current <= 1) {
        stack = {};
        return false;
    }
    stack.durability = static_cast<std::uint16_t>(current - 1);
    return true;
}

} // namespace voxels::gameplay