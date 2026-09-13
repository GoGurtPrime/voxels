/**
 * @file test_editor.cpp
 * @brief Behavioral coverage for the headless-capable voxel editor document core.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "editor_document.hpp"
#include "voxels/world/block.hpp"

namespace {

std::filesystem::path TestDirectory(const char* name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "voxels_editor_tests" / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

voxels::editor::EditorDocument MakeDocument() {
    voxels::VoxelModel model;
    model.gridSize = {4, 4, 4};
    model.voxels.assign(model.GridVoxelCount(), 0);
    model.palette = {{255, 255, 255, 255, 0, 0, 0}, {200, 80, 30, 255, 1, 0, 0}};
    return voxels::editor::EditorDocument(std::move(model));
}

} // namespace

TEST_CASE("ModelEdit mutates grid and undo restores exact prior state", "[editor]") {
    auto document = MakeDocument();
    REQUIRE(document.SetVoxel({1, 1, 1}, 1));
    REQUIRE(document.SetVoxel({2, 1, 1}, 2));
    REQUIRE(document.Model().SolidVoxelCount() == 2);
    REQUIRE(document.Model().IsSolidAt(1, 1, 1));
    REQUIRE(document.Undo());
    REQUIRE(document.Model().SolidVoxelCount() == 1);
    REQUIRE(document.Redo());
    REQUIRE(document.Model().voxels[document.Model().gridSize[0] * (1 + document.Model().gridSize[1]) + 2] == 2);
}

TEST_CASE("ModelEdit fill remains contiguous and palette removal remaps safely", "[editor]") {
    auto document = MakeDocument();
    REQUIRE(document.SetVoxel({0, 0, 0}, 1));
    REQUIRE(document.SetVoxel({1, 0, 0}, 1));
    REQUIRE(document.SetVoxel({3, 3, 3}, 1));
    REQUIRE(document.Fill({0, 0, 0}, 2));
    REQUIRE(document.Model().voxels[0] == 2);
    REQUIRE(document.Model().voxels[1] == 2);
    REQUIRE(document.Model().voxels[63] == 1);
    REQUIRE(document.RemovePaletteEntry(0));
    REQUIRE(document.Model().palette.size() == 1);
    REQUIRE(document.Model().voxels[0] == 1);
    REQUIRE(document.Model().voxels[63] == 0);
}

TEST_CASE("ModelEdit transforms and selection movement preserve authored geometry", "[editor]") {
    auto document = MakeDocument();
    REQUIRE(document.SetVoxel({0, 0, 0}, 1));
    REQUIRE(document.SetVoxel({1, 2, 3}, 2));
    const auto original = document.Model().voxels;
    REQUIRE(document.Mirror(voxels::editor::Axis::X));
    REQUIRE(document.Mirror(voxels::editor::Axis::X));
    REQUIRE(document.Model().voxels == original);
    REQUIRE(document.Rotate90(voxels::editor::Axis::Y));
    REQUIRE(document.Rotate90(voxels::editor::Axis::Y));
    REQUIRE(document.Rotate90(voxels::editor::Axis::Y));
    REQUIRE(document.Rotate90(voxels::editor::Axis::Y));
    REQUIRE(document.Model().voxels == original);
    REQUIRE(document.MoveSelection({{0, 0, 0}}, 1, 0, 0));
    REQUIRE_FALSE(document.Model().IsSolidAt(0, 0, 0));
    REQUIRE(document.Model().IsSolidAt(1, 0, 0));
}

TEST_CASE("ModelEdit box selection and selected movement mutate only selected voxels", "[editor]") {
    auto document = MakeDocument();
    REQUIRE(document.SetVoxel({0, 0, 0}, 1));
    REQUIRE(document.SetVoxel({1, 0, 0}, 2));
    REQUIRE(document.SetVoxel({3, 3, 3}, 1));
    REQUIRE(document.SelectBox({0, 0, 0}, {1, 0, 0}, false));
    REQUIRE(document.Selection().size() == 2);
    REQUIRE(document.MoveSelected(0, 1, 0));
    REQUIRE_FALSE(document.Model().IsSolidAt(0, 0, 0));
    REQUIRE(document.Model().IsSolidAt(0, 1, 0));
    REQUIRE(document.Model().IsSolidAt(3, 3, 3));
}

TEST_CASE("ModelEdit properties are dirty and undoable", "[editor]") {
    auto document = MakeDocument();
    const auto originalPalette = document.Model().palette[0];
    auto updatedPalette = originalPalette;
    updatedPalette.red = 32;
    REQUIRE(document.UpdatePaletteEntry(0, updatedPalette));
    REQUIRE(document.SetPivot({0.5F, 0.25F, 0.5F}));
    REQUIRE(document.SetBounds({0.1F, 0.2F, 0.3F}, {0.8F, 0.9F, 1.0F}));
    REQUIRE(document.IsDirty());
    REQUIRE(document.Model().boundsMin == std::array<float, 3>{0.1F, 0.2F, 0.3F});
    REQUIRE(document.Undo());
    REQUIRE(document.Model().boundsMin != std::array<float, 3>{0.1F, 0.2F, 0.3F});
    REQUIRE(document.Undo());
    REQUIRE(document.Undo());
    REQUIRE(document.Model().palette[0].red == originalPalette.red);
}

TEST_CASE("Editor project persists VMDL recovery and produces a pack", "[editor][filesystem]") {
    const std::filesystem::path root = TestDirectory("project");
    voxels::editor::EditorProject project(root);
    std::string error;
    REQUIRE(project.CreateLayout(error));
    auto document = MakeDocument();
    REQUIRE(document.SetVoxel({1, 1, 1}, 1));
    document.Model().pivot = {0.5F, 0.25F, 0.5F};
    document.Model().attachments.push_back({"top", {0.5F, 1.0F, 0.5F}, {0.0F, 0.0F, 0.0F}});
    const std::filesystem::path model = root / "models" / "editor_test.vmdl";
    const std::filesystem::path recovery = root / "recovery" / "editor_test.vmdl";
    REQUIRE(document.Save(model, error));
    REQUIRE(document.WriteRecovery(recovery, error));
    REQUIRE(std::filesystem::exists(model));
    REQUIRE(std::filesystem::exists(recovery));
    voxels::editor::EditorDocument loaded;
    REQUIRE(loaded.Load(recovery, error));
    REQUIRE(loaded.Model().pivot == document.Model().pivot);
    REQUIRE(loaded.Model().attachments.size() == 1);
    std::ofstream blocks(root / "data" / "blocks.json");
    blocks << R"({"blocks":[{"id":"air","numeric_id":0,"solid":false,"opaque":false,"model_id":null},{"id":"editor_test","numeric_id":1,"solid":true,"opaque":true,"model_id":"models/editor_test.vmdl"}]})";
    blocks.close();
    std::ofstream recipes(root / "data" / "recipes.json");
    recipes << R"({"recipes":[{"id":"recipe_editor_test","icon":"editor_test","category":"furniture","ingredients":[{"item":"editor_test","count":1}],"output_item":"editor_test","output_count":1}]})";
    recipes.close();
    voxels::AssetBundleReport report;
    REQUIRE(project.BuildPack(root / "editor_test.vpk", report, error));
    REQUIRE(report.archive.entryCount >= 3);
    REQUIRE(std::filesystem::file_size(root / "editor_test.vpk") > 0);
}

TEST_CASE("Editor project writes textures and preserves existing block definitions", "[editor][filesystem]") {
    const std::filesystem::path root = TestDirectory("block_upsert");
    voxels::editor::EditorProject project(root);
    std::string error;
    REQUIRE(project.CreateLayout(error));
    auto document = MakeDocument();
    REQUIRE(document.SetVoxel({1, 1, 1}, 1));
    REQUIRE(document.Save(root / "models" / "lantern.vmdl", error));
    voxels::editor::TextureDocument texture;
    REQUIRE(texture.SetPixel(0, 0, {255, 160, 32, 255}));
    REQUIRE(project.CreateBlockTexture("blocks/lantern", texture.Image(), error));
    REQUIRE(std::filesystem::exists(root / "textures" / "blocks" / "lantern.png"));
    REQUIRE(project.UpsertModelBlock("lantern", "Lantern", "models/lantern.vmdl", "blocks/lantern", 0.4F, error));
    REQUIRE(project.UpsertModelBlock("lantern", "Polished Lantern", "models/lantern.vmdl", "blocks/lantern", 0.8F, error));
    voxels::BlockRegistry registry;
    registry.LoadFromFile(root / "data" / "blocks.json");
    REQUIRE(registry.Count() == 1);
    const auto* lantern = registry.GetDefinition("lantern");
    REQUIRE(lantern != nullptr);
    REQUIRE(lantern->displayName == "Polished Lantern");
    REQUIRE(lantern->modelId == "models/lantern.vmdl");
}