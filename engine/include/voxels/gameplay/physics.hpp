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

/// Player-vs-liquid occupancy sampled at the AABB's feet, eye, and full body height.
struct SubmersionInfo {
    bool feetSubmerged = false;   ///< True when the block at the AABB's bottom face is a liquid.
    bool eyeSubmerged = false;    ///< True when the block at the camera eye offset is a liquid.
    float bodyFraction = 0.0f;    ///< 0..1 portion of the AABB's height occupied by liquid blocks.
};

class Physics {
public:
    static constexpr float kGravity = 22.0f;          ///< Downward acceleration while airborne, blocks/s^2.
    static constexpr float kTerminalVelocity = 54.0f; ///< Maximum fall speed, blocks/s.
    static constexpr float kJumpImpulse = 8.2f;       ///< Upward velocity applied by Jump(), blocks/s.
    static constexpr float kPlayerHalfWidth = 0.3f;   ///< AABB half-extent on X/Z, in blocks (centred on position).
    static constexpr float kPlayerHalfHeight = 0.9f;  ///< AABB half-extent on Y, in blocks (centred on position).
    static constexpr float kEyeOffsetFromCenter = 0.72f; ///< Camera eye height above the AABB center, in blocks.

    static constexpr float kWaterHorizontalSpeedScale = 0.45f; ///< Horizontal velocity multiplier while fully submerged.
    static constexpr float kWaterGravity = 3.0f;      ///< Downward acceleration while submerged and not swimming up.
    static constexpr float kWaterTerminalVelocity = 3.4f; ///< Maximum sink speed while submerged, blocks/s.
    static constexpr float kWaterSwimAccel = 14.0f;   ///< Upward acceleration applied while holding swim-up submerged.
    static constexpr float kWaterSwimSpeed = 3.6f;    ///< Maximum upward swim speed, blocks/s.

    /// Advances one tick: applies gravity (capped at terminal velocity) while airborne, then
    /// sweeps the player AABB axis-by-axis (X, Y, Z) against solid blocks, zeroing each blocked
    /// velocity component and refreshing onGround. Honours per-block collision bounds. While the
    /// AABB overlaps liquid blocks, horizontal speed is scaled down, free fall is arrested by a
    /// reduced gravity/terminal velocity, and holding `swimAscend` accelerates the player upward
    /// instead of the ground-only jump impulse.
    static void Step(const World& world, Player& player, float deltaTime, const BlockRegistry* registry = nullptr,
                      bool swimAscend = false);
    /// No-op unless onGround; sets vertical velocity to kJumpImpulse and clears onGround.
    static void Jump(Player& player);
    /// Probes a +/-0.01-block slab under the AABB's bottom face for solid support.
    static bool IsGrounded(const World& world, const Player& player, const BlockRegistry* registry = nullptr);
    /// With a registry, defers to BlockDefinition::isSolid; with nullptr, falls back to treating
    /// everything except Air and Water as solid.
    static bool IsSolidBlock(BlockId block, const BlockRegistry* registry = nullptr) noexcept;
    /// Samples liquid occupancy at the player's feet, eye, and across the full AABB height.
    /// With a registry, a block counts as liquid when `BlockDefinition::isLiquid` is true;
    /// with nullptr, only `BlockType::Water` counts. Missing/unloaded chunks resolve to air.
    static SubmersionInfo SampleSubmersion(const World& world, const Player& player,
                                           const BlockRegistry* registry = nullptr) noexcept;

private:
    static void ResolveAxis(const World& world, Player& player, int axis, float delta, float& axisPosition,
                            bool& grounded, const BlockRegistry* registry);
};

} // namespace voxels::gameplay
