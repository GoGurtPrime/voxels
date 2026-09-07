/**
 * @file paths.hpp
 * @brief Centralized path resolution for portable user-data and runtime asset discovery.
 *
 * @details Encapsulates per-platform user-data roots, save/log locations, and the
 *          executable-relative assets directory so the shipped app resolves content in the
 *          same way regardless of where the binary is launched from.
 */

#pragma once

#include <filesystem>
#include <string>

namespace voxels {

class Paths {
public:
    static std::filesystem::path UserDataDir();
    static std::filesystem::path SavesDir();
    static std::filesystem::path LogsDir();
    static std::filesystem::path SettingsFile();
    static std::filesystem::path AssetsDir();
    static std::filesystem::path ExecutableDir();
};

} // namespace voxels
