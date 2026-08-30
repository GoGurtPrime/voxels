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
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/assets/texture_loader.hpp"
#include "voxels/audio/audio_engine.hpp"
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
#include "voxels/world/generation_pipeline.hpp"

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

voxels::AudioCategory AudioCategoryFromString(const std::string& category) {
    if (category == "music") return voxels::AudioCategory::Music;
    if (category == "ambience") return voxels::AudioCategory::Ambience;
    if (category == "ui") return voxels::AudioCategory::Ui;
    return voxels::AudioCategory::Sfx;
}

std::unordered_map<std::string, voxels::SoundHandle> LoadSoundBank(voxels::AudioEngine& audio,
                                                                     const std::filesystem::path& dataPath,
                                                                     const std::filesystem::path& audioRoot) {
    std::ifstream stream(dataPath);
    if (!stream) return {};
    nlohmann::json document;
    try { stream >> document; } catch (const nlohmann::json::parse_error&) { return {}; }
    std::unordered_map<std::string, voxels::SoundHandle> bank;
    for (const auto& [soundId, spec] : document.at("sounds").items()) {
        const std::filesystem::path clipPath = audioRoot / spec.at("file").get<std::string>();
        bank.emplace(soundId, audio.LoadSound(clipPath, AudioCategoryFromString(spec.value("category", "sfx"))));
    }
    return bank;
}

std::array<std::uint8_t, 3> PreviewColor(voxels::Biome biome) {
    switch (biome) {
        case voxels::Biome::Forest: return {48, 115, 52};
        case voxels::Biome::Hills: return {107, 145, 62};
        case voxels::Biome::Mountains: return {118, 120, 126};
        case voxels::Biome::Desert: return {207, 177, 87};
        case voxels::Biome::Beach: return {225, 207, 136};
        case voxels::Biome::Ocean: return {42, 112, 178};
        case voxels::Biome::Plains: return {91, 157, 70};
    }
    return {255, 0, 255};
}

bool WriteGenerationPreview(std::uint64_t seed, const std::filesystem::path& outputPath) {
    constexpr int kChunksPerSide = 8;
    constexpr int kPixelsPerChunk = 16;
    constexpr int kBlocksPerPixel = 4;
    constexpr int kSize = kChunksPerSide * kPixelsPerChunk;
    voxels::ImageData image;
    image.width = kSize;
    image.height = kSize;
    image.channels = 4;
    image.pixels.resize(static_cast<std::size_t>(kSize * kSize * 4));
    const voxels::WorldGenerator generator({.seed = seed});
    for (int z = 0; z < kSize; ++z) {
        for (int x = 0; x < kSize; ++x) {
            const voxels::TerrainColumn column = generator.SampleColumn((x - kSize / 2) * kBlocksPerPixel,
                                                                          (z - kSize / 2) * kBlocksPerPixel);
            const auto color = PreviewColor(column.biome);
            const float shade = 0.60f + static_cast<float>(column.surfaceY) / 295.0f;
            const std::size_t offset = static_cast<std::size_t>((z * kSize + x) * 4);
            image.pixels[offset] = static_cast<std::uint8_t>(static_cast<float>(color[0]) * shade);
            image.pixels[offset + 1] = static_cast<std::uint8_t>(static_cast<float>(color[1]) * shade);
            image.pixels[offset + 2] = static_cast<std::uint8_t>(static_cast<float>(color[2]) * shade);
            image.pixels[offset + 3] = 255;
        }
    }
    std::error_code error;
    if (outputPath.has_parent_path()) std::filesystem::create_directories(outputPath.parent_path(), error);
    return !error && voxels::TextureLoader::WritePngToFile(outputPath, image);
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const voxels::CliParser cliParser;
    const voxels::AppCommandLineOptions options = cliParser.Parse(args);

    if (options.genPreview) {
        const std::filesystem::path outputPath = options.genPreviewPath.empty()
            ? voxels::Paths::LogsDir() / "generation_preview.png"
            : std::filesystem::path(options.genPreviewPath);
        if (!WriteGenerationPreview(options.seedOverride ? options.seed : 0, outputPath)) {
            std::cerr << "Failed to write generation preview to " << outputPath.string() << ".\n";
            return 1;
        }
        std::cout << "Generation preview written to " << outputPath.string() << ".\n";
        return 0;
    }

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

    if (options.forgeAudioAssets) {
        const std::filesystem::path outputPath = options.forgeAudioAssetsPath.empty()
            ? voxels::Paths::AssetsDir() / "audio" : std::filesystem::path(options.forgeAudioAssetsPath);
        const std::size_t count = voxels::ForgeDefaultAudioAssets(outputPath, true);
        if (count != 28) {
            std::cerr << "Failed to forge all audio assets under " << outputPath.string() << ".\n";
            return 1;
        }
        std::cout << "Forged " << count << " audio assets under " << outputPath.string() << ".\n";
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
    std::unique_ptr<voxels::IAudioDevice> audioDevice = std::make_unique<voxels::SDLAudioDevice>();
    auto audio = std::make_unique<voxels::AudioEngine>(*audioDevice);
    std::string audioError;
    if (!audio->Initialize(audioError)) {
        audioDevice = std::make_unique<voxels::NullAudioDevice>();
        audio = std::make_unique<voxels::AudioEngine>(*audioDevice);
        audio->Initialize(audioError);
    }
    audio->ApplyVolumes(preferences.masterVolume, preferences.musicVolume, preferences.sfxVolume, 0.7f);
    const std::filesystem::path audioRoot = voxels::Paths::AssetsDir() / "audio";
    std::unordered_map<std::string, voxels::SoundHandle> soundBank =
        LoadSoundBank(*audio, voxels::Paths::AssetsDir() / "data" / "sounds.json", audioRoot);
    if (const auto menuMusic = soundBank.find("music/menu_theme"); menuMusic != soundBank.end()) {
        audio->PlayMusic({menuMusic->second.id}, true);
    }
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
    appContext.audio = audio.get();
    appContext.soundBank = std::move(soundBank);
    appContext.requestTransition = [&stateMachine](std::unique_ptr<voxels::IAppState> state) { stateMachine.RequestTransition(std::move(state)); };
    appContext.requestPushOverlay = [&stateMachine](std::unique_ptr<voxels::IAppState> state) { stateMachine.RequestPushOverlay(std::move(state)); };
    appContext.requestPopOverlay = [&stateMachine]() { stateMachine.RequestPopOverlay(); };
    appContext.requestQuit = [&running]() { running = false; };
    stateMachine.Start(std::make_unique<voxels::MainMenuState>(&appContext));
    if (!audio->IsAudible()) uiManager.ShowToast("Audio device unavailable; playing silently.");

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
    audio->Shutdown();
    voxels::SetGlobalRenderer(nullptr);
    renderer.Shutdown();
    engine.shutdown();
    return 0;
}
