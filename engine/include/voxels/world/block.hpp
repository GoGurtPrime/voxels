#pragma once

/**
 * @file block.hpp
 * @brief Block definitions, texture mapping, sound bindings, and data-driven catalogue registry.
 *
 * @details Establishes the contract for block definitions, item definitions, per-face texture
 *          resolution, and runtime registry loading from blocks.json.
 *          Reference ARCHITECTURE.md §6.6 and work_items/04_block_definitions_textures_and_atlas.md.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "voxels/world/geometry.hpp"

namespace voxels {

/// Stable per-block identifier used for chunk storage and registry lookup.
using BlockId = std::uint16_t;

enum class BlockType : std::uint32_t {
    Air = 0,
    Stone = 1,
    Dirt = 2,
    Grass = 3,
    Sand = 4,
    Gravel = 5,
    Water = 6,
    Coal = 7,
    CoalOre = Coal,
    Iron = 8,
    IronOre = Iron,
    Wood = 9,
    TreeTrunk = Wood,
    Tree = Wood,
    WoodLog = Wood,
    Leaf = 10,
    Leaves = Leaf,
    Planks = 11,
    Glass = 12,
    Bedrock = 13
};

/// Packed per-voxel runtime state: light level, facing orientation, and free-form metadata.
struct BlockState {
    std::uint8_t lightLevel : 4 = 0;
    std::uint8_t orientation : 3 = 0;
    std::uint8_t metadata = 0;

    [[nodiscard]] bool operator==(const BlockState&) const noexcept = default;
};

/// Per-face texture paths / identifiers associated with a block.
struct BlockTextures {
    std::string all;
    std::string top;
    std::string bottom;
    std::string north;
    std::string south;
    std::string east;
    std::string west;
    std::string side;

    [[nodiscard]] std::string ResolveFaceTexture(Face face) const {
        switch (face) {
            case Face::PosY:
                if (!top.empty()) return top;
                break;
            case Face::NegY:
                if (!bottom.empty()) return bottom;
                break;
            case Face::NegZ:
                if (!north.empty()) return north;
                if (!side.empty()) return side;
                break;
            case Face::PosZ:
                if (!south.empty()) return south;
                if (!side.empty()) return side;
                break;
            case Face::PosX:
                if (!east.empty()) return east;
                if (!side.empty()) return side;
                break;
            case Face::NegX:
                if (!west.empty()) return west;
                if (!side.empty()) return side;
                break;
        }
        return all;
    }
};

/// Audio sound event identifiers for block interaction.
struct BlockSounds {
    std::string breakSound;
    std::string stepSound;
    std::string placeSound;
};

/// Drops produced when breaking a block.
struct BlockDrop {
    std::string item;
    int count = 1;

    [[nodiscard]] bool operator==(const BlockDrop&) const noexcept = default;
};

struct BlockCollisionBounds {
    std::array<float, 3> min = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> max = {1.0f, 1.0f, 1.0f};
};

struct BlockDefinition {
    BlockType type = BlockType::Air;
    BlockId id = 0;
    std::string name;
    std::string displayName;
    bool isSolid = true;
    bool isTransparent = false;
    bool isOpaque = true;
    bool isLiquid = false;
    float hardness = 1.0f;
    std::uint8_t lightEmission = 0;
    std::string renderType = "cube"; // "cube" | "cross" | "liquid" | "model"
    std::optional<std::string> modelId = std::nullopt;
    BlockCollisionBounds collisionBounds;
    BlockTextures textures;
    BlockSounds sounds;
    std::vector<BlockDrop> drops;
    std::array<float, 4> tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    std::unordered_map<std::string, std::string> metadata;

    [[nodiscard]] std::string GetFaceTexture(Face face) const {
        return textures.ResolveFaceTexture(face);
    }
};

struct ItemDefinition {
    std::string id;
    std::string name;
    bool isPlaceable = false;
    std::unordered_map<std::string, std::string> state;
};

class IBlockBehavior {
public:
    virtual ~IBlockBehavior() = default;
    virtual void OnPlaced() = 0;
    virtual void OnUpdated() = 0;
};

/// Maps block ids and names to their registered definitions.
class BlockRegistry {
public:
    /// Registers (or replaces) a block definition, keyed by its id.
    BlockId RegisterBlock(BlockDefinition definition);

    /// Registers a secondary alias name resolving to targetId.
    void RegisterAlias(const std::string& aliasName, BlockId targetId);

    /// Loads block definitions from a JSON string. Throws std::runtime_error on malformed/duplicate entries.
    void LoadFromJsonString(std::string_view jsonContent, std::string_view sourceName = "blocks.json");

    /// Loads block definitions from a blocks.json file. Throws std::runtime_error if file cannot be read or parsed.
    void LoadFromFile(const std::filesystem::path& filePath);

    [[nodiscard]] bool IsRegistered(BlockId id) const noexcept;
    [[nodiscard]] bool IsRegistered(const std::string& name) const noexcept;
    [[nodiscard]] const BlockDefinition* GetDefinition(BlockId id) const noexcept;
    [[nodiscard]] const BlockDefinition* GetDefinition(const std::string& name) const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept { return m_definitions.size(); }
    [[nodiscard]] const std::unordered_map<BlockId, BlockDefinition>& GetAllDefinitions() const noexcept {
        return m_definitions;
    }

    /// Returns all distinct texture paths referenced by registered blocks.
    [[nodiscard]] std::vector<std::string> GetReferencedTextureNames() const;

    /// Validates that registered numeric ids form a dense sequence starting from 0.
    [[nodiscard]] bool ValidateDenseNumericIds(std::string* outError = nullptr) const;

private:
    std::unordered_map<BlockId, BlockDefinition> m_definitions;
    std::unordered_map<std::string, BlockId> m_nameToId;
};

/// Builds a registry populated with the full launch block catalogue.
[[nodiscard]] BlockRegistry CreateDefaultBlockRegistry();

} // namespace voxels
