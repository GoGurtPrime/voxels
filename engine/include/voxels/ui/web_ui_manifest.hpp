/**
 * @file web_ui_manifest.hpp
 * @brief Validates the hashed local player UI asset manifest.
 *
 * @details Defines the build-to-runtime contract used by the web UI asset target. It is
 * independent of the browser implementation so startup code can reject modified assets before
 * creating a browser, as required by ARCHITECTURE.md section 9.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace voxels {

/// One immutable file emitted by the frontend build.
struct WebUiAsset {
    std::filesystem::path path;
    std::string sha256;
    std::uintmax_t bytes{};
};

/// Parsed and verified web UI build metadata.
struct WebUiManifest {
    std::filesystem::path entryHtml;
    std::string sourceRevision;
    std::string sourceSha256;
    std::vector<WebUiAsset> assets;
};

/// Loads a manifest and verifies every path, byte count, and SHA-256 digest relative to it.
[[nodiscard]] bool LoadAndVerifyWebUiManifest(const std::filesystem::path& manifestPath,
                                              WebUiManifest& manifest,
                                              std::string& error);

} // namespace voxels