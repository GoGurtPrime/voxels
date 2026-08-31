/**
 * @file test_packaging.cpp
 * @brief Regression tests for work_items/18_packaging_distribution_and_platform_services.md.
 *
 * @details Exercises the pieces of packaging/versioning/first-run/platform-services that are
 *          reachable without a live window: the generated version header's propagation into
 *          save metadata and the VPK manifest, the first-run settings/controls-card persistence
 *          primitives (identical calls to what app/src/main.cpp performs), and platform-services
 *          achievement unlocking driven through real `GameSession` gameplay, not direct calls.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>

#include <nlohmann/json.hpp>

#include "voxels/app/game_session.hpp"
#include "voxels/assets/asset_bundler.hpp"
#include "voxels/core/game_types.hpp"
#include "voxels/core/preferences.hpp"
#include "voxels/core/version.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/platform/platform_services.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/world.hpp"

namespace {
std::filesystem::path MakeTestDirectory(const char* name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
} // namespace

TEST_CASE("Version.IsConsistentAcrossExecutableLogAndSaveMetadata", "[packaging][version]") {
    // Real semantic-version shape (X.Y.Z), not an empty/placeholder string.
    REQUIRE(std::regex_match(voxels::kEngineVersion, std::regex(R"(\d+\.\d+\.\d+)")));
    REQUIRE(std::to_string(voxels::kEngineVersionMajor) + "." + std::to_string(voxels::kEngineVersionMinor) + "." +
                std::to_string(voxels::kEngineVersionPatch) ==
            std::string(voxels::kEngineVersion));
    REQUIRE_FALSE(std::string(voxels::kEngineGitCommit).empty());
    REQUIRE_FALSE(std::string(voxels::kEngineBuildTimestamp).empty());

    // A freshly constructed save carries the same version string a window title / log banner
    // would report; this is the single source of truth the save-format contract relies on.
    const voxels::GameSave save{};
    REQUIRE(save.engineVersion == voxels::kEngineVersion);
}

TEST_CASE("Version.VpkManifestRecordsTheSameEngineVersion", "[packaging][version]") {
    const auto root = MakeTestDirectory("voxels_packaging_manifest_version");
    WriteText(root / "data" / "example.json", "{\"value\":1}");
    voxels::AssetBundleReport report;
    std::string error;
    REQUIRE(voxels::AssetBundler::Bundle(root, root / "core.vpk", report, error));

    const auto archive = voxels::VpkArchive::Open(root / "core.vpk");
    REQUIRE(archive.has_value());
    const auto manifestBytes = archive->ReadEntry("manifest.json");
    REQUIRE(manifestBytes.has_value());
    std::string manifestText(manifestBytes->size(), '\0');
    std::memcpy(manifestText.data(), manifestBytes->data(), manifestBytes->size());
    const nlohmann::json manifest = nlohmann::json::parse(manifestText);
    REQUIRE(manifest.at("engine_compatibility").get<std::string>() == voxels::kEngineVersion);
    std::filesystem::remove_all(root);
}

TEST_CASE("FirstRun.CreatesDefaultSettingsFileOnFirstLaunch", "[packaging][first_run]") {
    const auto settingsPath = std::filesystem::temp_directory_path() / "voxels_first_run_settings.json";
    std::filesystem::remove(settingsPath);

    // Mirrors app/src/main.cpp exactly: absence of the file is the first-run signal, and the
    // loaded (default) preferences are written back immediately.
    REQUIRE_FALSE(std::filesystem::exists(settingsPath));
    const bool firstRun = !std::filesystem::exists(settingsPath);
    REQUIRE(firstRun);
    voxels::PreferencesManager manager(settingsPath);
    voxels::GamePreferences preferences = manager.Load();
    REQUIRE_FALSE(preferences.controlsCardSeen);
    manager.Save(preferences);

    REQUIRE(std::filesystem::exists(settingsPath));
    const voxels::GamePreferences reloaded = manager.Load();
    REQUIRE(reloaded == preferences);
    std::filesystem::remove(settingsPath);
}

TEST_CASE("FirstRun.SecondLaunchDoesNotShowTheControlsCardAgain", "[packaging][first_run]") {
    const auto settingsPath = std::filesystem::temp_directory_path() / "voxels_second_launch_settings.json";
    std::filesystem::remove(settingsPath);
    voxels::PreferencesManager manager(settingsPath);

    voxels::GamePreferences firstLaunch = manager.Load();
    REQUIRE_FALSE(firstLaunch.controlsCardSeen);
    manager.Save(firstLaunch);

    // Dismissing the controls card (ControlsCardState's "Got it" button) persists the flag.
    firstLaunch.controlsCardSeen = true;
    manager.Save(firstLaunch);

    const voxels::GamePreferences secondLaunch = manager.Load();
    REQUIRE(secondLaunch.controlsCardSeen);
    std::filesystem::remove(settingsPath);
}

TEST_CASE("PlatformServices.NullImplementationIsUsedWhenSteamDisabledAndAllCallsAreSafe", "[packaging][platform_services]") {
    const std::unique_ptr<voxels::IPlatformServices> services = voxels::CreatePlatformServices();
    REQUIRE(services->Name() == "Null");
    REQUIRE_FALSE(services->IsAvailable());
    REQUIRE(services->Initialize());
    services->Update();
    REQUIRE_FALSE(services->IsAchievementUnlocked(voxels::Achievement::FirstBlockBroken));
    services->UnlockAchievement(voxels::Achievement::FirstBlockBroken);
    REQUIRE(services->IsAchievementUnlocked(voxels::Achievement::FirstBlockBroken));
    // Unrelated achievements remain locked; unlocking is per-achievement, not global.
    REQUIRE_FALSE(services->IsAchievementUnlocked(voxels::Achievement::FirstCaveEntered));
    services->SetRichPresence("status", "Testing");
    REQUIRE_FALSE(services->IsOverlayActive());
    services->OnWorldSaved(std::filesystem::temp_directory_path());
    services->OnWorldLoaded(std::filesystem::temp_directory_path());
    services->Shutdown();
}

TEST_CASE("PlatformServices.AchievementTriggersAreFiredByRealGameplayEvents", "[packaging][platform_services]") {
    voxels::World world;
    for (int x = -1; x <= 1; ++x) {
        for (int z = -3; z <= 1; ++z) {
            world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        }
    }
    // A breakable block at z=-2 (destroyed below) and a separate, untouched roofed pocket at
    // z=1 with no line to the open sky: zero sky light is the real world-lighting signal used
    // for "the player entered a cave".
    world.SetBlock({0, 2, -2}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({0, 2, 1}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    world.SetBlock({0, 3, 1}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
    REQUIRE(world.GetSkyLight({0, 1, 1}) == 0);

    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::InputManager input;
    input.BindAction("DestroyBlock", {"DestroyBlock", 1, 0, voxels::InputDeviceType::Mouse});

    voxels::NullPlatformServices platformServices;
    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetInputManager(&input);
    session.SetPlatformServices(&platformServices);
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 0.5f});
    session.Initialize();

    REQUIRE_FALSE(platformServices.IsAchievementUnlocked(voxels::Achievement::FirstBlockBroken));
    REQUIRE_FALSE(platformServices.IsAchievementUnlocked(voxels::Achievement::FirstCaveEntered));

    // Real break: hold the bound "DestroyBlock" input and let GameSession's own mining-progress
    // simulation run to completion, exactly as InGameState drives it every frame.
    input.InjectMouseButtonEvent(1, true);
    session.Update(1.6f);
    REQUIRE(world.GetBlock({0, 2, -2}) == static_cast<voxels::BlockId>(voxels::BlockType::Air));
    REQUIRE(platformServices.IsAchievementUnlocked(voxels::Achievement::FirstBlockBroken));

    // Real cave-entry: walk the player into the roofed, unlit pocket and let GameSession's own
    // sky-light check (not a direct UnlockAchievement call) fire the achievement.
    session.SetPlayerSpawn(voxels::Vec3{0.5f, 1.9f, 1.5f});
    input.InjectMouseButtonEvent(1, false);
    session.Update(0.1f);
    REQUIRE(platformServices.IsAchievementUnlocked(voxels::Achievement::FirstCaveEntered));
    session.Shutdown();
}

TEST_CASE("PlatformServices.StructureAchievementFiresWhenAPlacementJoinsThreeSolidNeighbors", "[packaging][platform_services]") {
    voxels::World world;
    for (int x = 4; x <= 6; ++x) {
        for (int z = 2; z <= 7; ++z) {
            world.SetBlock({x, 0, z}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        }
    }
    world.SetBlock({5, 1, 4}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));  // below the notch
    world.SetBlock({4, 2, 4}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));  // west wall
    world.SetBlock({5, 2, 3}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));  // ray target (south)
    // Notch at {5, 2, 4} stays air: it already has 3 solid orthogonal neighbors, so placing into
    // it is a real "joined an existing structure" event.

    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::InputManager input;
    input.BindAction("PlaceBlock", {"PlaceBlock", 3, 0, voxels::InputDeviceType::Mouse});

    voxels::NullPlatformServices platformServices;
    voxels::GameSession session(&world);
    session.SetBlockRegistry(&registry);
    session.SetInputManager(&input);
    session.SetPlatformServices(&platformServices);
    session.SetPlayerSpawn(voxels::Vec3{5.5f, 1.9f, 6.5f});
    session.Initialize();
    session.GetPlayer().state.inventory.GetSlot(0) = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 2};
    session.GetPlayer().state.inventory.SetSelectedSlot(0);

    REQUIRE_FALSE(platformServices.IsAchievementUnlocked(voxels::Achievement::FirstStructureBuilt));
    input.InjectMouseButtonEvent(3, true);
    session.Update(0.1f);
    REQUIRE(world.GetBlock({5, 2, 4}) == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    REQUIRE(platformServices.IsAchievementUnlocked(voxels::Achievement::FirstStructureBuilt));
    session.Shutdown();
}
