# Work Item 04: Input & Multiplayer Abstraction

## 🎯 Objective & Overview
Design an input abstraction subsystem capable of mapping hardware inputs (Keyboard, Mouse, Gamepads/XInput, Sega Dreamcast Controller) to logical actions ("MoveForward", "Jump", "Interact", "PlaceBlock", "DestroyBlock"). Architect player input channels to seamlessly support drop-in/drop-out splitscreen local multiplayer (up to 4 local players).

---

## 🔗 Dependencies & Context
* **Prerequisites:** `03_platform_windowing_sdl2`
* **Target Subsystems:** `engine/include/voxels/input/`, `engine/src/input/`

---

## 📋 Detailed Task Breakdown
1. **Action & Axis Mapping System (`voxels/input/input_actions.hpp`):**
   * Define logical Action Enums/IDs and Analog Axis Enums (e.g. `LookX`, `LookY`, `MoveX`, `MoveY`).
   * Support deadzone handling, sensitivity scaling, and axis inversion per player channel.

2. **Multi-Player Input Slots (`voxels/input/input_manager.hpp`):**
   * Support up to 4 local player input slots (`PlayerIndex 0..3`).
   * Slot 0 defaults to Mouse + Keyboard or Gamepad 1.
   * Slots 1..3 bind to Gamepads 2..4 upon button press (drop-in) and unbind on disconnect/menu action (drop-out).

3. **Input Device Handlers (`voxels/input/input_device.hpp`):**
   * Mouse/Keyboard handler: Raw relative mouse motion delta, key state polling, mouse wheel delta.
   * Gamepad handler: SDL GameController / XInput polling for analog sticks, triggers, and digital buttons.
   * Dreamcast Controller handler: Dreamcast maple bus button/trigger mapping stubs.

4. **Rebinding & Keymap Persistence:**
   * Support serializing custom action-to-key/button bindings to `GamePreferences`.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_input.cpp`
* **Test Cases:**
  * `Input.ActionBinding`: Bind key `Space` to action `Jump`. Inject key down/up event, verify `isActionActive("Jump")` state transition.
  * `Input.AnalogDeadzone`: Inject raw analog stick value (e.g., `0.1`) under deadzone threshold (`0.15`), verify output axis is `0.0`. Inject `0.8`, verify normalized response.
  * `Input.MultiplayerDropIn`: Inject controller connection event for Slot 1, press Start button, verify Slot 1 transitions to `ACTIVE` state.

---

## 👤 Human-in-the-Loop Actions Required
* **Controller Layout Glyphs / Prompt Images (Human Step):**
  1. Prepare controller button glyph images for UI prompts (Xbox A/B/X/Y, PlayStation Cross/Circle, Keyboard keys).
  2. Place PNG glyph files into `app/assets/ui/glyphs/`:
     * `button_a.png`, `button_b.png`, `button_x.png`, `button_y.png`
     * `key_space.png`, `key_shift.png`, `mouse_left.png`, `mouse_right.png`
  3. (Procedural Fallback: The UI rendering layer will render plain text labels like `[A]`, `[Space]`, `[LMB]` if PNG glyphs are missing).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R InputTest`
3. **Self-Healing:** Ensure event injection mock routines accurately simulate hardware input states without relying on an active physical window focus during automated unit testing.
