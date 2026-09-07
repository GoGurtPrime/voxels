/**
 * @file test_ui_framework.cpp
 * @brief Behavioral tests for UI scale, input arbitration, and theme tokens.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "voxels/app/display_settings.hpp"
#include "voxels/ui/imgui_ui_manager.hpp"
#include "voxels/app/player_ui_dispatcher.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/graphics/renderer.hpp"
#include "voxels/platform/platform.hpp"
#include "voxels/ui/player_ui.hpp"

namespace {

class TrackingPlatform final : public voxels::IPlatform {
public:
    voxels::PlatformContext GetContext() const override {
        return {.type = voxels::PlatformType::Windows, .name = "TrackingPlatform"};
    }

    bool Initialize(const voxels::WindowConfig&) override { return true; }
    void Shutdown() override {}
    void PollEvents(voxels::IPlatformEventListener*) override {}
    void SwapBuffers() override {}

    bool ApplyWindowDisplayConfig(const voxels::WindowDisplayConfig& config) override {
        ++displayConfigCalls;
        lastDisplayConfig = config;
        isFullscreen = config.mode == voxels::WindowPresentationMode::Fullscreen;
        isBorderless = config.mode == voxels::WindowPresentationMode::Borderless;
        windowWidth = config.width;
        windowHeight = config.height;
        return displayConfigSucceeds;
    }

    void SetWindowFullscreen(bool fullscreen) override {
        ++fullscreenCalls;
        isFullscreen = fullscreen;
    }

    void SetWindowBorderless(bool borderless) override {
        ++borderlessCalls;
        isBorderless = borderless;
    }

    void SetWindowResizable(bool) override {}

    void SetWindowResolution(int width, int height) override {
        ++resolutionCalls;
        windowWidth = width;
        windowHeight = height;
    }

    void SetWindowTitle(const std::string&) override {}
    void SetRelativeMouseMode(bool) override {}
    void SetCursorVisible(bool) override {}
    void SetVSync(bool) override {}

    std::pair<int, int> GetDrawableSize() const override {
        return {windowWidth, windowHeight};
    }

    voxels::WindowMetrics GetWindowMetrics() const override {
        return {windowWidth, windowHeight, windowWidth, windowHeight};
    }

    double GetHighResTimeSeconds() const override { return 0.0; }

    int fullscreenCalls = 0;
    int borderlessCalls = 0;
    int resolutionCalls = 0;
    int displayConfigCalls = 0;
    bool isFullscreen = false;
    bool isBorderless = false;
    bool displayConfigSucceeds = true;
    int windowWidth = 1920;
    int windowHeight = 1080;
    voxels::WindowDisplayConfig lastDisplayConfig{};
};

class PreviewCaptureRenderer final : public voxels::graphics::IGraphicsRenderer {
public:
    bool Initialize(voxels::IPlatform&, voxels::TextureAtlas&, bool) override { return true; }
    void Shutdown() override {}
    bool BeginFrame(const std::array<float, 4>&) override { return true; }
    bool EndFrame() override { return true; }
    bool Present() override { return true; }
    void SetViewport(int, int) override {}
    void SetCamera(const voxels::Camera& camera) override { m_camera = camera; }
    [[nodiscard]] const voxels::Camera& GetCamera() const noexcept override { return m_camera; }
    [[nodiscard]] voxels::RendererBackend GetBackend() const noexcept override {
        return voxels::RendererBackend::OpenGL;
    }
    [[nodiscard]] std::string_view GetName() const noexcept override { return "PreviewCaptureRenderer"; }
    [[nodiscard]] bool CaptureScreenshot(const std::filesystem::path& path) const override {
        ++captureCount;
        capturedPath = path;
        std::ofstream(path, std::ios::binary) << "png";
        return std::filesystem::is_regular_file(path);
    }

    mutable int captureCount = 0;
    mutable std::filesystem::path capturedPath;

private:
    voxels::Camera m_camera{};
};

class GlobalRendererScope final {
public:
    explicit GlobalRendererScope(voxels::graphics::IGraphicsRenderer* renderer) {
        voxels::SetGlobalRenderer(renderer);
    }
    ~GlobalRendererScope() { voxels::SetGlobalRenderer(nullptr); }
};

} // namespace

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

TEST_CASE("PlayerUI.PlaySkipsEmptyWorldSelection", "[player-ui][world-select]") {
    const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "voxels_empty_world_select";
    std::error_code error;
    std::filesystem::remove_all(saveRoot, error);
    voxels::SaveManager saveManager(saveRoot);
    voxels::AppStateId requestedState = voxels::AppStateId::Boot;
    voxels::AppContext context{};
    context.saveManager = &saveManager;
    context.requestTransition = [&requestedState](std::unique_ptr<voxels::IAppState> state) {
        requestedState = state->GetId();
    };

    const voxels::PlayerUIActionDispatcher dispatcher;
    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::MainMenu,
                                {.requestId = 1, .kind = voxels::PlayerUIActionKind::Play}, context));
    REQUIRE(requestedState == voxels::AppStateId::WorldCreation);
    std::filesystem::remove_all(saveRoot, error);
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
        .secondary = R"({"seed":"violetMesa","sandbox":true,"peaceful":false,"permadeath":false,"sunny":true,"public":false,"distance":10})"
    };

    const voxels::PlayerUIActionDispatcher dispatcher;
    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::WorldCreation, action, context));
    REQUIRE(loadingRequested);
    const auto saves = saveManager.ListSaves();
    REQUIRE(saves.size() == 1);
    REQUIRE(saves.front().save.worldName == "Web UI World");
    REQUIRE(saves.front().save.seed == static_cast<voxels::WorldSeed>(voxels::SeedFromText("violetMesa")));
    REQUIRE(saves.front().save.publicVisibility == false);
    std::filesystem::remove_all(saveRoot, error);
}

TEST_CASE("PlayerUI.DispatcherRejectsSeedCharactersOutsideLettersAndNumbers", "[player-ui][world-create]") {
    const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "voxels_invalid_seed";
    std::error_code error;
    std::filesystem::remove_all(saveRoot, error);
    voxels::SaveManager saveManager(saveRoot);
    voxels::AppStateId requestedState = voxels::AppStateId::Boot;
    voxels::AppContext context{};
    context.saveManager = &saveManager;
    context.requestTransition = [&requestedState](std::unique_ptr<voxels::IAppState> state) {
        requestedState = state->GetId();
    };

    const voxels::PlayerUIActionDispatcher dispatcher;
    REQUIRE(dispatcher.Dispatch(
        voxels::PlayerUIRoute::WorldCreation,
        {.requestId = 2,
         .kind = voxels::PlayerUIActionKind::CreateWorld,
         .primary = "Invalid Seed World",
         .secondary = R"({"seed":"not valid!","distance":8})"},
        context));
    REQUIRE(requestedState == voxels::AppStateId::Error);
    REQUIRE(saveManager.ListSaves().empty());
    std::filesystem::remove_all(saveRoot, error);
}

TEST_CASE("PlayerUI.PauseActionsRetainNativeSideEffects", "[player-ui][pause]") {
    const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "voxels_pause_actions";
    std::error_code error;
    std::filesystem::remove_all(saveRoot, error);
    voxels::SaveManager saveManager(saveRoot);
    voxels::GameSave save{};
    save.saveName = "PauseWorld";
    save.worldName = "Pause World";
    save.playerName = "Player";
    save.publicVisibility = false;
    REQUIRE(saveManager.Save(save));

    voxels::AppContext context{};
    context.saveManager = &saveManager;
    voxels::InGameState game(&context);
    game.SetActiveSave(save);
    context.activeGame = &game;
    voxels::AppStateId requestedState = voxels::AppStateId::Boot;
    voxels::AppStateId requestedOverlay = voxels::AppStateId::Boot;
    bool quitRequested = false;
    context.requestTransition = [&requestedState](std::unique_ptr<voxels::IAppState> state) {
        requestedState = state->GetId();
    };
    context.requestPushOverlay = [&requestedOverlay](std::unique_ptr<voxels::IAppState> state) {
        requestedOverlay = state->GetId();
    };
    context.requestQuit = [&quitRequested]() { quitRequested = true; };

    const voxels::PlayerUIActionDispatcher dispatcher;
    REQUIRE(dispatcher.Dispatch(
        voxels::PlayerUIRoute::Pause,
        {.requestId = 3, .kind = voxels::PlayerUIActionKind::ToggleWorldVisibility, .value = 1.0f},
        context));
    voxels::GameSave persisted{};
    REQUIRE(saveManager.Load(save.saveName, persisted));
    REQUIRE(persisted.publicVisibility);

    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::Pause,
                                {.requestId = 4, .kind = voxels::PlayerUIActionKind::OpenControls}, context));
    REQUIRE(requestedOverlay == voxels::AppStateId::ControlsCard);
    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::Pause,
                                {.requestId = 5, .kind = voxels::PlayerUIActionKind::ReturnToMainMenu}, context));
    REQUIRE(requestedState == voxels::AppStateId::Boot);
    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::Pause,
                                {.requestId = 6, .kind = voxels::PlayerUIActionKind::ExitToDesktop}, context));
    REQUIRE_FALSE(quitRequested);
    std::filesystem::remove_all(saveRoot, error);
}

TEST_CASE("PlayerUI.RemotePauseReturnRestoresLocalNetworking", "[player-ui][pause][networking]") {
    voxels::AppContext context{};
    voxels::InGameState game(&context);
    game.SetRemoteSession(true);
    context.activeGame = &game;
    bool resetRequested = false;
    bool menuRequested = false;
    context.resetNetworkToLocal = [&resetRequested]() { resetRequested = true; };
    context.requestTransition = [&menuRequested](std::unique_ptr<voxels::IAppState> state) {
        menuRequested = state->GetId() == voxels::AppStateId::MainMenu;
    };

    const voxels::PlayerUIActionDispatcher dispatcher;
    REQUIRE(dispatcher.Dispatch(
        voxels::PlayerUIRoute::Pause,
        {.requestId = 7, .kind = voxels::PlayerUIActionKind::ReturnToMainMenu}, context));
    REQUIRE(resetRequested);
    REQUIRE(menuRequested);
}

TEST_CASE("PlayerUI.LocalPauseExitCapturesPreviewBeforeLeaving", "[player-ui][pause][preview]") {
    const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "voxels_pause_preview";
    std::error_code error;
    std::filesystem::remove_all(saveRoot, error);
    voxels::SaveManager saveManager(saveRoot);
    voxels::GameSave save{};
    save.saveName = "Preview World";
    save.worldName = "Preview World";
    save.playerName = "Player";
    REQUIRE(saveManager.Save(save));

    voxels::AppStateId requestedState = voxels::AppStateId::Boot;
    bool quitRequested = false;
    voxels::AppContext context{};
    context.saveManager = &saveManager;
    context.requestTransition = [&requestedState](std::unique_ptr<voxels::IAppState> state) {
        requestedState = state->GetId();
    };
    context.requestQuit = [&quitRequested]() { quitRequested = true; };
    voxels::InGameState game(&context);
    game.SetActiveSave(save);
    PreviewCaptureRenderer renderer;
    const GlobalRendererScope rendererScope(&renderer);

    game.RequestSaveAndReturnToMenu();
    REQUIRE(requestedState == voxels::AppStateId::Boot);
    game.Render();
    REQUIRE(renderer.captureCount == 1);
    REQUIRE(std::filesystem::is_regular_file(saveManager.GetWorldPreviewPath(save.saveName)));
    REQUIRE(requestedState == voxels::AppStateId::MainMenu);

    game.RequestSaveAndExitToDesktop();
    REQUIRE_FALSE(quitRequested);
    game.Render();
    REQUIRE(renderer.captureCount == 2);
    REQUIRE(std::filesystem::is_regular_file(saveManager.GetWorldPreviewPath(save.saveName)));
    REQUIRE(quitRequested);
    std::filesystem::remove_all(saveRoot, error);
}

TEST_CASE("PlayerUI.FirstLocalWorldFrameCreatesMissingPreview", "[player-ui][world-select][preview]") {
    const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "voxels_first_world_preview";
    std::error_code error;
    std::filesystem::remove_all(saveRoot, error);
    voxels::SaveManager saveManager(saveRoot);
    voxels::GameSave save{};
    save.saveName = "New World";
    save.worldName = "New World";
    save.playerName = "Player";
    REQUIRE(saveManager.Save(save));

    voxels::AppContext context{};
    context.saveManager = &saveManager;
    voxels::InGameState game(&context);
    game.SetActiveSave(save);
    PreviewCaptureRenderer renderer;
    const GlobalRendererScope rendererScope(&renderer);

    game.OnEnter();
    game.Render();
    REQUIRE(renderer.captureCount == 1);
    REQUIRE(std::filesystem::is_regular_file(saveManager.GetWorldPreviewPath(save.saveName)));
    game.OnExit();
    std::filesystem::remove_all(saveRoot, error);
}

TEST_CASE("PlayerUI.WorldSelectionAddsPreviewUrlOnlyWhenImageExists", "[player-ui][world-select][preview]") {
    const std::filesystem::path saveRoot = std::filesystem::temp_directory_path() / "voxels_world_preview_model";
    std::error_code error;
    std::filesystem::remove_all(saveRoot, error);
    voxels::SaveManager saveManager(saveRoot);
    voxels::GameSave save{};
    save.saveName = "Preview World";
    save.worldName = "Preview World";
    REQUIRE(saveManager.Save(save));

    voxels::NullPlayerUI ui;
    voxels::AppContext context{};
    context.saveManager = &saveManager;
    context.ui = &ui;
    voxels::WorldSelectState state(&context);
    state.OnEnter();
    nlohmann::json payload = nlohmann::json::parse(ui.LastModel()->payload);
    REQUIRE_FALSE(payload["worlds"][0].contains("previewUrl"));

    std::ofstream(saveManager.GetWorldPreviewPath(save.saveName), std::ios::binary) << "png";
    state.OnEnter();
    payload = nlohmann::json::parse(ui.LastModel()->payload);
    REQUIRE(payload["worlds"][0]["previewUrl"] ==
            "voxels-ui://app/world-preview/Preview%20World.png");
    std::filesystem::remove_all(saveRoot, error);
}

TEST_CASE("PlayerUI.DispatcherAppliesSettingsFromStructuredPayload", "[player-ui]") {
    voxels::GamePreferences preferences{};
    preferences.windowMode = voxels::WindowMode::Windowed;
    preferences.resolution = {1280, 720, 60};
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
        .secondary = R"({"settings":{"windowMode":"Fullscreen","resolutionWidth":1920,"resolutionHeight":1080,"fov":72.0,"renderDistance":16,"simulationDistance":12,"master":0.4,"music":0.5,"effects":0.6,"sensitivity":2.3,"invertY":true,"particles":false}})"};

    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::Settings, action, context));
    REQUIRE(preferences.windowMode == voxels::WindowMode::Fullscreen);
    REQUIRE(preferences.resolution.width == 1920);
    REQUIRE(preferences.resolution.height == 1080);
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

TEST_CASE("PlayerUI.DispatcherIgnoresPayloadlessApplySettingsForDisplayMode", "[player-ui]") {
    voxels::GamePreferences preferences{};
    preferences.windowMode = voxels::WindowMode::Borderless;
    preferences.resolution = {1280, 720, 60};

    TrackingPlatform platform;
    voxels::AppContext context{};
    context.preferences = &preferences;
    context.platform = &platform;

    const voxels::PlayerUIActionDispatcher dispatcher;
    const voxels::PlayerUIAction action{
        .requestId = 501,
        .kind = voxels::PlayerUIActionKind::ApplySettings,
        .secondary = ""};

    REQUIRE_FALSE(dispatcher.Dispatch(voxels::PlayerUIRoute::Settings, action, context));
    REQUIRE(platform.fullscreenCalls == 0);
    REQUIRE(platform.borderlessCalls == 0);
    REQUIRE(platform.resolutionCalls == 0);
    REQUIRE(platform.displayConfigCalls == 0);
    REQUIRE(preferences.windowMode == voxels::WindowMode::Borderless);
    REQUIRE(preferences.resolution.width == 1280);
    REQUIRE(preferences.resolution.height == 720);
}

TEST_CASE("PlayerUI.DispatcherRollsBackUnavailableDisplayMode", "[player-ui][settings]") {
    voxels::GamePreferences preferences{};
    preferences.windowMode = voxels::WindowMode::Windowed;
    preferences.resolution = {1280, 720, 60};

    TrackingPlatform platform;
    platform.displayConfigSucceeds = false;
    bool errorRequested = false;
    voxels::AppContext context{};
    context.preferences = &preferences;
    context.platform = &platform;
    context.requestTransition = [&errorRequested](std::unique_ptr<voxels::IAppState>) {
        errorRequested = true;
    };

    const voxels::PlayerUIActionDispatcher dispatcher;
    const voxels::PlayerUIAction action{
        .requestId = 502,
        .kind = voxels::PlayerUIActionKind::ApplySettings,
        .secondary = R"({"settings":{"windowMode":"Fullscreen","resolutionWidth":1920,"resolutionHeight":1080}})"};

    REQUIRE(dispatcher.Dispatch(voxels::PlayerUIRoute::Settings, action, context));
    REQUIRE(platform.displayConfigCalls == 1);
    REQUIRE(errorRequested);
    REQUIRE(preferences.windowMode == voxels::WindowMode::Windowed);
    REQUIRE(preferences.resolution.width == 1280);
    REQUIRE(preferences.resolution.height == 720);
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

TEST_CASE("DisplaySettings.AppliesOneAtomicClampedTransition", "[platform][settings]") {
    TrackingPlatform platform;
    const voxels::Resolution resolution{9000, 200, 60};

    REQUIRE(voxels::ApplyWindowPreferences(platform, voxels::WindowMode::Fullscreen, resolution));
    REQUIRE(platform.displayConfigCalls == 1);
    REQUIRE(platform.fullscreenCalls == 0);
    REQUIRE(platform.borderlessCalls == 0);
    REQUIRE(platform.resolutionCalls == 0);
    REQUIRE(platform.lastDisplayConfig.width == 7680);
    REQUIRE(platform.lastDisplayConfig.height == 360);
    REQUIRE(platform.lastDisplayConfig.mode == voxels::WindowPresentationMode::Fullscreen);
    REQUIRE_FALSE(platform.lastDisplayConfig.resizable);
}