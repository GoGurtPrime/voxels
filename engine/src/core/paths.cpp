/**
 * @file paths.cpp
 * @brief Implements the engine path helpers used by runtime app configuration and save flow.
 */

#include "voxels/core/paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace voxels {
namespace {

std::filesystem::path SafeEnvPath(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
        std::filesystem::path path(value);
        free(value);
        return path;
    }
    return {};
#else
    if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
        return std::filesystem::path(value);
    }
    return {};
#endif
}

std::filesystem::path HomeDir() {
#if defined(_WIN32)
    if (const auto localAppData = SafeEnvPath("LOCALAPPDATA"); !localAppData.empty()) {
        return localAppData;
    }
    if (const auto userProfile = SafeEnvPath("USERPROFILE"); !userProfile.empty()) {
        return userProfile;
    }
    return std::filesystem::path("C:/");
#else
    if (const auto xdg = SafeEnvPath("XDG_DATA_HOME"); !xdg.empty()) {
        return xdg;
    }
    if (const auto home = SafeEnvPath("HOME"); !home.empty()) {
        return home / ".local" / "share";
    }
    return std::filesystem::current_path();
#endif
}

std::filesystem::path MacUserDataRoot() {
#if defined(__APPLE__)
    if (const auto home = SafeEnvPath("HOME"); !home.empty()) {
        return home / "Library" / "Application Support";
    }
#endif
    return {};
}

} // namespace

std::filesystem::path Paths::ExecutableDir() {
#if defined(_WIN32)
    char modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(modulePath).parent_path();
    }
#endif
    return std::filesystem::canonical(std::filesystem::current_path());
}

std::filesystem::path Paths::UserDataDir() {
    std::filesystem::path root;
#if defined(_WIN32)
    root = HomeDir();
    if (const auto localAppData = SafeEnvPath("LOCALAPPDATA"); !localAppData.empty()) {
        root = localAppData;
    }
    root /= "VoxelsEngine";
#elif defined(__APPLE__)
    root = MacUserDataRoot();
    if (root.empty()) {
        root = HomeDir();
    }
    root /= "VoxelsEngine";
#else
    root = HomeDir() / "VoxelsEngine";
#endif
    std::filesystem::create_directories(root);
    return root;
}

std::filesystem::path Paths::SavesDir() {
    const auto root = UserDataDir() / "saves";
    std::filesystem::create_directories(root);
    return root;
}

std::filesystem::path Paths::LogsDir() {
    const auto root = UserDataDir() / "logs";
    std::filesystem::create_directories(root);
    return root;
}

std::filesystem::path Paths::SettingsFile() {
    return UserDataDir() / "settings.json";
}

std::filesystem::path Paths::AssetsDir() {
    const auto exeDir = ExecutableDir();
    return exeDir / "assets";
}

} // namespace voxels
