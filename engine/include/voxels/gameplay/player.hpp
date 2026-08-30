/**
 * @file player.hpp
 * @brief Player state and per-peer gameplay entity definition.
 *
 * @details Captures the local player's transform, movement state, hotbar, and health metadata in
 *          a trivially-serializable object so physics and other subsystems can operate on a single
 *          player instance without any global state or engine-singleton coupling.
 */

#pragma once

#include <array>

#include "voxels/core/game_types.hpp"
#include "voxels/core/math.hpp"
#include "voxels/world/block.hpp"

namespace voxels {

struct PlayerState {
    Vec3 position{0.0f, 1.9f, 0.0f};
    Vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool onGround = false;
    int selectedHotbarSlot = 0;
    float health = 100.0f;
    std::array<BlockId, 10> inventory{};

    [[nodiscard]] bool operator==(const PlayerState&) const noexcept = default;
};

class Player {
public:
    Player() = default;
    explicit Player(PlayerState initialState) : state(std::move(initialState)) {}

    PlayerState state{};
};

} // namespace voxels
