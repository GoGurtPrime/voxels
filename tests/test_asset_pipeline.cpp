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
    WriteText(root / "data" / "example.json", "{\"value\":1}");
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
