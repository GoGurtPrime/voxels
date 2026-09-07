/**
 * @file test_ui_framework.cpp
 * @brief Behavioral tests for UI scale, input arbitration, and theme tokens.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <imgui.h>

#include "voxels/ui/imgui_ui_manager.hpp"
#include "voxels/app/player_ui_dispatcher.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/ui/player_ui.hpp"

TEST_CASE("UIScale.ComputesExpectedFactorsAcrossResolutions", "[ui]") {
    const auto scaleFor = [](int width, int height) {
        return voxels::ComputeUIScale({width, height, 1.0f});
    };
    REQUIRE(scaleFor(640, 480) == Catch::Approx(1.0f));
    REQUIRE(scaleFor(1280, 720) == Catch::Approx(1.5f));
    REQUIRE(scaleFor(1920, 1080) == Catch::Approx(2.25f));
    REQUIRE(scaleFor(3840, 2160) == Catch::Approx(4.5f));
}

TEST_CASE("PlayerUI.InputPoliciesCaptureAndDiscardOneTransitionDelta", "[player-ui]") {
    voxels::NullPlayerUI manager;
    manager.SetInputPolicy(voxels::PlayerUIInputPolicy::Overlay);
    REQUIRE(manager.CapturesMouse());
    REQUIRE(manager.CapturesKeyboard());
    manager.SetInputPolicy(voxels::PlayerUIInputPolicy::TextEntry);
    REQUIRE(manager.CapturesMouse());
    REQUIRE(manager.CapturesKeyboard());

    manager.SetInputPolicy(voxels::PlayerUIInputPolicy::Gameplay);
    REQUIRE(manager.ConsumeTransitionMouseDelta());
    REQUIRE_FALSE(manager.ConsumeTransitionMouseDelta());
    REQUIRE_FALSE(manager.CapturesMouse());
    REQUIRE_FALSE(manager.CapturesKeyboard());
}

TEST_CASE("PlayerUI.ProtocolAcceptsOnlyBoundedVersionedEnvelope", "[player-ui]") {
    const voxels::PlayerUIProtocolMessage message{.kind = "action", .requestId = 42, .payload = "resume"};
    const auto encoded = voxels::EncodePlayerUIProtocolMessage(message);
    REQUIRE(encoded.has_value());
    const auto decoded = voxels::DecodePlayerUIProtocolMessage(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->requestId == 42);
    REQUIRE(decoded->payload == "resume");

    REQUIRE_FALSE(voxels::DecodePlayerUIProtocolMessage(R"({"version":2,"kind":"action","requestId":42,"payload":"resume"})").has_value());
    REQUIRE_FALSE(voxels::DecodePlayerUIProtocolMessage(R"({"version":1,"kind":"action","requestId":0,"payload":"resume"})").has_value());
    REQUIRE_FALSE(voxels::EncodePlayerUIProtocolMessage({.kind = "action", .requestId = 1, .payload = std::string(64U * 1024U + 1U, 'x')}).has_value());
}

TEST_CASE("PlayerUI.DispatcherOnlyChangesStateForValidRouteAndRequest", "[player-ui]") {
    voxels::AppContext context{};
    bool transitionRequested = false;
    bool popRequested = false;
    context.requestTransition = [&transitionRequested](std::unique_ptr<voxels::IAppState>) { transitionRequested = true; };
    context.requestPopOverlay = [&popRequested]() { popRequested = true; };
    const voxels::PlayerUIActionDispatcher dispatcher;

    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::Pause, {.requestId = 1, .kind = voxels::PlayerUIActionKind::Resume}, context));
    REQUIRE(popRequested);
    popRequested = false;
    REQUIRE_FALSE(dispatcher.Dispatch(voxels::PlayerUIRoute::MainMenu, {.requestId = 1, .kind = voxels::PlayerUIActionKind::Resume}, context));
    REQUIRE_FALSE(popRequested);
    REQUIRE_FALSE(dispatcher.Dispatch(voxels::PlayerUIRoute::Error, {.requestId = 0, .kind = voxels::PlayerUIActionKind::AcknowledgeError}, context));
    REQUIRE_FALSE(transitionRequested);

    voxels::NullPlayerUI ui;
    ui.Publish({.route = voxels::PlayerUIRoute::Hud, .revision = 7, .title = "HUD"});
    REQUIRE(ui.LastModel().has_value());
    REQUIRE(ui.LastModel()->revision == 7);
}

TEST_CASE("PlayerUI.DispatcherCreatesWorldSaveBeforeLoading", "[player-ui]") {
    const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "voxels_player_ui_dispatcher";
    std::error_code error;
    std::filesystem::remove_all(saveRoot, error);
    voxels::SaveManager saveManager(saveRoot);
    voxels::AppContext context{};
    context.saveManager = &saveManager;
    bool loadingRequested = false;
    context.requestTransition = [&loadingRequested](std::unique_ptr<voxels::IAppState> state) {
        loadingRequested = state != nullptr && state->GetId() == voxels::AppStateId::LoadingScreen;
    };
    const voxels::PlayerUIAction action{
        .requestId = 41,
        .kind = voxels::PlayerUIActionKind::CreateWorld,
        .primary = "Web UI World",
        .secondary = R"({"seed":"violet mesa","sandbox":true,"peaceful":false,"permadeath":false,"sunny":true,"public":false,"distance":10})"
    };

    const voxels::PlayerUIActionDispatcher dispatcher;
    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::WorldCreation, action, context));
    REQUIRE(loadingRequested);
    const auto saves = saveManager.ListSaves();
    REQUIRE(saves.size() == 1);
    REQUIRE(saves.front().save.worldName == "Web UI World");
    REQUIRE(saves.front().save.seed == static_cast<voxels::WorldSeed>(voxels::SeedFromText("violet mesa")));
    REQUIRE(saves.front().save.publicVisibility == false);
    std::filesystem::remove_all(saveRoot, error);
}

TEST_CASE("PlayerUI.DispatcherAppliesSettingsFromStructuredPayload", "[player-ui]") {
    voxels::GamePreferences preferences{};
    preferences.fieldOfView = 90.0f;
    preferences.renderDistance = 8;
    preferences.simulationDistance = 4;
    preferences.masterVolume = 1.0f;
    preferences.musicVolume = 0.7f;
    preferences.sfxVolume = 0.8f;
    preferences.mouseSensitivity = 1.0f;
    preferences.invertY = false;
    preferences.particles = true;

    voxels::AppContext context{};
    context.preferences = &preferences;
    const voxels::PlayerUIActionDispatcher dispatcher;
    const voxels::PlayerUIAction action{
        .requestId = 77,
        .kind = voxels::PlayerUIActionKind::ApplySettings,
        .secondary = R"({"settings":{"fov":72.0,"renderDistance":16,"simulationDistance":12,"master":0.4,"music":0.5,"effects":0.6,"sensitivity":2.3,"invertY":true,"particles":false}})"};

    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::Settings, action, context));
    REQUIRE(preferences.fieldOfView == Catch::Approx(72.0f));
    REQUIRE(preferences.renderDistance == 16);
    REQUIRE(preferences.simulationDistance == 12);
    REQUIRE(preferences.masterVolume == Catch::Approx(0.4f));
    REQUIRE(preferences.musicVolume == Catch::Approx(0.5f));
    REQUIRE(preferences.sfxVolume == Catch::Approx(0.6f));
    REQUIRE(preferences.mouseSensitivity == Catch::Approx(2.3f));
    REQUIRE(preferences.invertY);
    REQUIRE_FALSE(preferences.particles);
}

TEST_CASE("Theme.AppliesConsistentTokenSetAndIsIdempotent", "[ui]") {
    ImGui::CreateContext();
    voxels::ApplyVoxelsTheme();
    const ImVec4 firstButton = ImGui::GetStyle().Colors[ImGuiCol_Button];
    const float firstPadding = ImGui::GetStyle().FramePadding.x;
    voxels::ApplyVoxelsTheme();

    REQUIRE(ImGui::GetStyle().Colors[ImGuiCol_Button].x == Catch::Approx(firstButton.x));
    REQUIRE(ImGui::GetStyle().Colors[ImGuiCol_Button].y == Catch::Approx(firstButton.y));
    REQUIRE(ImGui::GetStyle().FramePadding.x == Catch::Approx(firstPadding));
    ImGui::DestroyContext();
}

TEST_CASE("DebugOverlay.RetainsLiveFrameStats", "[ui]") {
    voxels::ImGuiUIManager manager;
    voxels::UIDebugMetrics metrics{};
    metrics.frameMilliseconds = 16.67f;
    metrics.framesPerSecond = 60.0f;
    metrics.loadedChunks = 49;
    metrics.glVendor = "Test Vendor";
    metrics.glRenderer = "Test Renderer";
    metrics.glVersion = "3.3";

    manager.SetDebugMetrics(metrics);
    const auto& retained = manager.GetDebugMetrics();
    REQUIRE(retained.frameMilliseconds == Catch::Approx(16.67f));
    REQUIRE(retained.framesPerSecond == Catch::Approx(60.0f));
    REQUIRE(retained.loadedChunks == 49);
    REQUIRE(retained.glVendor == "Test Vendor");
    REQUIRE(retained.glRenderer == "Test Renderer");
    REQUIRE(retained.glVersion == "3.3");
}