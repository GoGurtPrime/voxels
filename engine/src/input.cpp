/**
 * @file input.cpp
 * @brief Implements deterministic local-player input translation.
 *
 * @details Converts platform-neutral device events into logical actions and
 *          normalized axes, including four-player connection lifecycle and
 *          preference-backed bindings.
 */

#include "voxels/input/input_manager.hpp"

#include <algorithm>
#include <sstream>

namespace voxels {

namespace { constexpr int StartButton = 9; }

InputManager::InputManager() {
    for (int player = 1; player < MaxPlayers; ++player) m_players[static_cast<std::size_t>(player)].state = PlayerSlotState::Disconnected;
}

InputManager::PlayerChannel* InputManager::Channel(int player) {
    return player >= 0 && player < MaxPlayers ? &m_players[static_cast<std::size_t>(player)] : nullptr;
}

const InputManager::PlayerChannel* InputManager::Channel(int player) const {
    return player >= 0 && player < MaxPlayers ? &m_players[static_cast<std::size_t>(player)] : nullptr;
}

void InputManager::Update() {
    for (auto& player : m_players) {
        player.mouseX = 0.0f;
        player.mouseY = 0.0f;
        player.mouseWheelY = 0;
    }
}

InputState InputManager::GetInputState() const {
    InputState state;
    const auto* player = Channel(0);
    if (player == nullptr) return state;
    state.moveForward = IsActionActive("MoveForward");
    state.moveBackward = IsActionActive("MoveBackward");
    state.moveLeft = IsActionActive("MoveLeft");
    state.moveRight = IsActionActive("MoveRight");
    state.jump = IsActionActive("Jump");
    state.sprint = IsActionActive("Sprint");
    state.pause = IsActionActive("Pause");
    state.destroyBlock = IsActionActive("DestroyBlock");
    state.placeBlock = IsActionActive("PlaceBlock");
    for (int slot = 0; slot < 9; ++slot) {
        if (IsActionActive("Hotbar" + std::to_string(slot + 1))) {
            state.hotbarSlot = slot;
        }
    }
    state.mouseX = player->mouseX;
    state.mouseY = player->mouseY;
    state.mouseWheelY = player->mouseWheelY;
    return state;
}

void InputManager::BindAction(const std::string& actionName, InputBinding binding) {
    binding.actionName = actionName;
    const auto found = std::find_if(m_bindings.begin(), m_bindings.end(), [&](const auto& item) { return item.actionName == actionName; });
    if (found == m_bindings.end()) m_bindings.push_back(std::move(binding)); else *found = std::move(binding);
}

std::vector<InputBinding> InputManager::GetBindings() const { return m_bindings; }
void InputManager::InjectKeyEvent(int code, bool pressed, int player) {
    if (auto* channel = Channel(player)) {
        channel->keys[code] = pressed;
        if (code >= 'a' && code <= 'z') {
            channel->keys[code - ('a' - 'A')] = pressed;
        } else if (code >= 'A' && code <= 'Z') {
            channel->keys[code + ('a' - 'A')] = pressed;
        }
    }
}
void InputManager::InjectMouseDelta(float x, float y, int player) { if (auto* channel = Channel(player)) { channel->mouseX += x; channel->mouseY += y; } }
void InputManager::InjectMouseButtonEvent(int button, bool pressed, int player) { if (auto* channel = Channel(player)) channel->mouseButtons[button] = pressed; }
void InputManager::InjectMouseWheel(int deltaY, int player) { if (auto* channel = Channel(player)) channel->mouseWheelY += deltaY; }
void InputManager::ClearGameplayInput(int player) {
    if (auto* channel = Channel(player)) {
        channel->keys.clear();
        channel->mouseButtons.clear();
        channel->mouseX = 0.0f;
        channel->mouseY = 0.0f;
        channel->mouseWheelY = 0;
    }
}
void InputManager::InjectAxisEvent(InputAxis axis, float value, int player) { if (auto* channel = Channel(player)) channel->axes[axis] = ApplyAxisSettings(value, channel->axisSettings[axis]); }
void InputManager::InjectControllerConnection(int player, bool connected) { if (auto* channel = Channel(player)) channel->state = connected ? PlayerSlotState::Connected : PlayerSlotState::Disconnected; }

void InputManager::InjectGamepadButton(int player, int code, bool pressed) {
    if (auto* channel = Channel(player)) {
        channel->buttons[code] = pressed;
        if (player > 0 && channel->state == PlayerSlotState::Connected && code == StartButton && pressed) channel->state = PlayerSlotState::Active;
    }
}

bool InputManager::IsActionActive(const std::string& actionName, int player) const {
    const auto* channel = Channel(player);
    if (channel == nullptr) return false;
    const auto binding = std::find_if(m_bindings.begin(), m_bindings.end(), [&](const auto& item) { return item.actionName == actionName; });
    if (binding == m_bindings.end()) return false;
    if (binding->deviceType == InputDeviceType::Gamepad) return channel->buttons.contains(binding->primaryCode) && channel->buttons.at(binding->primaryCode);
    if (binding->deviceType == InputDeviceType::Mouse) return channel->mouseButtons.contains(binding->primaryCode) && channel->mouseButtons.at(binding->primaryCode);
    const bool primaryActive = channel->keys.contains(binding->primaryCode) && channel->keys.at(binding->primaryCode);
    const bool secondaryActive = binding->secondaryCode != 0 && channel->keys.contains(binding->secondaryCode) && channel->keys.at(binding->secondaryCode);
    return primaryActive || secondaryActive;
}

float InputManager::GetAxis(InputAxis axis, int player) const { const auto* channel = Channel(player); return channel != nullptr && channel->axes.contains(axis) ? channel->axes.at(axis) : 0.0f; }
void InputManager::SetAxisSettings(InputAxis axis, AxisSettings settings, int player) { if (auto* channel = Channel(player)) channel->axisSettings[axis] = settings; }
PlayerSlotState InputManager::GetPlayerSlotState(int player) const { const auto* channel = Channel(player); return channel == nullptr ? PlayerSlotState::Disconnected : channel->state; }

void InputManager::LoadBindings(const GamePreferences& preferences) {
    m_bindings.clear();
    for (const auto& [action, value] : preferences.keyBindings) {
        std::istringstream stream(value);
        InputBinding binding;
        binding.actionName = action;
        stream >> binding.primaryCode;
        binding.deviceType = InputDeviceType::Keyboard;
        BindAction(action, binding);
    }
}

void InputManager::SaveBindings(GamePreferences& preferences) const {
    preferences.keyBindings.clear();
    for (const auto& binding : m_bindings) preferences.keyBindings[binding.actionName] = std::to_string(binding.primaryCode);
}

} // namespace voxels
