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

struct InteractionResult {
    bool success = false;
    Vec3I targetPosition{};
    Vec3I adjacentPosition{};
    BlockId blockId = static_cast<BlockId>(BlockType::Air);
};

class BlockInteraction {
public:
    explicit BlockInteraction(float reachDistance = 5.0f);

    [[nodiscard]] RaycastHit Target(const World& world, const Player& player, const BlockRegistry& registry,
                                    bool targetLiquids = false) const;
    [[nodiscard]] InteractionResult BreakBlock(World& world, const Player& player, float reachDistanceOverride = -1.0f) const;
    [[nodiscard]] InteractionResult PlaceBlock(World& world, const Player& player, float reachDistanceOverride = -1.0f) const;

    void SetLastTarget(const Vec3I& position, Face face) {
        m_lastTarget = position;
        m_lastFace = face;
    }

private:
    float m_reachDistance;
    mutable Vec3I m_lastTarget{};
    mutable Face m_lastFace = Face::PosY;
};

} // namespace voxels::gameplay
