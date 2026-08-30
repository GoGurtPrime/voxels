/**
 * @file physics.cpp
 * @brief Voxel collision and gravity simulation for players.
 *
 * @details Applies gravity, resolves collisions against solid blocks by sweeping along each axis,
 *          and keeps a grounded/airborne state accurate for the active player and future peers.
 */

#include "voxels/gameplay/physics.hpp"

#include <algorithm>
#include <cmath>

namespace voxels::gameplay {
namespace {

struct AABB {
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
};

AABB PlayerAABB(const Player& player) {
    const float hx = Physics::kPlayerHalfWidth;
    const float hy = Physics::kPlayerHalfHeight;
    return {player.state.position.x - hx, player.state.position.y - hy, player.state.position.z - hx,
            player.state.position.x + hx, player.state.position.y + hy, player.state.position.z + hx};
}

bool BoxIntersectsBlock(const AABB& box, const Vec3I& blockPos) {
    const float minX = static_cast<float>(blockPos.x);
    const float maxX = static_cast<float>(blockPos.x + 1);
    const float minY = static_cast<float>(blockPos.y);
    const float maxY = static_cast<float>(blockPos.y + 1);
    const float minZ = static_cast<float>(blockPos.z);
    const float maxZ = static_cast<float>(blockPos.z + 1);

    return box.minX < maxX && box.maxX > minX && box.minY < maxY && box.maxY > minY &&
           box.minZ < maxZ && box.maxZ > minZ;
}

bool IntersectsAnySolid(const World& world, const AABB& box) {
    const int minX = static_cast<int>(std::floor(box.minX));
    const int maxX = static_cast<int>(std::floor(box.maxX));
    const int minY = static_cast<int>(std::floor(box.minY));
    const int maxY = static_cast<int>(std::floor(box.maxY));
    const int minZ = static_cast<int>(std::floor(box.minZ));
    const int maxZ = static_cast<int>(std::floor(box.maxZ));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                const Vec3I pos{x, y, z};
                if (Physics::IsSolidBlock(world.GetBlock(pos)) && BoxIntersectsBlock(box, pos)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool HasGroundBelow(const World& world, const Player& player) {
    const float bottom = player.state.position.y - Physics::kPlayerHalfHeight;
    const float probeMinY = bottom - 0.05f;
    const float probeMaxY = bottom + 0.05f;
    const float minX = player.state.position.x - Physics::kPlayerHalfWidth;
    const float maxX = player.state.position.x + Physics::kPlayerHalfWidth;
    const float minZ = player.state.position.z - Physics::kPlayerHalfWidth;
    const float maxZ = player.state.position.z + Physics::kPlayerHalfWidth;

    for (int x = static_cast<int>(std::floor(minX)); x <= static_cast<int>(std::floor(maxX)); ++x) {
        for (int z = static_cast<int>(std::floor(minZ)); z <= static_cast<int>(std::floor(maxZ)); ++z) {
            for (int y = static_cast<int>(std::floor(probeMinY)); y <= static_cast<int>(std::floor(probeMaxY)); ++y) {
                if (Physics::IsSolidBlock(world.GetBlock(Vec3I{x, y, z}))) {
                    const AABB probe{minX, probeMinY, minZ, maxX, probeMaxY, maxZ};
                    if (BoxIntersectsBlock(probe, Vec3I{x, y, z})) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // namespace

bool Physics::IsSolidBlock(BlockId block) noexcept {
    return block != static_cast<BlockId>(BlockType::Air) && block != static_cast<BlockId>(BlockType::Water);
}

bool Physics::IsGrounded(const World& world, const Player& player) {
    return HasGroundBelow(world, player);
}

void Physics::Jump(Player& player) {
    if (!player.state.onGround) {
        return;
    }
    player.state.velocity.y = kJumpImpulse;
    player.state.onGround = false;
}

void Physics::ResolveAxis(const World& world, Player& player, int axis, float delta, float& axisPosition,
                         bool& grounded) {
    const float old = axisPosition;
    const float next = old + delta;
    axisPosition = next;

    if (axis == 0 || axis == 2) {
        const float minCoord = std::min(old, next) - kPlayerHalfWidth;
        const float maxCoord = std::max(old, next) + kPlayerHalfWidth;
        const int minCell = static_cast<int>(std::floor(minCoord));
        const int maxCell = static_cast<int>(std::ceil(maxCoord));
        float resolvedBoundary = old;
        bool collided = false;

        for (int cell = minCell; cell <= maxCell; ++cell) {
            Vec3I probe;
            if (axis == 0) {
                probe = Vec3I{cell, static_cast<int>(std::floor(player.state.position.y)),
                              static_cast<int>(std::floor(player.state.position.z))};
            } else {
                probe = Vec3I{static_cast<int>(std::floor(player.state.position.x)),
                              static_cast<int>(std::floor(player.state.position.y)), cell};
            }

            if (!Physics::IsSolidBlock(world.GetBlock(probe))) {
                continue;
            }

            const float blockMin = static_cast<float>(cell);
            const float blockMax = blockMin + 1.0f;
            if (delta > 0.0f) {
                resolvedBoundary = std::min(resolvedBoundary, blockMin - kPlayerHalfWidth);
            } else {
                resolvedBoundary = std::max(resolvedBoundary, blockMax + kPlayerHalfWidth);
            }
            collided = true;
        }

        if (collided) {
            axisPosition = resolvedBoundary;
            player.state.velocity[axis] = 0.0f;
        }
        return;
    }

    AABB box = PlayerAABB(player);
    box.minY = axisPosition - kPlayerHalfHeight;
    box.maxY = axisPosition + kPlayerHalfHeight;

    if (IntersectsAnySolid(world, box)) {
        if (delta > 0.0f) {
            grounded = true;
        }
        player.state.velocity.y = 0.0f;
        axisPosition = old;
    }
}

void Physics::Step(const World& world, Player& player, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    if (player.state.onGround) {
        player.state.velocity.y = std::max(0.0f, player.state.velocity.y);
    }

    const float gravityStep = kGravity * deltaTime;
    if (!player.state.onGround) {
        player.state.velocity.y = std::max(-kTerminalVelocity, player.state.velocity.y - gravityStep);
    }

    const float stepX = player.state.velocity.x * deltaTime;
    const float stepY = player.state.velocity.y * deltaTime;
    const float stepZ = player.state.velocity.z * deltaTime;

    bool grounded = false;
    ResolveAxis(world, player, 0, stepX, player.state.position.x, grounded);
    ResolveAxis(world, player, 1, stepY, player.state.position.y, grounded);
    ResolveAxis(world, player, 2, stepZ, player.state.position.z, grounded);

    player.state.onGround = grounded || IsGrounded(world, player);
    if (player.state.onGround && player.state.velocity.y < 0.0f) {
        player.state.velocity.y = 0.0f;
    }
}

} // namespace voxels::gameplay
