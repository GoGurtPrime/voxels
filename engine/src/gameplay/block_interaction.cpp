/**
 * @file block_interaction.cpp
 * @brief Raycast-based block destruction and placement logic.
 *
 * @details Intersects a camera-space ray against the world, selects the first solid voxel, and
 *          either removes it or places a block adjacent to the face that was hit while rejecting
 *          placements that intersect the player body.
 */

#include "voxels/gameplay/block_interaction.hpp"

#include <algorithm>
#include <cmath>

#include "voxels/gameplay/physics.hpp"

namespace voxels::gameplay {
namespace {

bool OverlapsPlayer(const Player& player, const Vec3I& blockPos) {
    const float minX = static_cast<float>(blockPos.x);
    const float maxX = static_cast<float>(blockPos.x + 1);
    const float minY = static_cast<float>(blockPos.y);
    const float maxY = static_cast<float>(blockPos.y + 1);
    const float minZ = static_cast<float>(blockPos.z);
    const float maxZ = static_cast<float>(blockPos.z + 1);

    const float pMinX = player.state.position.x - Physics::kPlayerHalfWidth;
    const float pMaxX = player.state.position.x + Physics::kPlayerHalfWidth;
    const float pMinY = player.state.position.y - Physics::kPlayerHalfHeight;
    const float pMaxY = player.state.position.y + Physics::kPlayerHalfHeight;
    const float pMinZ = player.state.position.z - Physics::kPlayerHalfWidth;
    const float pMaxZ = player.state.position.z + Physics::kPlayerHalfWidth;

    return pMinX < maxX && pMaxX > minX && pMinY < maxY && pMaxY > minY && pMinZ < maxZ &&
           pMaxZ > minZ;
}

Vec3 ForwardVector(float yaw, float pitch) {
    const float cosPitch = std::cos(pitch);
    return {std::sin(yaw) * cosPitch, std::sin(pitch), std::cos(yaw) * cosPitch};
}

} // namespace

BlockInteraction::BlockInteraction(float reachDistance) : m_reachDistance(reachDistance) {}

InteractionResult BlockInteraction::BreakBlock(World& world, const Player& player,
                                              float reachDistanceOverride) const {
    const float reachDistance = reachDistanceOverride > 0.0f ? reachDistanceOverride : m_reachDistance;
    const Vec3 dir = ForwardVector(player.state.yaw, player.state.pitch);
    const RaycastHit hit = world.Raycast(player.state.position, dir, reachDistance);
    if (!hit.hit) {
        return {};
    }

    InteractionResult result;
    result.success = true;
    result.targetPosition = hit.blockPosition;
    result.blockId = world.GetBlock(hit.blockPosition);
    if (result.blockId != static_cast<BlockId>(BlockType::Air)) {
        const bool changed = world.SetBlock(hit.blockPosition, static_cast<BlockId>(BlockType::Air));
        result.success = changed;
        if (changed) {
            m_lastTarget = hit.blockPosition;
            m_lastFace = hit.face;
        }
    }
    return result;
}

InteractionResult BlockInteraction::PlaceBlock(World& world, const Player& player,
                                              float reachDistanceOverride) const {
    const float reachDistance = reachDistanceOverride > 0.0f ? reachDistanceOverride : m_reachDistance;
    const Vec3 dir = ForwardVector(player.state.yaw, player.state.pitch);
    RaycastHit hit = world.Raycast(player.state.position, dir, reachDistance);
    if (!hit.hit && m_lastTarget != Vec3I{}) {
        hit.blockPosition = m_lastTarget;
        hit.face = m_lastFace;
        hit.hit = true;
    }
    if (!hit.hit) {
        return {};
    }

    Vec3I placement = hit.blockPosition;
    switch (hit.face) {
        case Face::PosX: placement.x += 1; break;
        case Face::NegX: placement.x -= 1; break;
        case Face::PosY: placement.y += 1; break;
        case Face::NegY: placement.y -= 1; break;
        case Face::PosZ: placement.z += 1; break;
        case Face::NegZ: placement.z -= 1; break;
    }

    const BlockId blockId = player.state.inventory[player.state.selectedHotbarSlot] != 0
                                ? player.state.inventory[player.state.selectedHotbarSlot]
                                : static_cast<BlockId>(BlockType::Dirt);
    if (world.GetBlock(placement) != static_cast<BlockId>(BlockType::Air)) {
        return {};
    }
    if (OverlapsPlayer(player, placement)) {
        return {};
    }

    InteractionResult result;
    result.success = world.SetBlock(placement, blockId);
    result.adjacentPosition = placement;
    result.blockId = blockId;
    return result;
}

} // namespace voxels::gameplay
