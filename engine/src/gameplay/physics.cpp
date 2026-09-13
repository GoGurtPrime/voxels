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

bool HasGroundBelow(const World& world, const Player& player, const BlockRegistry* registry) {
    const float bottom = player.state.position.y - Physics::kPlayerHalfHeight;
    const float probeMinY = bottom - 0.01f;
    const float probeMaxY = bottom + 0.01f;
    const float minX = player.state.position.x - Physics::kPlayerHalfWidth + 0.02f;
    const float maxX = player.state.position.x + Physics::kPlayerHalfWidth - 0.02f;
    const float minZ = player.state.position.z - Physics::kPlayerHalfWidth + 0.02f;
    const float maxZ = player.state.position.z + Physics::kPlayerHalfWidth - 0.02f;

    for (int x = static_cast<int>(std::floor(minX)); x <= static_cast<int>(std::floor(maxX)); ++x) {
        for (int z = static_cast<int>(std::floor(minZ)); z <= static_cast<int>(std::floor(maxZ)); ++z) {
            for (int y = static_cast<int>(std::floor(probeMinY)); y <= static_cast<int>(std::floor(probeMaxY)); ++y) {
                const BlockId block = world.GetBlock(Vec3I{x, y, z});
                const BlockDefinition* definition = registry == nullptr ? nullptr : registry->GetDefinition(block);
                const float top = static_cast<float>(y) + (definition == nullptr ? 1.0f : definition->collisionBounds.max[1]);
                if (Physics::IsSolidBlock(block, registry) && top >= probeMinY && top <= probeMaxY) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

bool Physics::IsSolidBlock(BlockId block, const BlockRegistry* registry) noexcept {
    if (registry != nullptr) {
        const BlockDefinition* definition = registry->GetDefinition(block);
        return definition != nullptr && definition->isSolid;
    }
    return block != static_cast<BlockId>(BlockType::Air) && block != static_cast<BlockId>(BlockType::Water);
}

namespace {
bool IsLiquidAt(const World& world, const BlockRegistry* registry, int x, float y, int z) {
    const BlockId block = world.GetBlock(Vec3I{x, static_cast<int>(std::floor(y)), z});
    if (registry != nullptr) {
        const BlockDefinition* definition = registry->GetDefinition(block);
        return definition != nullptr && definition->isLiquid;
    }
    return block == static_cast<BlockId>(BlockType::Water);
}

/// Finds the liquid surface (top boundary) reachable from `fromY` at (x,z). Searches downward
/// first, within `kSearchWindow` blocks, for the nearest liquid cell - covering the brief airborne
/// arc while climbing out of water onto a ledge, where the AABB has already cleared the surface
/// but the player is still mid-climb - then climbs upward from there to the true top boundary.
/// Kept deliberately small (just over the tallest climbable step) so a player still falling toward
/// water from well above it is not affected before actually touching the surface.
bool FindNearbyLiquidSurfaceTopY(const World& world, const BlockRegistry* registry, int x, int z, float fromY,
                                 float& outSurfaceTopY) {
    constexpr float kSearchWindow = 2.2f;
    const int startY = static_cast<int>(std::floor(fromY));
    const int minY = startY - static_cast<int>(kSearchWindow);
    for (int probe = startY; probe >= minY; --probe) {
        if (!IsLiquidAt(world, registry, x, static_cast<float>(probe) + 0.5f, z)) continue;
        int y = probe;
        while (IsLiquidAt(world, registry, x, static_cast<float>(y) + 0.5f, z)) {
            ++y;
        }
        outSurfaceTopY = static_cast<float>(y);
        return true;
    }
    return false;
}

/// A ledge is climbable while swimming when solid ground begins at or within one block above the
/// water surface (a flush shoreline, or a single step up) with two clear blocks of headroom above
/// it to actually stand on; anything taller returns false so the player must break a block instead
/// of swim-climbing an arbitrary wall.
bool TryFindClimbableLedgeTop(const World& world, const BlockRegistry* registry, int x, int z,
                              float surfaceTopY, float& outLedgeTopY) {
    const int groundY = static_cast<int>(std::floor(surfaceTopY));
    // Solid ground may start one block below the surface (a flush shore, the common case), right
    // at the surface (a one-block step), or not at all within reach.
    int y = groundY - 1;
    while (y <= groundY + 1 && !Physics::IsSolidBlock(world.GetBlock(Vec3I{x, y, z}), registry)) {
        ++y;
    }
    if (y > groundY + 1) return false; // no solid ground within reach of the surface

    // Climb through any contiguous solid stack to find its true top.
    while (Physics::IsSolidBlock(world.GetBlock(Vec3I{x, y, z}), registry)) {
        ++y;
    }
    const float ledgeTopY = static_cast<float>(y);
    if (ledgeTopY > surfaceTopY + 1.0f + 1.0e-4f) return false; // too tall to swim-climb
    if (Physics::IsSolidBlock(world.GetBlock(Vec3I{x, y + 1, z}), registry)) return false; // no headroom

    outLedgeTopY = ledgeTopY;
    return true;
}
} // namespace

SubmersionInfo Physics::SampleSubmersion(const World& world, const Player& player, const BlockRegistry* registry) noexcept {
    SubmersionInfo info{};
    const int x = static_cast<int>(std::floor(player.state.position.x));
    const int z = static_cast<int>(std::floor(player.state.position.z));
    const float feetY = player.state.position.y - kPlayerHalfHeight;
    const float headY = player.state.position.y + kPlayerHalfHeight;
    const float eyeY = player.state.position.y + kEyeOffsetFromCenter;

    info.feetSubmerged = IsLiquidAt(world, registry, x, feetY + 0.05f, z);
    info.eyeSubmerged = IsLiquidAt(world, registry, x, eyeY, z);

    float submergedHeight = 0.0f;
    const int startY = static_cast<int>(std::floor(feetY));
    const int endY = static_cast<int>(std::floor(headY));
    for (int y = startY; y <= endY; ++y) {
        if (!IsLiquidAt(world, registry, x, static_cast<float>(y) + 0.5f, z)) continue;
        const float cellMin = std::max(feetY, static_cast<float>(y));
        const float cellMax = std::min(headY, static_cast<float>(y) + 1.0f);
        if (cellMax > cellMin) submergedHeight += cellMax - cellMin;
    }
    const float totalHeight = headY - feetY;
    info.bodyFraction = totalHeight > 0.0f ? std::clamp(submergedHeight / totalHeight, 0.0f, 1.0f) : 0.0f;
    return info;
}

bool Physics::IsGrounded(const World& world, const Player& player, const BlockRegistry* registry) {
    return HasGroundBelow(world, player, registry);
}

void Physics::Jump(Player& player) {
    if (!player.state.onGround) {
        return;
    }
    player.state.velocity.y = kJumpImpulse;
    player.state.onGround = false;
}

void Physics::ResolveAxis(const World& world, Player& player, int axis, float delta, float& axisPosition,
                         bool& grounded, const BlockRegistry* registry) {
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
            for (int y = startY; y <= endY; ++y) {
                for (int z = startZ; z <= endZ; ++z) {
                    const BlockId block = world.GetBlock(Vec3I{cell, y, z});
                    const BlockDefinition* definition = registry == nullptr ? nullptr : registry->GetDefinition(block);
                    if (Physics::IsSolidBlock(block, registry)) {
                        const float blockMin = static_cast<float>(cell) + (definition == nullptr ? 0.0f : definition->collisionBounds.min[0]);
                        const float blockMax = static_cast<float>(cell) + (definition == nullptr ? 1.0f : definition->collisionBounds.max[0]);
                        if ((delta > 0.0f && (blockMin < old + kPlayerHalfWidth || blockMin > next + kPlayerHalfWidth)) ||
                            (delta < 0.0f && (blockMax > old - kPlayerHalfWidth || blockMax < next - kPlayerHalfWidth))) continue;
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
            for (int x = startX; x <= endX; ++x) {
                for (int z = startZ; z <= endZ; ++z) {
                    const BlockId block = world.GetBlock(Vec3I{x, cell, z});
                    const BlockDefinition* definition = registry == nullptr ? nullptr : registry->GetDefinition(block);
                    if (Physics::IsSolidBlock(block, registry)) {
                        const float blockMin = static_cast<float>(cell) + (definition == nullptr ? 0.0f : definition->collisionBounds.min[1]);
                        const float blockMax = static_cast<float>(cell) + (definition == nullptr ? 1.0f : definition->collisionBounds.max[1]);
                        if ((delta > 0.0f && (blockMin < old + kPlayerHalfHeight || blockMin > next + kPlayerHalfHeight)) ||
                            (delta < 0.0f && (blockMax > old - kPlayerHalfHeight || blockMax < next - kPlayerHalfHeight))) continue;
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
            for (int x = startX; x <= endX; ++x) {
                for (int y = startY; y <= endY; ++y) {
                    const BlockId block = world.GetBlock(Vec3I{x, y, cell});
                    const BlockDefinition* definition = registry == nullptr ? nullptr : registry->GetDefinition(block);
                    if (Physics::IsSolidBlock(block, registry)) {
                        const float blockMin = static_cast<float>(cell) + (definition == nullptr ? 0.0f : definition->collisionBounds.min[2]);
                        const float blockMax = static_cast<float>(cell) + (definition == nullptr ? 1.0f : definition->collisionBounds.max[2]);
                        if ((delta > 0.0f && (blockMin < old + kPlayerHalfWidth || blockMin > next + kPlayerHalfWidth)) ||
                            (delta < 0.0f && (blockMax > old - kPlayerHalfWidth || blockMax < next - kPlayerHalfWidth))) continue;
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

void Physics::Step(const World& world, Player& player, float deltaTime, const BlockRegistry* registry,
                    bool swimAscend) {
    if (deltaTime <= 0.0f) {
        return;
    }

    const SubmersionInfo submersion = SampleSubmersion(world, player, registry);
    const bool swimming = submersion.bodyFraction > 0.0f;
    const int columnX = static_cast<int>(std::floor(player.state.position.x));
    const int columnZ = static_cast<int>(std::floor(player.state.position.z));
    float surfaceTopY = 0.0f;
    const bool nearLiquidSurface = FindNearbyLiquidSurfaceTopY(world, registry, columnX, columnZ,
                                                               player.state.position.y - kPlayerHalfHeight, surfaceTopY);
    // Swim-assist (the tread/climb ceiling below) stays active through the brief airborne arc of
    // climbing out onto a ledge, where the AABB has already cleared the water but hasn't yet
    // landed - gating purely on current submersion would let normal gravity yank the player back
    // down mid-climb, oscillating forever instead of completing the exit.
    const bool swimAssistActive = swimming || (swimAscend && !player.state.onGround && nearLiquidSurface);
    float swimCeilingCenterY = 0.0f;

    if (swimAssistActive) {
        if (swimming) {
            const float speedScale = 1.0f - submersion.bodyFraction * (1.0f - kWaterHorizontalSpeedScale);
            player.state.velocity.x *= speedScale;
            player.state.velocity.z *= speedScale;
        }

        // Treading water floats the player mostly submerged with only the head poking out - this
        // is the ceiling that stops holding space from just walking across the top of the water.
        swimCeilingCenterY = surfaceTopY + kSwimSurfaceClearance - kEyeOffsetFromCenter;

        const float horizontalSpeedSq = player.state.velocity.x * player.state.velocity.x +
                                        player.state.velocity.z * player.state.velocity.z;
        if (horizontalSpeedSq > 1.0e-4f) {
            const float invSpeed = 1.0f / std::sqrt(horizontalSpeedSq);
            const float probeX = player.state.position.x + player.state.velocity.x * invSpeed * (kPlayerHalfWidth + 0.15f);
            const float probeZ = player.state.position.z + player.state.velocity.z * invSpeed * (kPlayerHalfWidth + 0.15f);
            float ledgeTopY = 0.0f;
            if (TryFindClimbableLedgeTop(world, registry, static_cast<int>(std::floor(probeX)),
                                         static_cast<int>(std::floor(probeZ)), surfaceTopY, ledgeTopY)) {
                swimCeilingCenterY = std::max(swimCeilingCenterY, ledgeTopY + kPlayerHalfHeight);
            }
        }

        if (swimAscend) {
            // A proportional approach onto the ceiling (rather than accelerating into it and
            // then reversing under gravity) converges smoothly without the sink/climb oscillation
            // a fixed-acceleration model produces right at the surface or against a ledge.
            const float error = swimCeilingCenterY - player.state.position.y;
            player.state.velocity.y = std::clamp(error * kSwimCeilingGain, -kWaterTerminalVelocity, kWaterSwimSpeed);
        } else if (!player.state.onGround) {
            player.state.velocity.y = std::max(player.state.velocity.y - kWaterGravity * deltaTime, -kWaterTerminalVelocity);
        }
        // Arrests any fall speed accumulated before entering the water immediately, rather than
        // decaying it over several ticks - a real splash should visibly slow the player at once.
        player.state.velocity.y = std::clamp(player.state.velocity.y, -kWaterTerminalVelocity, kWaterSwimSpeed);
    } else {
        if (player.state.onGround) {
            player.state.velocity.y = std::max(0.0f, player.state.velocity.y);
        }
        const float gravityStep = kGravity * deltaTime;
        if (!player.state.onGround) {
            player.state.velocity.y = std::max(-kTerminalVelocity, player.state.velocity.y - gravityStep);
        }
    }

    const float stepX = player.state.velocity.x * deltaTime;
    const float stepY = player.state.velocity.y * deltaTime;
    const float stepZ = player.state.velocity.z * deltaTime;

    bool grounded = false;
    ResolveAxis(world, player, 0, stepX, player.state.position.x, grounded, registry);
    ResolveAxis(world, player, 1, stepY, player.state.position.y, grounded, registry);
    ResolveAxis(world, player, 2, stepZ, player.state.position.z, grounded, registry);

    player.state.onGround = grounded || IsGrounded(world, player, registry);
    if (player.state.onGround && player.state.velocity.y < 0.0f) {
        player.state.velocity.y = 0.0f;
    }
}

} // namespace voxels::gameplay
