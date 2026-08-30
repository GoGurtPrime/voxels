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
#include <limits>

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
    Vec3 origin{player.state.position.x, player.state.position.y + 0.72f, player.state.position.z};
    float remainingDistance = m_reachDistance;
    float totalDistance = 0.0f;
    RaycastHit hit = world.Raycast(origin, dir, remainingDistance);
    while (hit.hit && !targetLiquids) {
        const BlockDefinition* definition = registry.GetDefinition(world.GetBlock(hit.blockPosition));
        if (definition == nullptr || !definition->isLiquid) {
            hit.distance += totalDistance;
            return hit;
        }

        const Vec3 entry{origin.x + dir.x * hit.distance, origin.y + dir.y * hit.distance,
                         origin.z + dir.z * hit.distance};
        constexpr float kEpsilon = 0.0001f;
        float distanceToExit = std::numeric_limits<float>::infinity();
        const auto considerAxis = [&](float point, float direction, int cell) {
            if (std::abs(direction) <= kEpsilon) return;
            const float boundary = static_cast<float>(direction > 0.0f ? cell + 1 : cell);
            const float candidate = (boundary - point) / direction;
            if (candidate > kEpsilon) distanceToExit = std::min(distanceToExit, candidate);
        };
        considerAxis(entry.x, dir.x, hit.blockPosition.x);
        considerAxis(entry.y, dir.y, hit.blockPosition.y);
        considerAxis(entry.z, dir.z, hit.blockPosition.z);
        if (!std::isfinite(distanceToExit)) return {};

        const float advance = hit.distance + distanceToExit + kEpsilon;
        if (advance >= remainingDistance) return {};
        origin = {origin.x + dir.x * advance, origin.y + dir.y * advance, origin.z + dir.z * advance};
        remainingDistance -= advance;
        totalDistance += advance;
        hit = world.Raycast(origin, dir, remainingDistance);
    }
    return hit;
}

InteractionResult BlockInteraction::BreakBlock(World& world, const RaycastHit& target,
                                               const BlockRegistry& registry) const {
    if (!target.hit) {
        return {};
    }

    const BlockId blockId = world.GetBlock(target.blockPosition);
    const BlockDefinition* definition = registry.GetDefinition(blockId);
    if (definition == nullptr || definition->isLiquid || definition->hardness < 0.0f) {
        return {};
    }

    InteractionResult result;
    result.targetPosition = target.blockPosition;
    result.blockId = blockId;
    result.success = blockId != static_cast<BlockId>(BlockType::Air) &&
                     world.SetBlock(target.blockPosition, static_cast<BlockId>(BlockType::Air));
    return result;
}

InteractionResult BlockInteraction::PlaceBlock(World& world, const Player& player, const RaycastHit& target) const {
    if (!target.hit) {
        return {};
    }

    Vec3I placement = target.blockPosition;
    switch (target.face) {
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
