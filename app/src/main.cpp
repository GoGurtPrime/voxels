/*
 * Scope: Application bootstrap and lifecycle entry point.
 *
 * This file is the placeholder for launching the runtime game, parsing command-line options,
 * and transitioning through the menu, world setup, loading, and gameplay states.
 *
 * Relation to the rest of the codebase: the game app depends on the engine runtime services
 * and should remain focused on orchestration rather than low-level systems implementation.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/core/job_system.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/core/preferences.hpp"
#include "voxels/engine.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/platform/platform.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/render/texture_forge.hpp"
#include "voxels/ui/imgui_ui_manager.hpp"
#include "voxels/world/block.hpp"

namespace {

class WindowEventListener final : public voxels::IPlatformEventListener {
public:
    WindowEventListener(bool& runningFlag, voxels::graphics::GLRenderer& renderer, voxels::InputManager& inputManager,
                        voxels::ImGuiUIManager& uiManager)
        : m_running(runningFlag), m_renderer(renderer), m_inputManager(inputManager), m_uiManager(uiManager) {}

    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        if (event.type == voxels::PlatformEventType::WindowClosed ||
            event.type == voxels::PlatformEventType::QuitRequested) {
            m_running = false;
        } else if (event.type == voxels::PlatformEventType::WindowResized) {
            m_renderer.SetViewport(event.width, event.height);
        } else if (event.type == voxels::PlatformEventType::KeyDown) {
            if (event.keyCode == 1073741884U) {
                m_uiManager.ToggleDebugOverlay();
                return;
            }
            if (m_uiManager.WantsKeyboardCapture()) return;
            m_inputManager.InjectKeyEvent(static_cast<int>(event.keyCode), true);
        } else if (event.type == voxels::PlatformEventType::KeyUp) {
            if (m_uiManager.WantsKeyboardCapture()) return;
            m_inputManager.InjectKeyEvent(static_cast<int>(event.keyCode), false);
        } else if (event.type == voxels::PlatformEventType::MouseMotion) {
            if (m_uiManager.ConsumeFirstMouseDelta() || m_uiManager.WantsMouseCapture()) return;
            m_inputManager.InjectMouseDelta(static_cast<float>(event.relativeX), static_cast<float>(event.relativeY));
        } else if (event.type == voxels::PlatformEventType::MouseButtonDown) {
            if (m_uiManager.WantsMouseCapture()) return;
            m_inputManager.InjectMouseButtonEvent(static_cast<int>(event.button), true);
        } else if (event.type == voxels::PlatformEventType::MouseButtonUp) {
            if (m_uiManager.WantsMouseCapture()) return;
            m_inputManager.InjectMouseButtonEvent(static_cast<int>(event.button), false);
        } else if (event.type == voxels::PlatformEventType::MouseWheel) {
            if (m_uiManager.WantsMouseCapture()) return;
            m_inputManager.InjectMouseWheel(event.wheelY);
        }
    }

private:
    bool& m_running;
    voxels::graphics::GLRenderer& m_renderer;
    voxels::InputManager& m_inputManager;
    voxels::ImGuiUIManager& m_uiManager;
};

constexpr double kFixedStepSeconds = 1.0 / 60.0;
constexpr char kSaveFormatDirectory[] = "v1";

void RemoveUnversionedSaves() {
    const std::filesystem::path savesRoot = voxels::Paths::SavesDir();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(savesRoot, error)) {
        if (error || !entry.is_directory() || entry.path().filename() == kSaveFormatDirectory) continue;
        std::filesystem::remove_all(entry.path(), error);
        if (error) return;
    }
}

std::string GetOpenGLString(GLenum name) {
    const auto* value = glGetString(name);
    return value == nullptr ? "Unavailable" : reinterpret_cast<const char*>(value);
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const voxels::CliParser cliParser;
    const voxels::AppCommandLineOptions options = cliParser.Parse(args);

    if (options.forgeInteractionAssets) {
        const std::filesystem::path outputPath = options.forgeInteractionAssetsPath.empty()
            ? voxels::Paths::AssetsDir()
            : std::filesystem::path(options.forgeInteractionAssetsPath);
        const std::size_t count = voxels::TextureForge::ForgeInteractionAssets(outputPath, true);
        if (count != 13) {
            std::cerr << "Failed to forge all interaction assets under " << outputPath.string() << ".\n";
            return 1;
        }
        std::cout << "Forged " << count << " interaction assets under " << outputPath.string() << ".\n";
        return 0;
    }

    if (options.serverMode) {
        std::cout << "Voxels app booting in headless server mode." << std::endl;
        voxels::networking::GameServer server;
        if (!server.Start()) {
            std::cerr << "Voxels server failed to start." << std::endl;
            return 1;
        }
        std::cout << "Voxels server listening on port " << server.Port() << "." << std::endl;
        for (int tick = 0; tick < std::max(1, options.maxTicks > 0 ? options.maxTicks : 1); ++tick) {
            server.Tick();
        }
        server.Stop();
        return 0;
    }

    if (options.headless) {
        voxels::Engine engine;
        if (!engine.initialize(true)) {
            std::cerr << "Voxels engine failed to initialize in headless mode." << std::endl;
            return 1;
        }

        voxels::BlockRegistry blockRegistry = voxels::CreateDefaultBlockRegistry();
        voxels::TextureAtlas textureAtlas(16, 16);
        textureAtlas.PopulateFromBlockRegistry(blockRegistry, voxels::Paths::AssetsDir() / "textures");

        if (options.dumpAtlas) {
            std::filesystem::path dumpPath = options.dumpAtlasPath.empty()
                ? (voxels::Paths::LogsDir() / "atlas_dump.png")
                : std::filesystem::path(options.dumpAtlasPath);
            std::error_code ec;
            if (dumpPath.has_parent_path()) {
                std::filesystem::create_directories(dumpPath.parent_path(), ec);
            }
            if (textureAtlas.DumpAtlasToPng(dumpPath)) {
                std::cout << "Texture atlas successfully dumped to " << dumpPath.string()
                          << " (" << textureAtlas.GetLayerCount() << " layers).\n";
            }
        }

        const int maxFrames = options.maxFrames > 0 ? options.maxFrames : 2;
        for (int frame = 0; frame < maxFrames; ++frame) {
            auto* platform = engine.getPlatform();
            if (platform != nullptr) {
                platform->PollEvents(nullptr);
                platform->SwapBuffers();
            }
        }

        engine.shutdown();
        std::cout << "Voxels headless smoke test completed after " << maxFrames << " frames.\n";
        return 0;
    }

    voxels::Engine engine;
    if (!engine.initialize(false)) {
        std::cerr << "Voxels engine failed to initialize." << std::endl;
        return 1;
    }

    auto* platform = engine.getPlatform();
    if (platform != nullptr) {
        const int windowWidth = options.resolutionOverride ? options.resolutionWidth : 1280;
        const int windowHeight = options.resolutionOverride ? options.resolutionHeight : 720;
        const bool windowFullscreen = options.fullscreenOverride ? options.fullscreenValue : false;

        platform->SetWindowTitle("Voxels Engine");
        platform->SetWindowResolution(windowWidth, windowHeight);
        platform->SetWindowFullscreen(windowFullscreen);
        if (options.vsyncOverride) {
            platform->SetVSync(options.vsyncValue);
        }
    }

    voxels::BlockRegistry blockRegistry = voxels::CreateDefaultBlockRegistry();
    std::cout << "Voxels block catalogue loaded with " << blockRegistry.Count() << " registered block definitions.\n";

    voxels::TextureAtlas textureAtlas(16, 16);
    textureAtlas.PopulateFromBlockRegistry(blockRegistry, voxels::Paths::AssetsDir() / "textures");
    textureAtlas.BuildGLTexture();

    if (options.dumpAtlas) {
        std::filesystem::path dumpPath = options.dumpAtlasPath.empty()
            ? (voxels::Paths::LogsDir() / "atlas_dump.png")
            : std::filesystem::path(options.dumpAtlasPath);
        std::error_code ec;
        if (dumpPath.has_parent_path()) {
            std::filesystem::create_directories(dumpPath.parent_path(), ec);
        }
        if (textureAtlas.DumpAtlasToPng(dumpPath)) {
            std::cout << "Texture atlas successfully dumped to " << dumpPath.string()
                      << " (" << textureAtlas.GetLayerCount() << " layers).\n";
        } else {
            std::cerr << "Failed to dump texture atlas to " << dumpPath.string() << "\n";
        }
    }

    voxels::graphics::GLRenderer renderer;
    renderer.SetTextureAtlas(&textureAtlas);
    if (!renderer.Initialize()) {
        std::cerr << "Voxels GL renderer failed to initialize." << std::endl;
        engine.shutdown();
        return 1;
    }

    voxels::ImGuiUIManager uiManager;
    if (!uiManager.Initialize(platform, nullptr)) {
        std::cerr << "Voxels ImGui UI failed to initialize." << std::endl;
        renderer.Shutdown();
        engine.shutdown();
        return 1;
    }

    voxels::SetGlobalRenderer(&renderer);

    voxels::InputManager inputManager;
    voxels::PreferencesManager preferencesManager(voxels::Paths::UserDataDir() / "settings.json", platform->GetContext().type);
    voxels::GamePreferences preferences = preferencesManager.Load();
    RemoveUnversionedSaves();
    voxels::SaveManager saveManager(voxels::Paths::SavesDir() / kSaveFormatDirectory);
    bool running = true;
    voxels::AppStateMachine stateMachine;
    voxels::AppContext appContext{};
    appContext.platform = platform;
    appContext.renderer = &renderer;
    appContext.ui = &uiManager;
    appContext.input = &inputManager;
    appContext.blockRegistry = &blockRegistry;
    appContext.textureAtlas = &textureAtlas;
    appContext.saveManager = &saveManager;
    appContext.preferences = &preferences;
    appContext.requestTransition = [&stateMachine](std::unique_ptr<voxels::IAppState> state) { stateMachine.RequestTransition(std::move(state)); };
    appContext.requestPushOverlay = [&stateMachine](std::unique_ptr<voxels::IAppState> state) { stateMachine.RequestPushOverlay(std::move(state)); };
    appContext.requestPopOverlay = [&stateMachine]() { stateMachine.RequestPopOverlay(); };
    appContext.requestQuit = [&running]() { running = false; };
    stateMachine.Start(std::make_unique<voxels::MainMenuState>(&appContext));

    voxels::networking::GameServer localServer;
    voxels::networking::GameClient localClient;
    if (!localServer.Start("127.0.0.1", 0) || !localClient.Connect("127.0.0.1", localServer.Port())) {
        std::cerr << "Voxels local server failed to start." << std::endl;
        engine.shutdown();
        return 1;
    }

    WindowEventListener windowListener(running, renderer, inputManager, uiManager);
    if (platform != nullptr) {
        platform->RegisterEventListener(&uiManager, 1000);
        platform->RegisterEventListener(&windowListener, 100);
        const auto [drawableW, drawableH] = platform->GetDrawableSize();
        renderer.SetViewport(drawableW, drawableH);
    }

    voxels::FrameAccumulator frameAccumulator;
    auto lastTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    const int maxFrames = options.maxFrames > 0 ? options.maxFrames : std::numeric_limits<int>::max();

    while (running && frameCount < maxFrames) {
        if (platform != nullptr) {
            platform->PollEvents(nullptr);
        }

        const auto now = std::chrono::steady_clock::now();
        const double deltaSeconds = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        frameAccumulator.Accumulate(deltaSeconds);

        const int simTicks = frameAccumulator.Resolve(kFixedStepSeconds);
        for (int i = 0; i < simTicks; ++i) {
            localServer.Tick();
            localClient.Tick();
            stateMachine.Update(kFixedStepSeconds);
        }

        uiManager.BeginFrame();
        stateMachine.Render();
        voxels::UIDebugMetrics debugMetrics{};
        debugMetrics.frameMilliseconds = static_cast<float>(deltaSeconds * 1000.0);
        debugMetrics.framesPerSecond = deltaSeconds > 0.0 ? static_cast<float>(1.0 / deltaSeconds) : 0.0f;
        const auto& camera = renderer.GetCamera();
        debugMetrics.playerX = camera.position.x;
        debugMetrics.playerY = camera.position.y;
        debugMetrics.playerZ = camera.position.z;
        debugMetrics.chunkX = static_cast<int>(std::floor(camera.position.x / 16.0f));
        debugMetrics.chunkY = static_cast<int>(std::floor(camera.position.y / 16.0f));
        debugMetrics.chunkZ = static_cast<int>(std::floor(camera.position.z / 16.0f));
        if (const auto* inGame = dynamic_cast<const voxels::InGameState*>(stateMachine.GetCurrentState())) {
            if (const auto* chunkRenderer = inGame->GetChunkRenderer()) {
                const auto& metrics = chunkRenderer->GetMetrics();
                debugMetrics.loadedChunks = metrics.loadedChunks;
                debugMetrics.meshedChunks = metrics.meshedChunks;
                debugMetrics.visibleChunks = metrics.visibleChunks;
                debugMetrics.drawCalls = metrics.drawCalls;
                debugMetrics.triangles = metrics.triangles;
                debugMetrics.meshQueueDepth = metrics.meshQueueDepth;
            }
        }
        debugMetrics.glVendor = GetOpenGLString(GL_VENDOR);
        debugMetrics.glRenderer = GetOpenGLString(GL_RENDERER);
        debugMetrics.glVersion = GetOpenGLString(GL_VERSION);
        uiManager.SetDebugMetrics(std::move(debugMetrics));
        uiManager.EndFrame();
        renderer.EndFrame();
        if (platform != nullptr) {
            platform->SwapBuffers();
        }

        ++frameCount;
    }

    localClient.Disconnect();
    localServer.Stop();
    std::cout << "Voxels app loop exited after " << frameCount << " frames.\n";
    // Exit the active state (releasing any GPU resources it owns, e.g. InGameState's
    // ChunkRenderer) while the GL context is still alive, before the renderer/platform below
    // tear it down. Destroying stateMachine after engine.shutdown() would call GL delete
    // functions against an already-destroyed context.
    stateMachine.Shutdown();
    if (platform != nullptr) {
        platform->UnregisterEventListener(&uiManager);
    }
    uiManager.Shutdown();
    voxels::SetGlobalRenderer(nullptr);
    renderer.Shutdown();
    engine.shutdown();
    return 0;
}
