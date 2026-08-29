/**
 * @file input_actions.hpp
 * @brief Logical actions, axes, and analog processing rules for player input.
 *
 * @details Defines device-independent identifiers and normalization shared by
 *          SDL, XInput, Dreamcast, and deterministic test adapters.
 */

#pragma once

#include <algorithm>

namespace voxels {

enum class InputAction { MoveForward, MoveBackward, MoveLeft, MoveRight, Jump, Sprint, Interact, PlaceBlock, DestroyBlock, Pause };
enum class InputAxis { LookX, LookY, MoveX, MoveY, TriggerLeft, TriggerRight };

struct AxisSettings {
    float deadzone = 0.15f;
    float sensitivity = 1.0f;
    bool inverted = false;
};

[[nodiscard]] inline float ApplyAxisSettings(float value, const AxisSettings& settings) noexcept {
    const float magnitude = std::clamp(value < 0.0f ? -value : value, 0.0f, 1.0f);
    const float deadzone = std::clamp(settings.deadzone, 0.0f, 0.99f);
    if (magnitude <= deadzone) return 0.0f;
    const float normalized = (magnitude - deadzone) / (1.0f - deadzone);
    const float processed = std::clamp(normalized * settings.sensitivity, 0.0f, 1.0f);
    const float signedValue = value < 0.0f ? -processed : processed;
    return settings.inverted ? -signedValue : signedValue;
}

} // namespace voxels