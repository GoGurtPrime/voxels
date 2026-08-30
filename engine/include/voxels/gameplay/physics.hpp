/**
 * @file physics.hpp
 * @brief AABB-based player movement and collision resolution against the voxel world.
 *
 * @details Resolves discrete collision against world blocks in 3 axes, applies gravity and jump
 *          impulses, and tracks grounded state for a per-player controller. The implementation is
 *          intentionally self-contained and does not depend on rendering or platform services.
 */

#pragma once

#include "voxels/core/math.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/world/world.hpp"

namespace voxels::gameplay {

class Physics {
public:
    static constexpr float kGravity = 22.0f;
    static constexpr float kTerminalVelocity = 54.0f;
    static constexpr float kJumpImpulse = 8.2f;
    static constexpr float kPlayerHalfWidth = 0.3f;
    static constexpr float kPlayerHalfHeight = 0.9f;

    static void Step(const World& world, Player& player, float deltaTime);
    static void Jump(Player& player);
    static bool IsGrounded(const World& world, const Player& player);
    static bool IsSolidBlock(BlockId block) noexcept;

private:
    static void ResolveAxis(const World& world, Player& player, int axis, float delta, float& axisPosition,
                            bool& grounded);
};

} // namespace voxels::gameplay
