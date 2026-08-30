#pragma once

/*
 * Scope: Block and item identity layer for world data and gameplay logic.
 *
 * This file establishes the contract for block definitions, item definitions, and custom
 * stateful behavior. The underlying implementation should allow each voxel type and item
 * to expose custom logic and serialized state while keeping the world system generic.
 *
 * Relation to the rest of the codebase: chunks, world generation, item logic, and editor asset
 * packing all depend on these definitions for world representation and serialization.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace voxels {

/// Stable per-block identifier used for chunk storage and registry lookup.
using BlockId = std::uint16_t;

enum class BlockType : std::uint32_t {
    Air,
    Stone,
    Dirt,
    Coal,
    Water,
    Wood = 5,
    TreeTrunk = Wood,
    Tree = Wood,
    Leaf
};

/// Packed per-voxel runtime state: light level, facing orientation, and free-form metadata.
struct BlockState {
    std::uint8_t lightLevel : 4 = 0;
    std::uint8_t orientation : 3 = 0;
    std::uint8_t metadata = 0;

    [[nodiscard]] bool operator==(const BlockState&) const noexcept = default;
};

struct BlockDefinition {
    BlockType type = BlockType::Air;
    BlockId id = 0;
    std::string name;
    bool isSolid = true;
    bool isTransparent = false;
    std::unordered_map<std::string, std::string> metadata;
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

    [[nodiscard]] bool IsRegistered(BlockId id) const noexcept;
    [[nodiscard]] const BlockDefinition* GetDefinition(BlockId id) const noexcept;
    [[nodiscard]] const BlockDefinition* GetDefinition(const std::string& name) const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept { return m_definitions.size(); }

private:
    std::unordered_map<BlockId, BlockDefinition> m_definitions;
    std::unordered_map<std::string, BlockId> m_nameToId;
};

/// Builds a registry populated with the core built-in block types (air, stone, dirt, coal,
/// water, tree trunk, leaf).
[[nodiscard]] BlockRegistry CreateDefaultBlockRegistry();

} // namespace voxels
