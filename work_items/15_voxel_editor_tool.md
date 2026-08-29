# Work Item 15: Voxel Editor Tool Sub-Project

## 🎯 Objective & Overview
Develop the standalone `editor` sub-project (`voxels_editor` executable). The editor consumes `voxels_engine` as a library and provides a 3D Dear ImGui editing interface to create, edit, preview, and bake custom non-generated 3D voxel models (animals, doors, items, furniture), test block texture atlas mappings, and pack assets into binary `.vpk` files for the main game App to consume.

> **Priority note:** This item is intentionally scheduled *after* `13_minimum_viable_playable_build`. The editor is a productivity tool for authoring content once the base game is actually playable — it must not be prioritized ahead of making `voxels_app` a real running game.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `02_core_engine_foundation`, `05_graphics_renderer_abstraction`, `06_world_voxel_core`, `08_audio_and_asset_pipeline`, `09_app_lifecycle_ui_and_menus`, `13_minimum_viable_playable_build`
* **Target Subsystems:** `editor/CMakeLists.txt`, `editor/src/`

---

## 📋 Detailed Task Breakdown
1. **Editor Application Scaffold (`editor/src/main.cpp`):**
   * Initializes `voxels_engine`, platform windowing, and Dear ImGui docked workspace layout.

2. **3D Sub-Voxel Model Editor (`editor/src/model_editor.cpp`):**
   * Grid workspace for editing sub-voxel grids (e.g. $16 \times 16 \times 16$ micro-voxels per block model).
   * Editing tools: Add Voxel, Remove Voxel, Paint Voxel, Eyedropper, Box Select, Rotate Model, Pivot Point Adjuster.
   * Model palette: Color picker and block texture selector.

3. **Block Texture Preview & UV Mapper (`editor/src/texture_preview.cpp`):**
   * Live 3D preview of standard $1 \times 1 \times 1$ block faces with custom texture UV offsets applied.

4. **Asset Packer Tool (`editor/src/asset_packer.cpp`):**
   * GUI and CLI utility to aggregate raw loose textures, shaders, audio files, and model `.vmdl` files into compiled `.vpk` asset archives consumed by `app`.

5. **Model File Exporter (`editor/src/model_exporter.cpp`):**
   * Serializes custom voxel models to binary `.vmdl` format (containing sub-voxel coordinate list, color palette, collision box bounds, and pivot offset).

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_editor.cpp`
* **Test Cases:**
  * `ModelExporter.SaveAndLoadModel`: Create a 3D sub-voxel door model programmatically, export to `.vmdl` binary stream, deserialize, verify voxel matrix parity.
  * `AssetPacker.PackageDirectory`: Specify loose test asset folder, pack into `test_assets.vpk`, verify binary table of contents index.

---

## 👤 Human-in-the-Loop Actions Required
* **Sample Editor Project Files (Human Step):**
  1. Create sample sub-voxel model projects in the editor:
     * `editor/samples/wooden_door.vmdl`
     * `editor/samples/wooden_chair.vmdl`
     * `editor/samples/duck_animal.vmdl`
  2. Save sample assets to `editor/samples/`.
  3. (Procedural Fallback: The editor provides built-in procedural sample models generated in code if the `samples/` directory is empty).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build -t voxels_editor`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R EditorTest`
3. **Self-Healing:** Ensure editor tool builds as a separate binary target (`voxels_editor`) without polluting `voxels_app` executable targets or dependencies.
