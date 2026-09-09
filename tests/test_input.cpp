/**
 * @file test_input.cpp
 * @brief Unit tests for Work Item 03 input actions, devices, and local slots.
 */

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "voxels/input/input_manager.hpp"

TEST_CASE("Input.ActionBinding", "[input]") {
    voxels::InputManager manager;
    manager.BindAction("Jump", voxels::InputBinding{"", 32, 0, voxels::InputDeviceType::Keyboard});
    manager.InjectKeyEvent(32, true);
    REQUIRE(manager.IsActionActive("Jump"));
    manager.InjectKeyEvent(32, false);
    REQUIRE_FALSE(manager.IsActionActive("Jump"));
}

TEST_CASE("Input.AnalogDeadzone", "[input]") {
    voxels::InputManager manager;
    manager.SetAxisSettings(voxels::InputAxis::MoveX, voxels::AxisSettings{0.15f, 1.0f, false});
    manager.InjectAxisEvent(voxels::InputAxis::MoveX, 0.1f);
    REQUIRE(manager.GetAxis(voxels::InputAxis::MoveX) == 0.0f);
    manager.InjectAxisEvent(voxels::InputAxis::MoveX, 0.8f);
    REQUIRE(std::abs(manager.GetAxis(voxels::InputAxis::MoveX) - ((0.8f - 0.15f) / 0.85f)) < 0.0001f);
}

TEST_CASE("Input.MultiplayerDropIn", "[input]") {
    voxels::InputManager manager;
    REQUIRE(manager.GetPlayerSlotState(1) == voxels::PlayerSlotState::Disconnected);
    manager.InjectControllerConnection(1, true);
    manager.InjectGamepadButton(1, 9, true);
    REQUIRE(manager.GetPlayerSlotState(1) == voxels::PlayerSlotState::Active);
    manager.InjectControllerConnection(1, false);
    REQUIRE(manager.GetPlayerSlotState(1) == voxels::PlayerSlotState::Disconnected);
}

TEST_CASE("Input.PreferenceBindingRoundTrip", "[input]") {
    voxels::InputManager manager;
    manager.BindAction("Jump", voxels::InputBinding{"", 32, 0, voxels::InputDeviceType::Keyboard});
    voxels::GamePreferences preferences;
    manager.SaveBindings(preferences);
    voxels::InputManager restored;
    restored.LoadBindings(preferences);
    restored.InjectKeyEvent(32, true);
    REQUIRE(restored.IsActionActive("Jump"));
}

TEST_CASE("Input.DeviceHandlers", "[input]") {
    voxels::KeyboardMouseDevice keyboard;
    keyboard.SetKey(32, true);
    keyboard.SetMouseDelta(2.0f, -1.0f);
    REQUIRE(keyboard.IsKeyPressed(32));
    REQUIRE(keyboard.MouseX() == 2.0f);
    voxels::GamepadDevice gamepad;
    gamepad.SetConnected(true);
    gamepad.SetButton(1, true);
    REQUIRE(gamepad.IsConnected());
    REQUIRE(gamepad.IsButtonPressed(1));
}

TEST_CASE("Input.TracksLastActiveDeviceForHudControlHints", "[input]") {
    voxels::InputManager input;
    REQUIRE(input.GetLastActiveDeviceType() == voxels::InputDeviceType::Unknown);

    input.InjectKeyEvent(static_cast<int>('w'), true);
    REQUIRE(input.GetLastActiveDeviceType() == voxels::InputDeviceType::Keyboard);

    input.InjectGamepadButton(0, 1, true);
    REQUIRE(input.GetLastActiveDeviceType() == voxels::InputDeviceType::Gamepad);

    input.InjectMouseButtonEvent(1, true);
    REQUIRE(input.GetLastActiveDeviceType() == voxels::InputDeviceType::Mouse);

    // Releasing a button is not itself "activity"; the device stays whatever last pressed.
    input.InjectKeyEvent(static_cast<int>('w'), false);
    REQUIRE(input.GetLastActiveDeviceType() == voxels::InputDeviceType::Mouse);

    input.InjectAxisEvent(voxels::InputAxis::MoveX, 0.9f);
    REQUIRE(input.GetLastActiveDeviceType() == voxels::InputDeviceType::Gamepad);
}