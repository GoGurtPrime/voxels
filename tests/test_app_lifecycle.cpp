/**
 * @file test_app_lifecycle.cpp
 * @brief Automated regression tests for work item 08's app lifecycle systems.
 *
 * @details Covers CLI argument parsing, app state machine transition ordering, save manager
 *          directory/metadata round-tripping, menu logic controllers, and UI scale
 *          computation for the loading screen / responsive layout requirements.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/menus.hpp"
#include "voxels/app/save_manager.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/ui/ui_manager.hpp"

TEST_CASE("CLIParser.ParseArguments", "[app][cli]") {
    const voxels::CliParser parser;
    const std::vector<std::string> args = {"--fullscreen=false", "--resolution=1280x720", "--server"};

    const voxels::AppCommandLineOptions options = parser.Parse(args);

    REQUIRE(options.fullscreenOverride);
    REQUIRE_FALSE(options.fullscreenValue);
    REQUIRE(options.resolutionOverride);
    REQUIRE(options.resolutionWidth == 1280);
    REQUIRE(options.resolutionHeight == 720);
    REQUIRE(options.serverMode);
    REQUIRE_FALSE(options.worldNameOverride);
    REQUIRE_FALSE(options.seedOverride);
    REQUIRE_FALSE(options.renderDistanceOverride);
}

TEST_CASE("CLIParser.ParsesWorldSeedAndRenderDistance", "[app][cli]") {
    const voxels::CliParser parser;
    const std::vector<std::string> args = {"--world=TestWorld", "--seed=12345", "--render-distance=16"};

    const voxels::AppCommandLineOptions options = parser.Parse(args);

    REQUIRE(options.worldNameOverride);
    REQUIRE(options.worldName == "TestWorld");
    REQUIRE(options.seedOverride);
    REQUIRE(options.seed == 12345u);
    REQUIRE(options.renderDistanceOverride);
    REQUIRE(options.renderDistance == 16);
}

TEST_CASE("CLIParser.IgnoresUnknownFlags", "[app][cli]") {
    const voxels::CliParser parser;
    const std::vector<std::string> args = {"--unknown-flag=value", "notaflag"};

    const voxels::AppCommandLineOptions options = parser.Parse(args);

    REQUIRE_FALSE(options.fullscreenOverride);
    REQUIRE_FALSE(options.serverMode);
}

TEST_CASE("StateMachine.TransitionOrder", "[app][state]") {
    voxels::AppStateMachine machine;
    machine.Start(std::make_unique<voxels::MainMenuState>());

    REQUIRE(machine.GetCurrentState()->GetId() == voxels::AppStateId::MainMenu);

    machine.TransitionTo(std::make_unique<voxels::LoadingScreenState>());

    REQUIRE(machine.GetCurrentState()->GetId() == voxels::AppStateId::LoadingScreen);

    const auto& log = machine.GetTransitionLog();
    REQUIRE(log.size() == 3);
    REQUIRE(log[0].state == voxels::AppStateId::MainMenu);
    REQUIRE(log[0].entered);
    REQUIRE(log[1].state == voxels::AppStateId::MainMenu);
    REQUIRE_FALSE(log[1].entered);
    REQUIRE(log[2].state == voxels::AppStateId::LoadingScreen);
    REQUIRE(log[2].entered);
}

TEST_CASE("StateMachine.FullMenuFlow", "[app][state]") {
    voxels::AppStateMachine machine;
    machine.Start(std::make_unique<voxels::BootState>());
    machine.TransitionTo(std::make_unique<voxels::MainMenuState>());
    machine.TransitionTo(std::make_unique<voxels::WorldCreationState>());
    machine.TransitionTo(std::make_unique<voxels::LoadingScreenState>());
    machine.TransitionTo(std::make_unique<voxels::InGameState>());
    machine.TransitionTo(std::make_unique<voxels::PauseMenuState>());

    REQUIRE(machine.GetCurrentState()->GetId() == voxels::AppStateId::PauseMenu);
    // 1 initial enter + 5 transitions * (exit + enter) = 11 log entries.
    REQUIRE(machine.GetTransitionLog().size() == 11);
}

TEST_CASE("SaveManager.CreateAndListSaves", "[app][save]") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "voxels_test_saves_create_and_list";
    std::filesystem::remove_all(root);

    voxels::SaveManager manager(root);

    voxels::GameSave save{};
    save.saveName = "TestWorld";
    save.worldName = "TestWorld";
    save.playerName = "Steve";
    save.lastPlayedAt = "2026-01-01T00:00:00Z";
    save.seed = 42;
    save.publicVisibility = false;

    REQUIRE(manager.Save(save));
    REQUIRE(std::filesystem::exists(root / "TestWorld"));
    REQUIRE(std::filesystem::exists(root / "TestWorld" / "level.json"));

    const std::vector<voxels::SaveSlot> slots = manager.ListSaves();
    REQUIRE(slots.size() == 1);
    REQUIRE(slots[0].slotName == "TestWorld");
    REQUIRE(slots[0].save.worldName == "TestWorld");
    REQUIRE(slots[0].save.playerName == "Steve");
    REQUIRE(slots[0].save.seed == 42u);
    REQUIRE_FALSE(slots[0].save.publicVisibility);

    voxels::GameSave loaded{};
    REQUIRE(manager.Load("TestWorld", loaded));
    REQUIRE(loaded.playerName == "Steve");

    std::filesystem::remove_all(root);
}

TEST_CASE("SaveManager.DeleteSaveRemovesDirectory", "[app][save]") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "voxels_test_saves_delete";
    std::filesystem::remove_all(root);

    voxels::SaveManager manager(root);
    voxels::GameSave save{};
    save.saveName = "ToDelete";
    REQUIRE(manager.Save(save));
    REQUIRE(manager.DeleteSave("ToDelete"));
    REQUIRE_FALSE(std::filesystem::exists(root / "ToDelete"));

    std::filesystem::remove_all(root);
}

TEST_CASE("Menus.WorldCreationBuildsGameSave", "[app][menu]") {
    voxels::WorldCreationController controller;
    controller.SetWorldName("NewWorld");
    controller.SetSeed(777u);
    controller.SetPeaceful(true);
    controller.SetSandboxMode(true);
    controller.SetPublic(false);

    const voxels::GameSave save = controller.BuildGameSave("Alex");

    REQUIRE(save.saveName == "NewWorld");
    REQUIRE(save.worldName == "NewWorld");
    REQUIRE(save.playerName == "Alex");
    REQUIRE(save.seed == 777u);
    REQUIRE_FALSE(save.publicVisibility);
    REQUIRE(controller.GetWorldOptions().peaceful);
    REQUIRE(controller.GetWorldOptions().sandboxMode);
}

TEST_CASE("Menus.PauseMenuTogglesVisibility", "[app][menu]") {
    voxels::GameSave save{};
    save.publicVisibility = true;

    voxels::PauseMenuController controller(save);
    REQUIRE(controller.IsPublic());

    controller.Apply(voxels::PauseMenuAction::ToggleVisibility);
    REQUIRE_FALSE(controller.IsPublic());
    REQUIRE_FALSE(save.publicVisibility);
}

TEST_CASE("Menus.LoadingScreenProgressIsMonotonic", "[app][menu]") {
    voxels::LoadingScreenModel model;
    REQUIRE(model.GetProgress() == 0.0f);

    model.SetPhase(voxels::GenerationPhase::Caves);
    const float afterCaves = model.GetProgress();
    model.SetPhase(voxels::GenerationPhase::Vegetation);
    const float afterVegetation = model.GetProgress();
    model.SetPhase(voxels::GenerationPhase::SpawnPlacement);
    const float afterSpawn = model.GetProgress();
    model.SetPhase(voxels::GenerationPhase::Complete);

    REQUIRE(afterCaves > 0.0f);
    REQUIRE(afterVegetation > afterCaves);
    REQUIRE(afterSpawn > afterVegetation);
    REQUIRE(model.GetProgress() == 1.0f);
}

TEST_CASE("NullUIManager.ScaleMatchesFixedDreamcastOutput", "[app][ui]") {
    voxels::UIDisplayMetrics metrics{};
    metrics.windowWidth = 640;
    metrics.windowHeight = 480;
    metrics.dpiScale = 1.0f;

    REQUIRE(voxels::ComputeUIScale(metrics) == 1.0f);
}

TEST_CASE("NullUIManager.ScaleRespondsToHighDpiResize", "[app][ui]") {
    voxels::NullUIManager manager;
    REQUIRE(manager.Initialize(nullptr, nullptr));
    REQUIRE(manager.GetUIScale() > 0.0f);

    voxels::PlatformEvent event{};
    event.type = voxels::PlatformEventType::WindowResized;
    event.width = 1920;
    event.height = 1080;
    manager.OnPlatformEvent(event);

    // min(1920/640, 1080/480) == 2.25
    REQUIRE(manager.GetUIScale() > 2.2f);
    REQUIRE(manager.GetUIScale() < 2.3f);

    REQUIRE_FALSE(manager.IsFrameActive());
    manager.BeginFrame();
    REQUIRE(manager.IsFrameActive());
    manager.EndFrame();
    REQUIRE_FALSE(manager.IsFrameActive());

    manager.Shutdown();
}
