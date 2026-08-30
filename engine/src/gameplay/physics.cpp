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

bool HasGroundBelow(const World& world, const Player& player) {
    const float bottom = player.state.position.y - Physics::kPlayerHalfHeight;
    const float probeMinY = bottom - 0.08f;
    const float probeMaxY = bottom + 0.01f;
    const float minX = player.state.position.x - Physics::kPlayerHalfWidth + 0.02f;
    const float maxX = player.state.position.x + Physics::kPlayerHalfWidth - 0.02f;
    const float minZ = player.state.position.z - Physics::kPlayerHalfWidth + 0.02f;
    const float maxZ = player.state.position.z + Physics::kPlayerHalfWidth - 0.02f;

    for (int x = static_cast<int>(std::floor(minX)); x <= static_cast<int>(std::floor(maxX)); ++x) {
        for (int z = static_cast<int>(std::floor(minZ)); z <= static_cast<int>(std::floor(maxZ)); ++z) {
            for (int y = static_cast<int>(std::floor(probeMinY)); y <= static_cast<int>(std::floor(probeMaxY)); ++y) {
                if (Physics::IsSolidBlock(world.GetBlock(Vec3I{x, y, z}))) {
                    return true;
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
    if (std::abs(delta) < 1.0e-6f) {
        return;
    }

    const float old = axisPosition;
    const float next = old + delta;
    axisPosition = next;

    if (axis == 0) {
        const float minCoord = std::min(old, next) - kPlayerHalfWidth;
        const float maxCoord = std::max(old, next) + kPlayerHalfWidth;
        const int minCell = static_cast<int>(std::floor(minCoord));
        const int maxCell = static_cast<int>(std::ceil(maxCoord));
        float resolved = next;
        bool collided = false;

        const int startY = static_cast<int>(std::floor(player.state.position.y - kPlayerHalfHeight + 0.02f));
        const int endY = static_cast<int>(std::floor(player.state.position.y + kPlayerHalfHeight - 0.02f));
        const int startZ = static_cast<int>(std::floor(player.state.position.z - kPlayerHalfWidth + 0.02f));
        const int endZ = static_cast<int>(std::floor(player.state.position.z + kPlayerHalfWidth - 0.02f));

        for (int cell = minCell; cell <= maxCell; ++cell) {
            if ((delta > 0.0f && (static_cast<float>(cell) < old + kPlayerHalfWidth ||
                                  static_cast<float>(cell) > next + kPlayerHalfWidth)) ||
                (delta < 0.0f && (static_cast<float>(cell + 1) > old - kPlayerHalfWidth ||
                                  static_cast<float>(cell + 1) < next - kPlayerHalfWidth))) {
                continue;
            }
            for (int y = startY; y <= endY; ++y) {
                for (int z = startZ; z <= endZ; ++z) {
                    if (Physics::IsSolidBlock(world.GetBlock(Vec3I{cell, y, z}))) {
                        const float blockMin = static_cast<float>(cell);
                        const float blockMax = blockMin + 1.0f;
                        if (delta > 0.0f) {
                            resolved = std::min(resolved, blockMin - kPlayerHalfWidth);
                        } else {
                            resolved = std::max(resolved, blockMax + kPlayerHalfWidth);
                        }
                        collided = true;
                    }
                }
            }
        }
        if (collided) {
            axisPosition = resolved;
            player.state.velocity.x = 0.0f;
        }
    } else if (axis == 1) {
        const float minCoord = std::min(old, next) - kPlayerHalfHeight;
        const float maxCoord = std::max(old, next) + kPlayerHalfHeight;
        const int minCell = static_cast<int>(std::floor(minCoord));
        const int maxCell = static_cast<int>(std::ceil(maxCoord));
        float resolved = next;
        bool collided = false;

        const int startX = static_cast<int>(std::floor(player.state.position.x - kPlayerHalfWidth + 0.02f));
        const int endX = static_cast<int>(std::floor(player.state.position.x + kPlayerHalfWidth - 0.02f));
        const int startZ = static_cast<int>(std::floor(player.state.position.z - kPlayerHalfWidth + 0.02f));
        const int endZ = static_cast<int>(std::floor(player.state.position.z + kPlayerHalfWidth - 0.02f));

        for (int cell = minCell; cell <= maxCell; ++cell) {
            if ((delta > 0.0f && (static_cast<float>(cell) < old + kPlayerHalfHeight ||
                                  static_cast<float>(cell) > next + kPlayerHalfHeight)) ||
                (delta < 0.0f && (static_cast<float>(cell + 1) > old - kPlayerHalfHeight ||
                                  static_cast<float>(cell + 1) < next - kPlayerHalfHeight))) {
                continue;
            }
            for (int x = startX; x <= endX; ++x) {
                for (int z = startZ; z <= endZ; ++z) {
                    if (Physics::IsSolidBlock(world.GetBlock(Vec3I{x, cell, z}))) {
                        const float blockMin = static_cast<float>(cell);
                        const float blockMax = blockMin + 1.0f;
                        if (delta > 0.0f) {
                            resolved = std::min(resolved, blockMin - kPlayerHalfHeight);
                        } else {
                            resolved = std::max(resolved, blockMax + kPlayerHalfHeight);
                            grounded = true;
                        }
                        collided = true;
                    }
                }
            }
        }
        if (collided) {
            axisPosition = resolved;
            player.state.velocity.y = 0.0f;
        }
    } else if (axis == 2) {
        const float minCoord = std::min(old, next) - kPlayerHalfWidth;
        const float maxCoord = std::max(old, next) + kPlayerHalfWidth;
        const int minCell = static_cast<int>(std::floor(minCoord));
        const int maxCell = static_cast<int>(std::ceil(maxCoord));
        float resolved = next;
        bool collided = false;

        const int startX = static_cast<int>(std::floor(player.state.position.x - kPlayerHalfWidth + 0.02f));
        const int endX = static_cast<int>(std::floor(player.state.position.x + kPlayerHalfWidth - 0.02f));
        const int startY = static_cast<int>(std::floor(player.state.position.y - kPlayerHalfHeight + 0.02f));
        const int endY = static_cast<int>(std::floor(player.state.position.y + kPlayerHalfHeight - 0.02f));

        for (int cell = minCell; cell <= maxCell; ++cell) {
            if ((delta > 0.0f && (static_cast<float>(cell) < old + kPlayerHalfWidth ||
                                  static_cast<float>(cell) > next + kPlayerHalfWidth)) ||
                (delta < 0.0f && (static_cast<float>(cell + 1) > old - kPlayerHalfWidth ||
                                  static_cast<float>(cell + 1) < next - kPlayerHalfWidth))) {
                continue;
            }
            for (int x = startX; x <= endX; ++x) {
                for (int y = startY; y <= endY; ++y) {
                    if (Physics::IsSolidBlock(world.GetBlock(Vec3I{x, y, cell}))) {
                        const float blockMin = static_cast<float>(cell);
                        const float blockMax = blockMin + 1.0f;
                        if (delta > 0.0f) {
                            resolved = std::min(resolved, blockMin - kPlayerHalfWidth);
                        } else {
                            resolved = std::max(resolved, blockMax + kPlayerHalfWidth);
                        }
                        collided = true;
                    }
                }
            }
        }
        if (collided) {
            axisPosition = resolved;
            player.state.velocity.z = 0.0f;
        }
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
