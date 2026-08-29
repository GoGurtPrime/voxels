/*
 * Scope: Block registry implementation.
 *
 * Provides id/name lookup for block definitions and the default registration of the core
 * built-in block set used by world generation and rendering.
 *
 * Relation to the rest of the codebase: chunk storage, generation, and rendering query this
 * registry to resolve BlockId values into solidity/transparency/metadata information.
 */

#include "voxels/world/block.hpp"

namespace voxels {

BlockId BlockRegistry::RegisterBlock(BlockDefinition definition) {
    const BlockId id = definition.id;
    m_nameToId[definition.name] = id;
    m_definitions[id] = std::move(definition);
    return id;
}

bool BlockRegistry::IsRegistered(BlockId id) const noexcept {
    return m_definitions.find(id) != m_definitions.end();
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

    registry.RegisterBlock(BlockDefinition{
        BlockType::Air, static_cast<BlockId>(BlockType::Air), "air", false, true, {}});
    registry.RegisterBlock(BlockDefinition{
        BlockType::Stone, static_cast<BlockId>(BlockType::Stone), "stone", true, false, {}});
    registry.RegisterBlock(BlockDefinition{
        BlockType::Dirt, static_cast<BlockId>(BlockType::Dirt), "dirt", true, false, {}});
    registry.RegisterBlock(BlockDefinition{
        BlockType::Coal, static_cast<BlockId>(BlockType::Coal), "coal_ore", true, false, {}});
    registry.RegisterBlock(BlockDefinition{
        BlockType::Water, static_cast<BlockId>(BlockType::Water), "water", false, true, {}});
    registry.RegisterBlock(BlockDefinition{
        BlockType::Wood, static_cast<BlockId>(BlockType::Wood), "tree_trunk", true, false, {}});
    registry.RegisterBlock(BlockDefinition{
        BlockType::Leaf, static_cast<BlockId>(BlockType::Leaf), "leaf", true, true, {}});

    return registry;
}

} // namespace voxels
