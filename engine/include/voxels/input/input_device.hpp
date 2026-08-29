/**
 * @file input_device.hpp
 * @brief Platform-neutral keyboard, mouse, gamepad, and Dreamcast device state.
 *
 * @details Platform backends feed these handlers, while tests can inject state
 *          without creating a focused window or requiring physical hardware.
 */

#pragma once

#include <array>
#include <cstddef>

namespace voxels {

class KeyboardMouseDevice final {
public:
    void SetKey(int code, bool pressed) noexcept { m_keys[ToIndex(code)] = pressed; }
    [[nodiscard]] bool IsKeyPressed(int code) const noexcept { return m_keys[ToIndex(code)]; }
    void SetMouseDelta(float x, float y) noexcept { m_mouseX = x; m_mouseY = y; }
    void SetWheelDelta(float delta) noexcept { m_wheel = delta; }
    [[nodiscard]] float MouseX() const noexcept { return m_mouseX; }
    [[nodiscard]] float MouseY() const noexcept { return m_mouseY; }
    [[nodiscard]] float Wheel() const noexcept { return m_wheel; }
    void ClearFrameDeltas() noexcept { m_mouseX = 0.0f; m_mouseY = 0.0f; m_wheel = 0.0f; }

private:
    [[nodiscard]] static std::size_t ToIndex(int code) noexcept { return static_cast<std::size_t>(static_cast<unsigned int>(code) % 512U); }
    std::array<bool, 512> m_keys{};
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_wheel = 0.0f;
};

class GamepadDevice final {
public:
    void SetConnected(bool connected) noexcept { m_connected = connected; }
    [[nodiscard]] bool IsConnected() const noexcept { return m_connected; }
    void SetButton(int code, bool pressed) noexcept { m_buttons[ToIndex(code)] = pressed; }
    [[nodiscard]] bool IsButtonPressed(int code) const noexcept { return m_buttons[ToIndex(code)]; }
    void SetAxis(int code, float value) noexcept { m_axes[ToIndex(code)] = value; }
    [[nodiscard]] float Axis(int code) const noexcept { return m_axes[ToIndex(code)]; }

private:
    [[nodiscard]] static std::size_t ToIndex(int code) noexcept { return static_cast<std::size_t>(static_cast<unsigned int>(code) % 32U); }
    bool m_connected = false;
    std::array<bool, 32> m_buttons{};
    std::array<float, 32> m_axes{};
};

class DreamcastControllerDevice final {
public:
    void SetButton(int mapleCode, bool pressed) noexcept { m_buttons[static_cast<std::size_t>(mapleCode) % m_buttons.size()] = pressed; }
    [[nodiscard]] bool IsButtonPressed(int mapleCode) const noexcept { return m_buttons[static_cast<std::size_t>(mapleCode) % m_buttons.size()]; }

private:
    std::array<bool, 16> m_buttons{};
};

} // namespace voxels