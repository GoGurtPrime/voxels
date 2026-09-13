/**
 * @file test_asset_pipeline.cpp
 * @brief Behavioral tests for deterministic VPK assembly and validation.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "voxels/assets/asset_bundler.hpp"

namespace {

std::filesystem::path MakeTestDirectory(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    return path;
}

void WriteText(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << value;
}

void WriteMinimalCatalogue(const std::filesystem::path& root, const std::string& recipes) {
    WriteText(root / "data" / "blocks.json", R"({"blocks":[
        {"id":"air","numeric_id":0,"display_name":"Air","solid":false,"opaque":false,"liquid":false,"replaceable":true,"hardness":0.0,"light_emission":0,"textures":{},"render_type":"cube","model_id":null,"sounds":{},"drops":[]},
        {"id":"wood_log","numeric_id":1,"display_name":"Wood Log","solid":true,"opaque":true,"liquid":false,"hardness":1.0,"light_emission":0,"textures":{},"render_type":"cube","model_id":null,"sounds":{},"drops":[]},
        {"id":"planks","numeric_id":2,"display_name":"Planks","solid":true,"opaque":true,"liquid":false,"hardness":1.0,"light_emission":0,"textures":{},"render_type":"cube","model_id":null,"sounds":{},"drops":[]}
    ]})");
    WriteText(root / "data" / "recipes.json", recipes);
}

} // namespace

TEST_CASE("Vpk.WriteReadRoundTripAndRejectsCorruption", "[assets]") {
    const auto root = MakeTestDirectory("voxels_vpk_roundtrip");
    const auto pack = root / "content.vpk";
    const std::vector<voxels::AssetArchiveEntry> entries = {
        {"textures/a.bin", {std::byte{7}, std::byte{8}}, voxels::AssetType::Texture},
        {"data/b.json", {std::byte{9}}, voxels::AssetType::Json},
    };
    REQUIRE(voxels::VpkArchive::Write(pack, entries));
    const auto archive = voxels::VpkArchive::Open(pack);
    REQUIRE(archive.has_value());
    REQUIRE(archive->ReadEntry("textures/a.bin") == entries[0].data);

    std::fstream tampered(pack, std::ios::in | std::ios::out | std::ios::binary);
    tampered.seekp(64);
    tampered.put('\0');
    tampered.close();
    const auto corrupt = voxels::VpkArchive::Open(pack);
    REQUIRE(corrupt.has_value());
    REQUIRE_FALSE(corrupt->ReadEntry("textures/a.bin").has_value());
}

TEST_CASE("Bundler.OutputIsDeterministicAndHasManifest", "[assets]") {
    const auto root = MakeTestDirectory("voxels_bundle_deterministic");
    WriteMinimalCatalogue(root, R"({"recipes":[{"id":"recipe_planks","icon":"planks","category":"construction","ingredients":[{"item":"wood_log","count":1}],"output_item":"planks","output_count":4}]})");
    const auto outputRoot = MakeTestDirectory("voxels_bundle_deterministic_output");
    const auto first = outputRoot / "first.vpk";
    const auto second = outputRoot / "second.vpk";
    voxels::AssetBundleReport firstReport;
    voxels::AssetBundleReport secondReport;
    std::string error;
    REQUIRE(voxels::AssetBundler::Bundle(root, first, firstReport, error));
    REQUIRE(voxels::AssetBundler::Bundle(root, second, secondReport, error));
    std::ifstream firstInput(first, std::ios::binary);
    std::ifstream secondInput(second, std::ios::binary);
    const std::string firstBytes((std::istreambuf_iterator<char>(firstInput)), {});
    const std::string secondBytes((std::istreambuf_iterator<char>(secondInput)), {});
    REQUIRE(firstBytes == secondBytes);
    const auto archive = voxels::VpkArchive::Open(first);
    REQUIRE(archive->Contains("manifest.json"));
}

TEST_CASE("Bundler.FailsOnMissingBlockTexture", "[assets]") {
    const auto root = MakeTestDirectory("voxels_bundle_missing_texture");
    WriteText(root / "data" / "blocks.json", R"({"blocks":[{"id":"test","numeric_id":0,"textures":{"all":"blocks/missing"},"model_id":null}]})");
    voxels::AssetBundleReport report;
    std::string error;
    REQUIRE_FALSE(voxels::AssetBundler::Bundle(root, root / "out.vpk", report, error));
    REQUIRE(error.find("test") != std::string::npos);
    REQUIRE(error.find("missing") != std::string::npos);
}

TEST_CASE("Bundler rejects recipe content before it enters a pack", "[assets][Crafting]") {
    const auto root = MakeTestDirectory("voxels_bundle_invalid_recipes");
    WriteMinimalCatalogue(root, R"({"recipes":[{"id":"broken_recipe","icon":"planks","category":"construction","ingredients":[{"item":"missing","count":1}],"output_item":"planks","output_count":4}]})");
    voxels::AssetBundleReport report;
    std::string error;
    REQUIRE_FALSE(voxels::AssetBundler::Bundle(root, root / "out.vpk", report, error));
    REQUIRE(error.find("broken_recipe") != std::string::npos);
    REQUIRE(error.find("ingredients.item") != std::string::npos);
}

TEST_CASE("AssetManager.ResolutionOrderAndCache", "[assets]") {
    const auto root = MakeTestDirectory("voxels_asset_resolution");
    const auto loosePath = root / "shaders" / "test.glsl";
    WriteText(loosePath, "loose");
    const std::vector<voxels::AssetArchiveEntry> first = {
        {"shaders/test.glsl", {std::byte{'f'}}, voxels::AssetType::Shader},
    };
    const std::vector<voxels::AssetArchiveEntry> second = {
        {"shaders/test.glsl", {std::byte{'s'}}, voxels::AssetType::Shader},
    };
    REQUIRE(voxels::VpkArchive::Write(root / "first.vpk", first));
    REQUIRE(voxels::VpkArchive::Write(root / "second.vpk", second));
    voxels::AssetManager manager(root);
    REQUIRE(manager.MountArchive(root / "first.vpk"));
    REQUIRE(manager.MountArchive(root / "second.vpk"));
    const auto looseHandle = manager.LoadAsync("shaders/test.glsl", voxels::AssetType::Shader).get();
    REQUIRE(manager.GetData(looseHandle)->at(0) == std::byte{'l'});
    REQUIRE(manager.LoadAsync("shaders/test.glsl", voxels::AssetType::Shader).get() == looseHandle);

    std::filesystem::remove(loosePath);
    voxels::AssetManager packedManager(root);
    REQUIRE(packedManager.MountArchive(root / "first.vpk"));
    REQUIRE(packedManager.MountArchive(root / "second.vpk"));
    const auto packedHandle = packedManager.LoadAsync("shaders/test.glsl", voxels::AssetType::Shader).get();
    REQUIRE(packedManager.GetData(packedHandle)->at(0) == std::byte{'s'});
}
