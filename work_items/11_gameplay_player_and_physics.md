# Work Item 11: Player Entity, Physics & Block Interaction

## 🎯 Objective & Overview
Implement the missing gameplay layer that turns the existing world/render/input subsystems into something a human can actually control: a `Player` entity with a first-person camera, gravity, ground collision against the voxel world, jumping, falling, walking/running, and basic block break/place interaction. Work items `01`-`10` produced correctly-tested *isolated* subsystems (platform, input, renderer, world, worldgen, audio, app menus, networking) but nothing currently reads live input and moves an entity through the generated world — this item closes that gap.

> **Why this item exists:** Prior work items were scoped and verified purely by unit tests on isolated classes. None of them required an entity that consumes `IInputManager` + `World` + `IRenderer` together every frame. As a result the compiled app has all the pieces but nothing moves. This item — and `12`/`13` after it — are explicitly integration-first.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `04_input_and_multiplayer_abstraction`, `05_graphics_renderer_abstraction`, `06_world_voxel_core`, `07_world_generation_pipeline`
* **Target Subsystems:** `engine/include/voxels/gameplay/`, `engine/src/gameplay/`

---

## 📋 Detailed Task Breakdown
1. **Player Entity & Save-Relevant State (`voxels/gameplay/player.hpp`):**
   * `PlayerState` struct: position (`Vec3`), velocity (`Vec3`), yaw/pitch, `onGround` flag, selected hotbar slot, health (stub), inventory stub (array of `BlockId` counts is sufficient for now).
   * `Player` class wraps `PlayerState` plus per-frame update logic; must be trivially serializable (plain data) so `12_persistence_and_game_loop_integration` can (de)serialize it without gameplay-layer coupling.
2. **AABB Voxel Collision & Physics (`voxels/gameplay/physics.hpp` / `.cpp`):**
   * Simple axis-aligned bounding box vs. voxel-grid sweep (resolve X, then Y, then Z independently to avoid tunneling).
   * Constant gravity acceleration, terminal velocity clamp, ground-check raycast/AABB test for `onGround`, jump impulse.
   * Must operate purely against `voxels::World::GetBlock` (no rendering/platform dependency) so it is unit-testable headlessly.
3. **First-Person Camera Controller (`voxels/gameplay/camera_controller.hpp`):**
   * Maps `IInputManager` action state (move axes, look delta, jump action) → player velocity/yaw/pitch each tick.
   * Mouse-look sensitivity and invert-Y are read from `GamePreferences` (already defined in `01`/core).
4. **Block Interaction (`voxels/gameplay/block_interaction.hpp` / `.cpp`):**
   * Raycast from camera along view direction (reuse `World::Raycast` from `06_world_voxel_core`) up to a configurable reach distance (default 6 blocks).
   * "Break" action sets the targeted block to `Air`; "Place" action sets the adjacent face-neighbor block to the currently selected hotbar block, refusing placement if it would intersect the player's own AABB.
5. **Multiplayer-Ready Shape:** keep `Player`/physics free of any singleton/global state — must be constructible per connected peer so `10_networking_and_server_client`'s multi-peer model and future splitscreen (`04`) can each own an independent instance.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_gameplay.cpp`
* **Test Cases:**
  * `Physics.GravityAndGroundedStop`: Drop a player AABB above a flat stone platform, step simulation, verify it comes to rest exactly on the surface with `onGround == true` and zero vertical velocity.
  * `Physics.JumpArc`: Apply jump impulse on grounded player, verify velocity becomes negative (falling) again after gravity integrates for N ticks and player lands at the same Y it started from (no energy gain/loss).
  * `Physics.HorizontalCollisionStopsAtWall`: Walk a player horizontally into a solid wall column, verify position clamps at the wall boundary instead of penetrating.
  * `BlockInteraction.BreakAndPlace`: Raycast at a known block, break it (verify becomes `Air`), place a new block on the adjacent face (verify correct neighbor coordinate and block id), verify placement is rejected when it would overlap the player AABB.

---

## 👤 Human-in-the-Loop Actions Required
* None required — this item is pure gameplay logic and reuses existing procedural fallbacks (no new art/audio assets needed).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R GameplayTest`
3. **Self-Healing:** If collision resolution allows tunneling at high velocity/low tick rate in tests, reduce the physics step size (fixed-timestep sub-stepping) rather than special-casing specific test coordinates.
