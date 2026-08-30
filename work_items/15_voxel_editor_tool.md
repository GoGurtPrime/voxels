# Work Item 15 — Voxel Editor Tool

**Phase:** E — Content Tooling · **Prerequisites:** 08, 13, 14

---

## 1. Problem Statement

`editor/src/main.cpp` prints one line and exits. No meaningful work has been done on the internal content tool at all. Without it, every model, texture assignment, block definition, and asset pack must be hand-authored in a text editor or generated in code — which does not scale and is not how the project is meant to produce content.

## 2. Objective

A genuinely usable internal authoring tool: model sub-voxel geometry (blocks, items, tools, creatures), paint and assign textures, define block types, preview everything exactly as the game renders it, and bundle the whole content set into a `.vpk` the game consumes.

## 3. Scope

**In scope:** the `voxels_editor` application — docked ImGui workspace, 3D model editing viewport, texture/atlas tools, block definition editor, in-game-accurate preview, project management, and the bundling front end for work item 14.

**Out of scope:** animation timelines, world editing, terrain painting (possible follow-ups).

## 4. Implementation Tasks

1. **Editor application shell:** reuse `voxels_engine` — same `SDL2Platform`, `GLRenderer`, `ImGuiUIManager`, `AssetManager`. ImGui docking workspace with a persisted layout (`<userdata>/editor_layout.ini`), menu bar (File / Edit / View / Content / Help), and a status bar.
2. **Project model:** an editor project is a content directory (the same layout work item 14 bundles). Open/create a project, browse its assets in a **Content Browser** panel with thumbnails for textures and models, and see validation problems inline.
3. **Sub-voxel model editor (the core feature):**
   * 3D viewport with orbit/pan/zoom, grid, axis gizmo, and the authored bounding box visualized.
   * Editable `16×16×16` micro-voxel grid (resolution configurable per model, ADR-010).
   * Tools: **Add**, **Remove**, **Paint**, **Eyedropper**, **Fill/Bucket (contiguous)**, **Box Select**, **Move Selection**, **Mirror X/Y/Z**, **Rotate 90° about any axis**.
   * Slice/layer mode: lock to a plane and step through it, so interiors are editable — essential and easy to omit.
   * Palette editor: add/remove/reorder colors, assign a texture layer and an emissive flag per palette entry.
   * **Undo/redo stack** with a sane depth. Non-negotiable for a usable tool.
   * Pivot editor, collision-bounds editor (including multi-box for stairs), named **elements** (door leaf, chest lid, limbs), and named **attachment points**.
4. **Texture tools:**
   * Import PNGs; validate dimensions; view the assembled atlas with layer indices.
   * Per-face texture assignment for cube blocks with a live 3D preview of the resulting block.
   * A small built-in pixel painter for quick 16×16 edits and placeholder authoring, saving back to PNG.
5. **Block definition editor:** a form over `blocks.json` — id, display name, flags, hardness, light emission, per-face textures, `render_type`, `model_id` (picked from project models), sounds (audition button), and drops. **This is the association step the project needs**: selecting a model for a block here is what makes an authored `.vmdl` appear in the game.
6. **In-game-accurate preview:**
   * A **Block Preview** panel rendering the block with the game's chunk shader, lighting, and AO — what you see is what ships.
   * A **World Preview** panel placing the selected block/model into a small sandbox chunk so the model can be judged in context, with a first-person camera toggle.
   * A **Item Icon** preview showing the hotbar icon that will be generated.
7. **Bundling front end:** a **Content → Build Pack** action invoking the work item 14 bundler, streaming the validation report into a docked output panel with clickable errors that focus the offending asset. Also expose the pure CLI mode (`--bundle`) for build automation.
8. **Import/export:** load and save `.vmdl`; import from a simple `.vox`-style or PNG-layer-stack format if cheap; export the model as OBJ for external inspection (one-way, diagnostics only).
9. **Sample project:** ship `editor/samples/` containing the work item 13 models as editable projects, so the tool opens with real content on first launch.
10. **Safety:** autosave the working model to a recovery file every 60 s; prompt on unsaved changes; never silently overwrite a project file.

## 5. Acceptance Criteria

* Launching `voxels_editor` opens a docked workspace with a content browser and a 3D viewport containing an editable model.
* A human can, in one session with no code changes: create a new model, sculpt a recognizable shape, paint it, set its pivot and collision bounds, save it as `.vmdl`, define a new block that references it, build the pack, launch `voxels_app`, and **place that block in the world**.
* Undo/redo works reliably across every editing tool.
* The block preview matches in-game appearance.
* The build-pack output panel reports validation errors precisely and refuses to produce a broken pack.

## 6. Automated Tests

`tests/test_editor.cpp` (logic is in testable, headless-capable classes; GUI shell is thin):
* `ModelEdit.AddRemovePaintMutateGridAsExpected`.
* `ModelEdit.UndoRedoRestoresExactGridStateAcrossMixedOperations`.
* `ModelEdit.FillIsContiguousAndRespectsBoundaries`.
* `ModelEdit.RotateAndMirrorPreserveVoxelCountAndAreReversible`.
* `ModelEdit.SelectionMoveClampsToGridAndDoesNotDuplicateVoxels`.
* `Palette.RemovingAnEntryRemapsReferencingVoxelsSafely`.
* `Project.SaveLoadRoundTripsModelPivotBoundsElementsAndAttachments`.
* `BlockDefEditor.WritesValidJsonThatTheRuntimeRegistryLoads` — round-trip through the real `BlockRegistry`.
* `BlockDefEditor.RejectsDuplicateIdsAndDanglingModelReferences`.
* `Bundle.EditorBuildProducesSamePackAsCliBundler` — byte-identical.
* `Recovery.AutosaveFileIsWrittenAndRestorable`.
* `IconBake.GeneratesDeterministicItemIconForBlockAndModel`.

## 7. Anti-Shell Checks

- [ ] The editor is a working tool, not a scaffold that prints a message.
- [ ] Every declared tool button performs its edit; no disabled placeholders shipped.
- [ ] The editor's output actually reaches the game through the pack pipeline — verified end to end.

## 8. Assets & Human Actions

* Ask the operator to author or supply reference art for the sample models, and to try the create-model → define-block → build-pack → play loop and report friction. Their feedback drives the tool's next pass.
* Register any editor UI art in [ASSET_REQUESTS.md](../ASSET_REQUESTS.md).

## 9. Verification

Build, test, launch the editor, **author a new model end to end**, bundle it, launch the game, place the block, and report every step you performed and observed.
