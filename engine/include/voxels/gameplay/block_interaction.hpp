/**
 * @file block_interaction.hpp
 * @brief Player block breaking and placement using camera-space raycast selection.
 *
 * @details Uses the world raycast hit result and the player's camera orientation to determine which
 *          voxel the player is targeting and whether the requested placement would overlap the player
 *          body. This keeps block interaction deterministic and decoupled from any rendering backend.
 */

#pragma once

#include "voxels/gameplay/player.hpp"
#include "voxels/world/world.hpp"

namespace voxels::gameplay {

/// Outcome of a break or place attempt; when `success` is false the world was not modified.
struct InteractionResult {
    bool success = false;
    Vec3I targetPosition{}; ///< World block coords of the broken block (filled by BreakBlock only).
    Vec3I adjacentPosition{}; ///< World cell the new block was placed into (filled by PlaceBlock only).
    BlockId blockId = static_cast<BlockId>(BlockType::Air); ///< Block removed (break) or placed (place).
};

class BlockInteraction {
public:
    /// `reachDistance` is the maximum targeting range along the view ray, in blocks.
    explicit BlockInteraction(float reachDistance = 5.0f);

    /// Raycasts from the eye point (0.72 blocks above the AABB centre) along the look direction.
    /// Liquid cells are skipped (ray continues through them) unless `targetLiquids` is true;
    /// a miss returns a hit with `hit == false`.
    [[nodiscard]] RaycastHit Target(const World& world, const Player& player, const BlockRegistry& registry,
                                    bool targetLiquids = false) const;
    /// Sets the targeted block to air, reporting the removed id. Fails on a miss, on liquids,
    /// and on negative-hardness (unbreakable) blocks. Drops/inventory are the caller's job.
    [[nodiscard]] InteractionResult BreakBlock(World& world, const RaycastHit& target, const BlockRegistry& registry) const;
    /// Places the player's selected hotbar block into the cell adjacent to the hit face. Fails
    /// when holding nothing, the held item is not placeable, the cell is occupied, or it would
    /// overlap the player AABB. The held stack is not decremented here.
    [[nodiscard]] InteractionResult PlaceBlock(World& world, const Player& player, const RaycastHit& target,
                                               const BlockRegistry& registry) const;

private:
    float m_reachDistance;
};

} // namespace voxels::gameplay
