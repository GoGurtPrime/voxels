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
    return {-std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch};
}

} // namespace

BlockInteraction::BlockInteraction(float reachDistance) : m_reachDistance(reachDistance) {}

RaycastHit BlockInteraction::Target(const World& world, const Player& player, const BlockRegistry& registry,
                                    bool targetLiquids) const {
    const Vec3 dir = ForwardVector(player.state.yaw, player.state.pitch);
    const Vec3 eye{player.state.position.x, player.state.position.y + 0.72f, player.state.position.z};
    RaycastHit hit = world.Raycast(eye, dir, m_reachDistance);
    while (hit.hit && !targetLiquids) {
        const BlockDefinition* definition = registry.GetDefinition(world.GetBlock(hit.blockPosition));
        if (definition == nullptr || !definition->isLiquid) return hit;
        const float nextDistance = hit.distance + 0.001f;
        if (nextDistance >= m_reachDistance) return {};
        const Vec3 nextOrigin{eye.x + dir.x * nextDistance, eye.y + dir.y * nextDistance, eye.z + dir.z * nextDistance};
        hit = world.Raycast(nextOrigin, dir, m_reachDistance - nextDistance);
        hit.distance += nextDistance;
    }
    return hit;
}

InteractionResult BlockInteraction::BreakBlock(World& world, const Player& player,
                                              float reachDistanceOverride) const {
    const float reachDistance = reachDistanceOverride > 0.0f ? reachDistanceOverride : m_reachDistance;
    const Vec3 dir = ForwardVector(player.state.yaw, player.state.pitch);
    const Vec3 eye{player.state.position.x, player.state.position.y + 0.72f, player.state.position.z};
    const RaycastHit hit = world.Raycast(eye, dir, reachDistance);
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
    const Vec3 eye{player.state.position.x, player.state.position.y + 0.72f, player.state.position.z};
    RaycastHit hit = world.Raycast(eye, dir, reachDistance);
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

    const ItemStack& heldStack = player.state.inventory.GetSelectedStack();
    const BlockId blockId = heldStack.IsEmpty() ? static_cast<BlockId>(BlockType::Air) : heldStack.blockId;
    if (blockId == static_cast<BlockId>(BlockType::Air)) return {};
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
