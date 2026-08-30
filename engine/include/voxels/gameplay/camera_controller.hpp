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
    void Update(Player& player, const InputState& input, const GamePreferences& preferences,
                float deltaTime) const;
};

} // namespace voxels::gameplay
