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
    static constexpr float kGravity = 22.0f;          ///< Downward acceleration while airborne, blocks/s^2.
    static constexpr float kTerminalVelocity = 54.0f; ///< Maximum fall speed, blocks/s.
    static constexpr float kJumpImpulse = 8.2f;       ///< Upward velocity applied by Jump(), blocks/s.
    static constexpr float kPlayerHalfWidth = 0.3f;   ///< AABB half-extent on X/Z, in blocks (centred on position).
    static constexpr float kPlayerHalfHeight = 0.9f;  ///< AABB half-extent on Y, in blocks (centred on position).

    /// Advances one tick: applies gravity (capped at terminal velocity) while airborne, then
    /// sweeps the player AABB axis-by-axis (X, Y, Z) against solid blocks, zeroing each blocked
    /// velocity component and refreshing onGround. Honours per-block collision bounds.
    static void Step(const World& world, Player& player, float deltaTime, const BlockRegistry* registry = nullptr);
    /// No-op unless onGround; sets vertical velocity to kJumpImpulse and clears onGround.
    static void Jump(Player& player);
    /// Probes a +/-0.01-block slab under the AABB's bottom face for solid support.
    static bool IsGrounded(const World& world, const Player& player, const BlockRegistry* registry = nullptr);
    /// With a registry, defers to BlockDefinition::isSolid; with nullptr, falls back to treating
    /// everything except Air and Water as solid.
    static bool IsSolidBlock(BlockId block, const BlockRegistry* registry = nullptr) noexcept;

private:
    static void ResolveAxis(const World& world, Player& player, int axis, float delta, float& axisPosition,
                            bool& grounded, const BlockRegistry* registry);
};

} // namespace voxels::gameplay
