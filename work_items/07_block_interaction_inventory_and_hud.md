# Work Item 07 — Block Interaction, Inventory & Gameplay HUD

**Phase:** B — Making the World Visible · **Prerequisites:** 05, 06

---

## 1. Problem Statement

`BlockInteraction` exists but is never invoked. There is no visual targeting, no crosshair, no hotbar, no inventory, no mining feedback, and no way for the player to change the world. The core sandbox loop — *look at a block, break it, place it somewhere else* — does not exist.

## 2. Objective

Deliver the complete core gameplay loop with the visual feedback that makes it feel like a game.

## 3. Scope

**In scope:** raycast targeting, break/place rules, mining progress, inventory/hotbar model, item drops, HUD primitives rendered by the engine renderer.

**Out of scope:** ImGui menus (08), crafting, mobs, survival mechanics beyond block interaction.

## 4. Implementation Tasks

1. **Targeting:** DDA voxel raycast from the eye along the view vector, max reach 5.0 blocks, returning `{ hit, blockPos, faceNormal, distance }`. Liquids are not targetable by default (configurable).
2. **Block outline:** render a black wireframe box around the targeted block via `DebugDraw`, with a slight outward bias so it never z-fights.
3. **Breaking:** hold the break action to accumulate progress against the block's `hardness`; on completion set the block to `air`, mark chunks dirty, emit the break sound event, spawn the configured drop into the inventory, and reset progress if the player looks away or releases. Render a **crack overlay** (10-stage progressive texture, procedurally generated) on the targeted face.
4. **Placing:** place the held block on the face adjacent to the target. Reject the placement if it would intersect the player's AABB or any entity. Apply a short cooldown so a held button places at a sane rate. Emit the place sound event.
5. **Inventory model** (`gameplay/inventory.hpp`): 9-slot hotbar + 27-slot main inventory, stack size 64, `AddItem` with stack merging and overflow, `RemoveItem`, `GetSelectedSlot`. Serializable (consumed by work item 10).
6. **Hotbar selection:** number keys `1`–`9` and mouse wheel cycle the selected slot.
7. **HUD (engine-rendered, not ImGui):**
   * Crosshair at screen center (blend-inverted or simple white cross).
   * Hotbar strip at the bottom: 9 slots, selected-slot highlight, item icon per slot, stack count.
   * Held-item view model in the bottom-right corner rendered in a separate depth range so it never clips into terrain.
   * Item icons: rendered from the block's atlas faces into an isometric icon at load time; a real item texture (work item 15) overrides it when present.
8. **Creative/survival toggle** driven by `WorldOptions.sandbox`: creative gives instant break, infinite blocks, and flight; survival respects hardness and inventory counts.
9. **Sound + particle events:** queue break/place/step events for work item 11's audio system; emit a small block-break particle burst (a simple textured quad batch) — behind a `particles` setting.

## 5. Acceptance Criteria

* Looking at a block draws an outline around exactly that block, and the outline tracks the crosshair accurately at range.
* Holding left-click breaks a block with visible cracking progress; the block disappears, the chunk re-meshes instantly, and the item lands in the hotbar.
* Right-click places the held block against the targeted face; you cannot place a block inside yourself.
* The hotbar is visible, selectable with `1`–`9` and the wheel, shows icons and counts, and the held item is visible in the corner.
* Breaking a block at a chunk boundary correctly updates the neighbouring chunk's mesh with no seam artifacts.
* Survival mode respects hardness and depletes stacks; creative does not.

## 6. Automated Tests

`tests/test_block_interaction.cpp`:
* `Raycast.HitsNearestSolidBlockAndReportsCorrectFace` — table-driven across all six faces and diagonal approaches.
* `Raycast.RespectsMaxReachAndMissesWhenNothingInRange`.
* `Raycast.SkipsNonTargetableLiquids`.
* `Break.ProgressAccumulatesWithHardnessAndCompletesAtOne`.
* `Break.LookingAwayResetsProgress`.
* `Break.RemovesBlockAndAddsConfiguredDropToInventory`.
* `Place.RejectsPlacementIntersectingPlayerAabb`.
* `Place.PlacesOnCorrectAdjacentFaceForEachNormal`.
* `Place.ConsumesStackInSurvivalButNotInCreative`.
* `Inventory.StacksMergeUpToLimitAndOverflowToNextSlot`.
* `Inventory.HotbarSelectionWrapsWithWheelAndRespondsToNumberKeys`.
* `Interaction.EditAtChunkBoundaryDirtiesNeighbourChunk`.

## 7. Anti-Shell Checks

- [ ] Break/place are driven by real input in the running game, not only by tests.
- [ ] HUD elements are drawn by the renderer and visible on screen.
- [ ] Sound events are queued through a real event path, even before work item 11 makes them audible.

## 8. Assets & Human Actions

* Crack overlay stages, crosshair, and hotbar frame are procedurally generated and committed; register them in [ASSET_REQUESTS.md](../ASSET_REQUESTS.md) as replaceable:
  * `app/assets/ui/crosshair.png` (16×16 RGBA8)
  * `app/assets/ui/hotbar.png` (182×22 RGBA8) and `hotbar_selection.png` (24×24 RGBA8)
  * `app/assets/textures/misc/crack_0..9.png` (16×16 RGBA8, alpha-only)

## 9. Verification

Build, test, launch, **play the loop**: break at least three different block types, place them back, switch hotbar slots, and report exactly what you observed at each step.
