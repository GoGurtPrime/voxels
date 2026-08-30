/**
 * @file camera_controller.cpp
 * @brief Input-to-camera movement translation.
 *
 * @details Maps the logical input channels and look deltas into orientation changes and movement
 *          velocities, while keeping the camera model independent of rendering and windowing details.
 */

#include "voxels/gameplay/camera_controller.hpp"

#include <cmath>

#include "voxels/gameplay/physics.hpp"

namespace voxels::gameplay {

void CameraController::Update(Player& player, const InputState& input, const GamePreferences& preferences,
                              float deltaTime) const {
    const float sensitivity = preferences.mouseSensitivity > 0.0f ? preferences.mouseSensitivity : 1.0f;
    const float yawDelta = input.mouseX * 0.0025f * sensitivity;
    const float pitchDelta = input.mouseY * 0.0025f * sensitivity * (preferences.invertY ? -1.0f : 1.0f);

    player.state.yaw += yawDelta;
    player.state.pitch = std::clamp(player.state.pitch + pitchDelta, -1.57f, 1.57f);

    const float moveX = static_cast<float>(input.moveRight) - static_cast<float>(input.moveLeft);
    const float moveZ = static_cast<float>(input.moveForward) - static_cast<float>(input.moveBackward);
    const float len = std::sqrt(moveX * moveX + moveZ * moveZ);
    if (len > 0.0f) {
        const float x = (moveX / len) * 3.5f;
        const float z = (moveZ / len) * 3.5f;
        const float cosYaw = std::cos(-player.state.yaw);
        const float sinYaw = std::sin(-player.state.yaw);
        player.state.velocity.x = x * cosYaw - z * sinYaw;
        player.state.velocity.z = x * sinYaw + z * cosYaw;
    } else {
        player.state.velocity.x = 0.0f;
        player.state.velocity.z = 0.0f;
    }

    if (input.jump && player.state.onGround) {
        Physics::Jump(player);
    }

    (void)deltaTime;
}

} // namespace voxels::gameplay
