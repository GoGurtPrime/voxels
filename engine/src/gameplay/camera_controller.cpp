/**
 * @file camera_controller.cpp
 * @brief Input-to-camera movement translation.
 *
 * @details Maps the logical input channels and look deltas into orientation changes and movement
 *          velocities, while keeping the camera model independent of rendering and windowing details.
 */

#include "voxels/gameplay/camera_controller.hpp"

#include <cmath>

#include <glm/gtc/constants.hpp>

#include "voxels/gameplay/physics.hpp"

namespace voxels::gameplay {

void CameraController::Update(Player& player, const InputState& input, const GamePreferences& preferences,
                              float deltaTime) const {
    const float sensitivity = preferences.mouseSensitivity > 0.0f ? preferences.mouseSensitivity : 1.0f;
    const float yawDelta = -input.mouseX * 0.003f * sensitivity;
    const float pitchDelta = input.mouseY * 0.003f * sensitivity * (preferences.invertY ? 1.0f : -1.0f);

    player.state.yaw += yawDelta;
    if (player.state.yaw > glm::pi<float>()) {
        player.state.yaw -= glm::two_pi<float>();
    } else if (player.state.yaw <= -glm::pi<float>()) {
        player.state.yaw += glm::two_pi<float>();
    }
    player.state.pitch = std::clamp(player.state.pitch + pitchDelta, -1.55f, 1.55f);

    const float inputRight = static_cast<float>(input.moveRight) - static_cast<float>(input.moveLeft);
    const float inputForward = static_cast<float>(input.moveForward) - static_cast<float>(input.moveBackward);
    const float lenSq = inputRight * inputRight + inputForward * inputForward;
    if (lenSq > 0.0f) {
        const float invLen = 1.0f / std::sqrt(lenSq);
        const float normRight = inputRight * invLen;
        const float normForward = inputForward * invLen;
        const float speed = input.sprint ? 7.0f : 4.3f;
        const float sinYaw = std::sin(player.state.yaw);
        const float cosYaw = std::cos(player.state.yaw);
        player.state.velocity.x = (normRight * cosYaw - normForward * sinYaw) * speed;
        player.state.velocity.z = (-normRight * sinYaw - normForward * cosYaw) * speed;
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
