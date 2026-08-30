/**
 * @file asset_bundler.hpp
 * @brief Validates authored content and writes deterministic VPK asset packs.
 *
 * @details Provides the tool-facing content pipeline from ARCHITECTURE.md section 6.6.
 * The editor executable delegates bundle validation and archive creation to this engine module.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "voxels/assets/asset_manager.hpp"

namespace voxels {

struct AssetBundleReport {
    VpkBuildReport archive;
    std::vector<std::string> warnings;
};

class AssetBundler {
public:
    [[nodiscard]] static bool Bundle(const std::filesystem::path& inputDirectory,
                                     const std::filesystem::path& outputPath,
                                     AssetBundleReport& report,
                                     std::string& error);
};

} // namespace voxels
