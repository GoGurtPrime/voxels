/**
 * @file test_build_environment.cpp
 * @brief Build-environment validation for the shipped executable path and user-data layout.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "voxels/core/paths.hpp"

TEST_CASE("Paths.UserDataDirIsCreatedAndWritable", "[build][paths]") {
    const std::filesystem::path userData = voxels::Paths::UserDataDir();

    REQUIRE(std::filesystem::exists(userData));
    REQUIRE(std::filesystem::is_directory(userData));

    const std::filesystem::path probePath = userData / "voxels_probe.txt";
    {
        std::ofstream probe(probePath, std::ios::binary | std::ios::trunc);
        REQUIRE(probe.is_open());
        probe << "probe";
    }

    REQUIRE(std::filesystem::exists(probePath));
    std::ifstream input(probePath, std::ios::binary);
    REQUIRE(input.is_open());
    std::string content;
    std::getline(input, content);
    REQUIRE(content == "probe");
    input.close();

    std::filesystem::remove(probePath);
}

TEST_CASE("Paths.SavesAndLogsAreUnderUserDataRoot", "[build][paths]") {
    const std::filesystem::path userData = voxels::Paths::UserDataDir();
    const std::filesystem::path saves = voxels::Paths::SavesDir();
    const std::filesystem::path logs = voxels::Paths::LogsDir();

    REQUIRE_FALSE(saves.is_relative());
    REQUIRE_FALSE(logs.is_relative());
    REQUIRE(saves.string().find(userData.string()) == 0);
    REQUIRE(logs.string().find(userData.string()) == 0);
}

TEST_CASE("Paths.AssetsDirExistsNextToExecutable", "[build][paths]") {
    const std::filesystem::path assetsDir = voxels::Paths::AssetsDir();
    REQUIRE(std::filesystem::exists(assetsDir));
    REQUIRE(std::filesystem::is_directory(assetsDir));
}

TEST_CASE("BuildConfig.Sdl2IsCompiledIn", "[build][config]") {
#if defined(VOXELS_HAS_SDL2)
    REQUIRE(VOXELS_HAS_SDL2 == 1);
#else
    FAIL("VOXELS_HAS_SDL2 must be defined for the desktop build");
#endif
}
