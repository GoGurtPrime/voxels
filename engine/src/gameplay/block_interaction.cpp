/**
 * @file block_interaction.cpp
 * @brief Raycast-based block destruction and placement logic.
 *
 * @details Intersects a camera-space ray against the world, selects the first solid voxel, and
 *          either removes it or places a block adjacent to the face that was hit while rejecting
 *          placements that intersect the player body.
 */

#include "voxels/gameplay/block_interaction.hpp"

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
    const Vec3 origin{player.state.position.x, player.state.position.y + 0.72f, player.state.position.z};
    // A single continuous traversal (never restarting the ray at a nudged origin) so the
    // reported hit face is always the true entry face, even when the eye or the approach path
    // is submerged in several consecutive liquid voxels.
    return world.Raycast(origin, dir, m_reachDistance, targetLiquids ? nullptr : &registry);
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

InteractionResult BlockInteraction::PlaceBlock(World& world, const Player& player, const RaycastHit& target,
                                               const BlockRegistry& registry) const {
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
    const BlockDefinition* heldDefinition = registry.GetDefinition(blockId);
    if (heldDefinition == nullptr || !heldDefinition->isPlaceable) return {};

    const BlockId existing = world.GetBlock(placement);
    if (existing != static_cast<BlockId>(BlockType::Air)) {
        const BlockDefinition* existingDefinition = registry.GetDefinition(existing);
        if (existingDefinition == nullptr || !existingDefinition->isReplaceable) return {};
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
