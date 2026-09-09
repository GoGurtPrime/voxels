/**
 * @file block.cpp
 * @brief Block registry and data-driven catalogue loader implementation.
 */

#include "voxels/world/block.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "voxels/core/logger.hpp"

namespace voxels {

namespace {

constexpr const char* kEmbeddedLaunchBlocksJson = R"({
  "blocks": [
    {
      "id": "air",
      "numeric_id": 0,
      "display_name": "Air",
      "solid": false,
      "opaque": false,
      "liquid": false,
      "hardness": 0.0,
      "light_emission": 0,
      "textures": {},
      "render_type": "cube",
      "model_id": null,
      "sounds": {},
      "drops": []
    },
    {
      "id": "stone",
      "numeric_id": 1,
      "display_name": "Stone",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 1.5,
      "light_emission": 0,
      "textures": { "all": "blocks/stone" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_stone", "step": "sfx/step_stone", "place": "sfx/place_stone" },
      "drops": [{ "item": "stone", "count": 1 }]
    },
    {
      "id": "dirt",
      "numeric_id": 2,
      "display_name": "Dirt",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 0.5,
      "light_emission": 0,
      "textures": { "all": "blocks/dirt" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_dirt", "step": "sfx/step_dirt", "place": "sfx/place_dirt" },
      "drops": [{ "item": "dirt", "count": 1 }]
    },
    {
      "id": "grass",
      "numeric_id": 3,
      "display_name": "Grass",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 0.6,
      "light_emission": 0,
      "textures": {
        "top": "blocks/grass_top",
        "bottom": "blocks/dirt",
        "side": "blocks/grass_side"
      },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_grass", "step": "sfx/step_grass", "place": "sfx/place_grass" },
      "drops": [{ "item": "dirt", "count": 1 }],
      "tint": [0.45, 0.75, 0.35, 1.0]
    },
    {
      "id": "sand",
      "numeric_id": 4,
      "display_name": "Sand",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 0.5,
      "light_emission": 0,
      "textures": { "all": "blocks/sand" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_sand", "step": "sfx/step_sand", "place": "sfx/place_sand" },
      "drops": [{ "item": "sand", "count": 1 }]
    },
    {
      "id": "gravel",
      "numeric_id": 5,
      "display_name": "Gravel",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 0.6,
      "light_emission": 0,
      "textures": { "all": "blocks/gravel" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_gravel", "step": "sfx/step_gravel", "place": "sfx/place_gravel" },
      "drops": [{ "item": "gravel", "count": 1 }]
    },
    {
      "id": "water",
      "numeric_id": 6,
      "display_name": "Water",
      "solid": false,
      "opaque": false,
      "liquid": true,
      "hardness": 100.0,
      "light_emission": 0,
      "textures": { "all": "blocks/water" },
      "render_type": "liquid",
      "model_id": null,
      "sounds": { "step": "sfx/step_water" },
      "drops": []
    },
    {
      "id": "coal_ore",
      "numeric_id": 7,
      "display_name": "Coal Ore",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 3.0,
      "light_emission": 0,
      "textures": { "all": "blocks/coal_ore" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_stone", "step": "sfx/step_stone", "place": "sfx/place_stone" },
      "drops": [{ "item": "coal", "count": 1 }]
    },
    {
      "id": "iron_ore",
      "numeric_id": 8,
      "display_name": "Iron Ore",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 3.0,
      "light_emission": 0,
      "textures": { "all": "blocks/iron_ore" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_stone", "step": "sfx/step_stone", "place": "sfx/place_stone" },
      "drops": [{ "item": "iron_ore", "count": 1 }]
    },
    {
      "id": "wood_log",
      "numeric_id": 9,
      "display_name": "Wood Log",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 2.0,
      "light_emission": 0,
      "textures": {
        "top": "blocks/wood_log_top",
        "bottom": "blocks/wood_log_top",
        "side": "blocks/wood_log_side"
      },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_wood", "step": "sfx/step_wood", "place": "sfx/place_wood" },
      "drops": [{ "item": "wood_log", "count": 1 }]
    },
    {
      "id": "leaves",
      "numeric_id": 10,
      "display_name": "Leaves",
      "solid": true,
      "opaque": false,
      "liquid": false,
      "hardness": 0.2,
      "light_emission": 0,
      "textures": { "all": "blocks/leaves" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_grass", "step": "sfx/step_grass", "place": "sfx/place_grass" },
      "drops": [],
      "tint": [0.38, 0.68, 0.28, 1.0]
    },
    {
      "id": "planks",
      "numeric_id": 11,
      "display_name": "Wooden Planks",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": 2.0,
      "light_emission": 0,
      "textures": { "all": "blocks/planks" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_wood", "step": "sfx/step_wood", "place": "sfx/place_wood" },
      "drops": [{ "item": "planks", "count": 1 }]
    },
    {
      "id": "glass",
      "numeric_id": 12,
      "display_name": "Glass",
      "solid": true,
      "opaque": false,
      "liquid": false,
      "hardness": 0.3,
      "light_emission": 0,
      "textures": { "all": "blocks/glass" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_glass", "step": "sfx/step_glass", "place": "sfx/place_glass" },
      "drops": []
    },
    {
      "id": "bedrock",
      "numeric_id": 13,
      "display_name": "Bedrock",
      "solid": true,
      "opaque": true,
      "liquid": false,
      "hardness": -1.0,
      "light_emission": 0,
      "textures": { "all": "blocks/bedrock" },
      "render_type": "cube",
      "model_id": null,
      "sounds": { "break": "sfx/break_stone", "step": "sfx/step_stone", "place": "sfx/place_stone" },
      "drops": []
    },
    {
      "id": "coal",
      "numeric_id": 14,
      "display_name": "Coal",
      "solid": false,
      "opaque": false,
      "liquid": false,
      "placeable": false,
      "hardness": -1.0,
      "light_emission": 0,
      "textures": { "all": "blocks/coal_ore" },
      "render_type": "cube",
      "model_id": null,
      "sounds": {},
      "drops": []
    },
    {
      "id": "stick",
      "numeric_id": 15,
      "display_name": "Stick",
      "solid": false,
      "opaque": false,
      "liquid": false,
      "placeable": false,
      "hardness": -1.0,
      "light_emission": 0,
      "textures": { "all": "blocks/wood_log_side" },
      "render_type": "cube",
      "model_id": null,
      "sounds": {},
      "drops": []
    },
    {
      "id": "garden_mix",
      "numeric_id": 16,
      "display_name": "Garden Mix",
      "solid": false,
      "opaque": false,
      "liquid": false,
      "placeable": false,
      "hardness": -1.0,
      "light_emission": 0,
      "textures": { "all": "blocks/dirt" },
      "render_type": "cube",
      "model_id": null,
      "sounds": {},
      "drops": []
    },
    {
      "id": "stone_axe",
      "numeric_id": 17,
      "display_name": "Stone Axe",
      "solid": false,
      "opaque": false,
      "liquid": false,
      "placeable": false,
      "hardness": -1.0,
      "light_emission": 0,
      "textures": { "all": "blocks/stone" },
      "render_type": "cube",
      "model_id": null,
      "sounds": {},
      "drops": []
    }
  ]
})";

} // namespace

BlockId BlockRegistry::RegisterBlock(BlockDefinition definition) {
    const BlockId id = definition.id;
    m_nameToId[definition.name] = id;
    m_definitions[id] = std::move(definition);
    return id;
}

void BlockRegistry::RegisterAlias(const std::string& aliasName, BlockId targetId) {
    m_nameToId[aliasName] = targetId;
}

void BlockRegistry::LoadFromJsonString(std::string_view jsonContent, std::string_view sourceName) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonContent, nullptr, true, true);
    } catch (const std::exception& e) {
        throw std::runtime_error("BlockRegistry JSON parse error in " + std::string(sourceName) + ": " + e.what());
    }

    if (!root.is_object() || !root.contains("blocks") || !root["blocks"].is_array()) {
        throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + ": root object must contain 'blocks' array.");
    }

    std::unordered_map<BlockId, BlockDefinition> newDefinitions;
    std::unordered_map<std::string, BlockId> newNameToId;

    for (std::size_t i = 0; i < root["blocks"].size(); ++i) {
        const auto& blockJson = root["blocks"][i];
        if (!blockJson.is_object()) {
            throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + " at block index " + std::to_string(i) + ": entry must be a JSON object.");
        }

        if (!blockJson.contains("id") || !blockJson["id"].is_string()) {
            throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + " at block index " + std::to_string(i) + ": missing or invalid string 'id'.");
        }
        const std::string idStr = blockJson["id"].get<std::string>();
        if (idStr.empty()) {
            throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + " at block index " + std::to_string(i) + ": 'id' cannot be empty.");
        }
        if (newNameToId.find(idStr) != newNameToId.end()) {
            throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + ": duplicate string id '" + idStr + "'.");
        }

        if (!blockJson.contains("numeric_id") || !blockJson["numeric_id"].is_number_integer()) {
            throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + " for block '" + idStr + "': missing or non-integer 'numeric_id'.");
        }
        const int numericIdInt = blockJson["numeric_id"].get<int>();
        if (numericIdInt < 0 || numericIdInt > 65535) {
            throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + " for block '" + idStr + "': numeric_id " + std::to_string(numericIdInt) + " out of valid range [0, 65535].");
        }
        const auto numericId = static_cast<BlockId>(numericIdInt);
        if (newDefinitions.find(numericId) != newDefinitions.end()) {
            throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + ": duplicate numeric_id " + std::to_string(numericIdInt) + " on block '" + idStr + "'.");
        }

        BlockDefinition def;
        def.id = numericId;
        def.name = idStr;
        def.type = static_cast<BlockType>(numericId);
        def.displayName = blockJson.value("display_name", idStr);
        def.isSolid = blockJson.value("solid", true);
        def.isOpaque = blockJson.value("opaque", def.isSolid);
        def.isTransparent = !def.isOpaque;
        def.isLiquid = blockJson.value("liquid", false);
        def.isPlaceable = blockJson.value("placeable", true);
        def.hardness = blockJson.value("hardness", 1.0f);
        def.lightEmission = static_cast<std::uint8_t>(blockJson.value("light_emission", 0));
        def.renderType = blockJson.value("render_type", "cube");

        if (blockJson.contains("model_id") && blockJson["model_id"].is_string()) {
            def.modelId = blockJson["model_id"].get<std::string>();
        }

        if (blockJson.contains("collision_bounds") && blockJson["collision_bounds"].is_object()) {
            const auto& bounds = blockJson["collision_bounds"];
            const auto& minimum = bounds.at("min");
            const auto& maximum = bounds.at("max");
            if (!minimum.is_array() || !maximum.is_array() || minimum.size() != 3 || maximum.size() != 3) {
                throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + " for block '" + idStr + "': collision_bounds must contain min/max triples.");
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                def.collisionBounds.min[axis] = minimum.at(axis).get<float>();
                def.collisionBounds.max[axis] = maximum.at(axis).get<float>();
                if (def.collisionBounds.min[axis] < 0.0f || def.collisionBounds.max[axis] > 1.0f ||
                    def.collisionBounds.min[axis] >= def.collisionBounds.max[axis]) {
                    throw std::runtime_error("BlockRegistry error in " + std::string(sourceName) + " for block '" + idStr + "': collision_bounds must be ordered within [0,1].");
                }
            }
        }

        if (blockJson.contains("textures") && blockJson["textures"].is_object()) {
            const auto& tex = blockJson["textures"];
            def.textures.all = tex.value("all", "");
            def.textures.top = tex.value("top", "");
            def.textures.bottom = tex.value("bottom", "");
            def.textures.north = tex.value("north", "");
            def.textures.south = tex.value("south", "");
            def.textures.east = tex.value("east", "");
            def.textures.west = tex.value("west", "");
            def.textures.side = tex.value("side", "");
        }

        if (blockJson.contains("sounds") && blockJson["sounds"].is_object()) {
            const auto& snd = blockJson["sounds"];
            def.sounds.breakSound = snd.value("break", "");
            def.sounds.stepSound = snd.value("step", "");
            def.sounds.placeSound = snd.value("place", "");
        }

        if (blockJson.contains("drops") && blockJson["drops"].is_array()) {
            for (const auto& dropJson : blockJson["drops"]) {
                if (dropJson.is_object() && dropJson.contains("item") && dropJson["item"].is_string()) {
                    BlockDrop drop;
                    drop.item = dropJson["item"].get<std::string>();
                    drop.count = dropJson.value("count", 1);
                    def.drops.push_back(drop);
                }
            }
        }

        if (blockJson.contains("tint") && blockJson["tint"].is_array()) {
            const auto& tintArr = blockJson["tint"];
            for (std::size_t t = 0; t < std::min<std::size_t>(4U, tintArr.size()); ++t) {
                if (tintArr[t].is_number()) {
                    def.tintColor[t] = tintArr[t].get<float>();
                }
            }
        }

        if (blockJson.contains("metadata") && blockJson["metadata"].is_object()) {
            for (auto it = blockJson["metadata"].begin(); it != blockJson["metadata"].end(); ++it) {
                if (it.value().is_string()) {
                    def.metadata[it.key()] = it.value().get<std::string>();
                }
            }
        }

        newNameToId[idStr] = numericId;
        newDefinitions[numericId] = std::move(def);
    }

    m_definitions = std::move(newDefinitions);
    m_nameToId = std::move(newNameToId);

    std::string densityError;
    if (!ValidateDenseNumericIds(&densityError)) {
        throw std::runtime_error("BlockRegistry density validation failed in " + std::string(sourceName) + ": " + densityError);
    }
}

void BlockRegistry::LoadFromFile(const std::filesystem::path& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("BlockRegistry failed to open file: " + filePath.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    LoadFromJsonString(buffer.str(), filePath.string());
}

bool BlockRegistry::ValidateDenseNumericIds(std::string* outError) const {
    const std::size_t count = m_definitions.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (m_definitions.find(static_cast<BlockId>(i)) == m_definitions.end()) {
            if (outError != nullptr) {
                *outError = "Missing numeric_id " + std::to_string(i) + " in dense sequence [0, " + std::to_string(count - 1) + "]";
            }
            return false;
        }
    }
    return true;
}

std::vector<std::string> BlockRegistry::GetReferencedTextureNames() const {
    std::vector<std::string> textures;
    for (const auto& [id, def] : m_definitions) {
        for (const auto& path : {
            def.textures.all,
            def.textures.top,
            def.textures.bottom,
            def.textures.north,
            def.textures.south,
            def.textures.east,
            def.textures.west,
            def.textures.side
        }) {
            if (!path.empty() && std::find(textures.begin(), textures.end(), path) == textures.end()) {
                textures.push_back(path);
            }
        }
    }
    return textures;
}

bool BlockRegistry::IsRegistered(BlockId id) const noexcept {
    return m_definitions.find(id) != m_definitions.end();
}

bool BlockRegistry::IsRegistered(const std::string& name) const noexcept {
    return m_nameToId.find(name) != m_nameToId.end();
}

const BlockDefinition* BlockRegistry::GetDefinition(BlockId id) const noexcept {
    const auto it = m_definitions.find(id);
    return it != m_definitions.end() ? &it->second : nullptr;
}

const BlockDefinition* BlockRegistry::GetDefinition(const std::string& name) const noexcept {
    const auto it = m_nameToId.find(name);
    if (it == m_nameToId.end()) {
        return nullptr;
    }
    return GetDefinition(it->second);
}

BlockRegistry CreateDefaultBlockRegistry() {
    BlockRegistry registry;

    bool loadedFromFile = false;
    const std::vector<std::filesystem::path> candidatePaths = {
        "assets/data/blocks.json",
        "app/assets/data/blocks.json",
#ifdef VOXELS_SOURCE_DIR
        std::filesystem::path(VOXELS_SOURCE_DIR) / "app" / "assets" / "data" / "blocks.json",
#endif
    };

    for (const auto& path : candidatePaths) {
        if (std::filesystem::exists(path)) {
            try {
                registry.LoadFromFile(path);
                loadedFromFile = true;
                break;
            } catch (const std::exception& e) {
                Logger logger;
                logger.Warn("Failed to load blocks from file " + path.string() + ": " + e.what() + "; trying fallback.");
            }
        }
    }

    if (!loadedFromFile) {
        registry.LoadFromJsonString(kEmbeddedLaunchBlocksJson, "<embedded blocks.json>");
    }

    // Register backward-compatible aliases for legacy lookups
    registry.RegisterAlias("tree_trunk", static_cast<BlockId>(BlockType::Wood));
    registry.RegisterAlias("tree", static_cast<BlockId>(BlockType::Wood));
    registry.RegisterAlias("leaf", static_cast<BlockId>(BlockType::Leaf));

    return registry;
}

} // namespace voxels
