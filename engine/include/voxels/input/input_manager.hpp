/**
 * @file input_manager.hpp
 * @brief Local-player input channels, bindings, and event injection.
 *
 * @details Owns up to four local channels and translates device events into
 *          logical actions and processed axes. The public injection methods are
 *          suitable for platform adapters and deterministic unit tests.
 */

#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "voxels/core/game_types.hpp"
#include "voxels/input/input_actions.hpp"
#include "voxels/input/input_device.hpp"

namespace voxels {

enum class InputDeviceType { Keyboard, Mouse, Gamepad, Touch, Unknown };
enum class PlayerSlotState { Disconnected, Connected, Active };

struct InputBinding {
    std::string actionName;
    int primaryCode = 0;
    int secondaryCode = 0;
    InputDeviceType deviceType = InputDeviceType::Unknown;
};

struct InputState {
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool jump = false;
    bool sprint = false;
    bool pause = false;
    bool destroyBlock = false;
    bool placeBlock = false;
    int hotbarSlot = -1;
    int mouseWheelY = 0;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
};

class IInputManager {
public:
    virtual ~IInputManager() = default;
    virtual void Update() = 0;
    virtual InputState GetInputState() const = 0;
    virtual void BindAction(const std::string& actionName, InputBinding binding) = 0;
    virtual std::vector<InputBinding> GetBindings() const = 0;
};

class InputManager final : public IInputManager {
public:
    static constexpr int MaxPlayers = 4;

    InputManager();

    void Update() override;
    [[nodiscard]] InputState GetInputState() const override;
    void BindAction(const std::string& actionName, InputBinding binding) override;
    [[nodiscard]] std::vector<InputBinding> GetBindings() const override;

    void InjectKeyEvent(int keyCode, bool pressed, int player = 0);
    void InjectMouseDelta(float x, float y, int player = 0);
    void InjectMouseButtonEvent(int button, bool pressed, int player = 0);
    void InjectMouseWheel(int deltaY, int player = 0);
    void ClearGameplayInput(int player = 0);
    void InjectAxisEvent(InputAxis axis, float value, int player = 0);
    void InjectControllerConnection(int player, bool connected);
    void InjectGamepadButton(int player, int buttonCode, bool pressed);
    [[nodiscard]] bool IsActionActive(const std::string& actionName, int player = 0) const;
    [[nodiscard]] float GetAxis(InputAxis axis, int player = 0) const;
    void SetAxisSettings(InputAxis axis, AxisSettings settings, int player = 0);
    [[nodiscard]] PlayerSlotState GetPlayerSlotState(int player) const;
    void LoadBindings(const GamePreferences& preferences);
    void SaveBindings(GamePreferences& preferences) const;

private:
    struct PlayerChannel {
        PlayerSlotState state = PlayerSlotState::Connected;
        std::map<int, bool> keys;
        std::map<int, bool> buttons;
        std::map<int, bool> mouseButtons;
        std::map<InputAxis, float> axes;
        std::map<InputAxis, AxisSettings> axisSettings;
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        int mouseWheelY = 0;
    };

    [[nodiscard]] PlayerChannel* Channel(int player);
    [[nodiscard]] const PlayerChannel* Channel(int player) const;
    std::array<PlayerChannel, MaxPlayers> m_players{};
    std::vector<InputBinding> m_bindings;
};

} // namespace voxels
