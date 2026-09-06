/**
 * @file test_web_ui_manifest.cpp
 * @brief Tests the hash-verified web UI asset contract.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "voxels/ui/web_ui_manifest.hpp"

namespace {
void WriteText(const std::filesystem::path& path, std::string_view text) { std::ofstream(path, std::ios::binary | std::ios::trunc) << text; }
}

TEST_CASE("WebUiManifest.VerifiesFrontendOutputAndRejectsTampering", "[web-ui][WebUiManifest]") {
    const auto root = std::filesystem::temp_directory_path() / "voxels_web_ui_manifest";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "browser");
    WriteText(root / "browser" / "index.html", "transparent UI");
    WriteText(root / "ui-manifest.json", R"({"schema_version":1,"entry_html":"browser/index.html","source_revision":"test","source_sha256":"0000000000000000000000000000000000000000000000000000000000000000","assets":{"browser/index.html":{"sha256":"9b30c168c5e27b5d2b219026d6690a01d8abd4ad2d901c06d0f28c267fe70774","bytes":14}}})");
    voxels::WebUiManifest manifest;
    std::string error;
    REQUIRE(voxels::LoadAndVerifyWebUiManifest(root / "ui-manifest.json", manifest, error));
    REQUIRE(manifest.assets.size() == 1U);
    WriteText(root / "browser" / "index.html", "modified UI");
    REQUIRE_FALSE(voxels::LoadAndVerifyWebUiManifest(root / "ui-manifest.json", manifest, error));
    std::filesystem::remove_all(root);
}