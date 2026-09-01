/**
 * @file camera_controller.hpp
 * @brief Converts input state into player yaw/pitch and movement velocity.
 *
 * @details Translates a local input state into the camera orientation and velocity used by the
 *          gameplay loop. This remains isolated from the renderer so it can be unit-tested and
 *          reused by multiple player instances or future networked peers.
 */

#pragma once

#include "voxels/core/game_types.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/input/input_manager.hpp"

namespace voxels::gameplay {

class CameraController {
public:
    /// Applies mouse-look (radians; yaw wrapped to (-pi, pi], pitch clamped to +/-1.55) and sets
    /// horizontal velocity in the yaw frame (4.3 walk / 7.0 sprint blocks/s), leaving vertical
    /// velocity to Physics. `deltaTime` is currently unused - mouse deltas arrive per-frame.
    void Update(Player& player, const InputState& input, const GamePreferences& preferences,
                float deltaTime) const;
};

} // namespace voxels::gameplay
