/**
 * @file player.hpp
 * @brief Player state and per-peer gameplay entity definition.
 *
 * @details Captures the local player's transform, movement state, hotbar, and health metadata in
 *          a trivially-serializable object so physics and other subsystems can operate on a single
 *          player instance without any global state or engine-singleton coupling.
 */

#pragma once

#include "voxels/core/game_types.hpp"
#include "voxels/gameplay/inventory.hpp"
#include "voxels/core/math.hpp"
#include "voxels/world/block.hpp"

namespace voxels {

/// Trivially-copyable player snapshot; equality-comparable so tests and replication can diff it.
struct PlayerState {
    Vec3 position{0.0f, 1.9f, 0.0f}; ///< World-space centre of the collision AABB, in blocks (half-extents in Physics).
    Vec3 velocity{0.0f}; ///< Blocks per second.
    float yaw = 0.0f; ///< Look yaw in radians; 0 faces -Z (Camera convention).
    float pitch = 0.0f; ///< Look pitch in radians; positive looks up.
    bool onGround = false;
    float health = 100.0f;
    gameplay::Inventory inventory{};

    [[nodiscard]] bool operator==(const PlayerState&) const noexcept = default;
};

/// Thin wrapper giving physics, input, and interaction one shared mutable PlayerState.
class Player {
public:
    Player() = default;
    explicit Player(PlayerState initialState) : state(std::move(initialState)) {}

    PlayerState state{};
};

} // namespace voxels
