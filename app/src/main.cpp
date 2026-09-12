/**
 * @file main.cpp
 * @brief `voxels_app` entry point: bootstrap, frame loop, and dedicated-server mode.
 *
 * @details Parses the command line, brings up the engine (platform/window, GL renderer,
 *          texture atlas, ImGui UI, input, audio, job system, platform services), drives the
 *          AppStateMachine (Boot → MainMenu → … → InGame) inside a frame-paced loop until the
 *          window closes, then tears everything down in reverse order — the state machine must
 *          shut down before the renderer so GPU-owning states release GL objects while the
 *          context is alive. `--server` instead runs the headless dedicated server loop.
 *          See DIAGRAMS.md (boot sequence / frame loop) and AGENT_RULES.md §2 (no headless
 *          fallback on the shipping path).
 */

#include <chrono>
#include <array>
#include <algorithm>
#include <charconv>
#include <csignal>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/display_settings.hpp"
#include "voxels/app/player_ui_dispatcher.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/assets/texture_loader.hpp"
#include "voxels/audio/audio_engine.hpp"
#include "voxels/core/job_system.hpp"
#include "voxels/core/logger.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/core/preferences.hpp"
#include "voxels/core/version.hpp"
#include "voxels/engine.hpp"
#if defined(_WIN32)
#include "voxels/graphics/dx11_renderer.hpp"
#endif
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/graphics/renderer.hpp"
#include "voxels/input/input_manager.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/platform/platform.hpp"
#include "voxels/platform/platform_services.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/render/texture_forge.hpp"
#include "voxels/ui/imgui_ui_manager.hpp"
#ifdef VOXELS_HAS_CEF
#include "voxels/ui/web_ui_manager.hpp"
#endif
#include "voxels/world/block.hpp"
#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/spawn_calculator.hpp"

namespace {

/// Mirrors the app-facing log banner to both stdout and `<userdata>/logs/` so the running
/// version/commit/build-time is recoverable from a bug report's log file alone (work_items/18).
voxels::Logger& BootLog() {
    static voxels::Logger logger;
    static const bool initialized = [] {
        logger.AddSink(std::make_shared<voxels::ConsoleLogSink>());
        logger.AddSink(std::make_shared<voxels::FileLogSink>(
            voxels::Paths::LogsDir() / ("voxels_" + std::to_string(std::time(nullptr)) + ".log")));
        return true;
    }();
    (void)initialized;
    return logger;
}

class WindowEventListener final : public voxels::IPlatformEventListener {
public:
    WindowEventListener(bool& runningFlag, voxels::IPlatform& platform, voxels::graphics::IGraphicsRenderer& renderer, voxels::InputManager& inputManager,
                        voxels::IPlayerUI& uiManager)
        : m_running(runningFlag), m_platform(platform), m_renderer(renderer), m_inputManager(inputManager), m_uiManager(uiManager) {}

    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        if (event.type == voxels::PlatformEventType::WindowClosed ||
            event.type == voxels::PlatformEventType::QuitRequested) {
            m_running = false;
        } else if (event.type == voxels::PlatformEventType::WindowResized) {
            const auto [drawableWidth, drawableHeight] = m_platform.GetDrawableSize();
            m_renderer.SetViewport(drawableWidth, drawableHeight);
        } else if (event.type == voxels::PlatformEventType::KeyDown) {
            if (event.keyCode == 1073741884U) {
                m_uiManager.ToggleDebugOverlay();
                return;
            }
            if (m_uiManager.CapturesKeyboard()) return;
            m_inputManager.InjectKeyEvent(static_cast<int>(event.keyCode), true);
        } else if (event.type == voxels::PlatformEventType::KeyUp) {
            if (m_uiManager.CapturesKeyboard()) return;
            m_inputManager.InjectKeyEvent(static_cast<int>(event.keyCode), false);
        } else if (event.type == voxels::PlatformEventType::MouseMotion) {
            if (m_uiManager.ConsumeTransitionMouseDelta() || m_uiManager.CapturesMouse()) return;
            m_inputManager.InjectMouseDelta(static_cast<float>(event.relativeX), static_cast<float>(event.relativeY));
        } else if (event.type == voxels::PlatformEventType::MouseButtonDown) {
            if (m_uiManager.CapturesMouse()) return;
            m_inputManager.InjectMouseButtonEvent(static_cast<int>(event.button), true);
        } else if (event.type == voxels::PlatformEventType::MouseButtonUp) {
            if (m_uiManager.CapturesMouse()) return;
            m_inputManager.InjectMouseButtonEvent(static_cast<int>(event.button), false);
        } else if (event.type == voxels::PlatformEventType::MouseWheel) {
            if (m_uiManager.CapturesMouse()) return;
            m_inputManager.InjectMouseWheel(event.wheelY);
        }
    }

private:
    bool& m_running;
    voxels::IPlatform& m_platform;
    voxels::graphics::IGraphicsRenderer& m_renderer;
    voxels::InputManager& m_inputManager;
    voxels::IPlayerUI& m_uiManager;
};

constexpr double kFixedStepSeconds = 1.0 / 60.0;
constexpr int kMaxSimulationStepsPerFrame = 4;
volatile std::sig_atomic_t g_serverRunning = 1;

void HandleServerInterrupt(int) {
    g_serverRunning = 0;
}

bool ParseNetworkEndpoint(const std::string& text, std::string& host, std::uint16_t& port) {
    const std::size_t separator = text.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator == text.size() - 1) return false;
    const std::string portText = text.substr(separator + 1);
    unsigned int parsedPort = 0;
    const auto result = std::from_chars(portText.data(), portText.data() + portText.size(), parsedPort);
    if (result.ec != std::errc{} || parsedPort == 0 || parsedPort > 65535) return false;
    host = text.substr(0, separator);
    port = static_cast<std::uint16_t>(parsedPort);
    return true;
}

void RemoveUnversionedSaves() {
    const std::filesystem::path savesRoot = voxels::Paths::SavesDir();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(savesRoot, error)) {
        if (error || !entry.is_directory() || entry.path().filename() == voxels::kCurrentSaveFormatDirectory) continue;
        std::filesystem::remove_all(entry.path(), error);
        if (error) return;
    }
}

std::string GetOpenGLString(GLenum name) {
    const auto* value = glGetString(name);
    return value == nullptr ? "Unavailable" : reinterpret_cast<const char*>(value);
}

constexpr voxels::PlatformType HostPlatformType() noexcept {
#if defined(_WIN32)
    return voxels::PlatformType::Windows;
#elif defined(__APPLE__)
    return voxels::PlatformType::MacOS;
#elif defined(__linux__)
    return voxels::PlatformType::Linux;
#else
    return voxels::PlatformType::Unknown;
#endif
}

voxels::Resolution ResolveWindowResolution(const voxels::GamePreferences& preferences,
                                           const voxels::AppCommandLineOptions& options) {
    voxels::Resolution resolved = preferences.resolution;
    if (options.resolutionOverride) {
        resolved.width = options.resolutionWidth;
        resolved.height = options.resolutionHeight;
    }
    resolved.width = std::clamp(resolved.width, 640, 7680);
    resolved.height = std::clamp(resolved.height, 360, 4320);
    return resolved;
}

voxels::WindowMode ResolveWindowMode(const voxels::GamePreferences& preferences,
                                     const voxels::AppCommandLineOptions& options) {
    if (options.fullscreenOverride) {
        return options.fullscreenValue ? voxels::WindowMode::Fullscreen : voxels::WindowMode::Windowed;
    }
    return preferences.windowMode;
}

voxels::AudioCategory AudioCategoryFromString(const std::string& category) {
    if (category == "music") return voxels::AudioCategory::Music;
    if (category == "ambience") return voxels::AudioCategory::Ambience;
    if (category == "ui") return voxels::AudioCategory::Ui;
    return voxels::AudioCategory::Sfx;
}

voxels::PlayerUIRoute PlayerUIRouteForState(const voxels::IAppState* state) {
    if (state == nullptr) return voxels::PlayerUIRoute::FatalError;
    switch (state->GetId()) {
        case voxels::AppStateId::MainMenu: return voxels::PlayerUIRoute::MainMenu;
        case voxels::AppStateId::WorldSelect: return voxels::PlayerUIRoute::SaveSelection;
        case voxels::AppStateId::WorldCreation: return voxels::PlayerUIRoute::WorldCreation;
        case voxels::AppStateId::LoadingScreen: case voxels::AppStateId::JoinLoading: return voxels::PlayerUIRoute::Loading;
        case voxels::AppStateId::JoinGame: return voxels::PlayerUIRoute::Join;
        case voxels::AppStateId::InGame: return voxels::PlayerUIRoute::Hud;
        case voxels::AppStateId::PauseMenu: return voxels::PlayerUIRoute::Pause;
        case voxels::AppStateId::Settings: return voxels::PlayerUIRoute::Settings;
        case voxels::AppStateId::ControlsCard: return voxels::PlayerUIRoute::ControlsCard;
        case voxels::AppStateId::Error: return voxels::PlayerUIRoute::Error;
        case voxels::AppStateId::Boot: return voxels::PlayerUIRoute::FatalError;
    }
    return voxels::PlayerUIRoute::FatalError;
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

struct MusicTrack {
    voxels::MusicHandle handle{};
    double durationSeconds = 0.0;
    std::filesystem::path path;
};

class MusicDirector {
public:
    MusicDirector(voxels::AudioEngine& audio,
                  std::unordered_map<std::string, voxels::SoundHandle>& soundBank,
                  std::filesystem::path audioRoot)
        : m_audio(audio), m_soundBank(soundBank), m_audioRoot(std::move(audioRoot)) {
        m_rng.seed(std::random_device{}());
    }

    void InitializeFromPreferences(const voxels::GamePreferences& preferences) {
        m_masterVolume = preferences.masterVolume;
        m_musicVolume = preferences.musicVolume;
        m_sfxVolume = preferences.sfxVolume;

        const auto menu = m_soundBank.find("music/menu_theme");
        if (menu != m_soundBank.end()) {
            m_menuTrack = {menu->second.id};
            m_menuVoice = m_audio.PlayMusic(m_menuTrack, true, 1.0f);
        }

        LoadGameplayPlaylist();
        ApplyGains();
    }

    void SyncPreferences(const voxels::GamePreferences& preferences) {
        m_masterVolume = preferences.masterVolume;
        m_musicVolume = preferences.musicVolume;
        m_sfxVolume = preferences.sfxVolume;
    }

    void Update(double deltaSeconds, const voxels::IAppState* baseState) {
        const bool gameplayActive = baseState != nullptr && baseState->GetId() == voxels::AppStateId::InGame;
        UpdateGameplayPlaylist(deltaSeconds, gameplayActive);
        ApplyGains();
    }

private:
    static constexpr float kContextFadeSeconds = 1.6f;
    static constexpr float kTrackFadeInSeconds = 1.2f;
    static constexpr float kTrackFadeOutSeconds = 1.4f;
    static constexpr float kTrackGapSeconds = 0.7f;

    void LoadGameplayPlaylist() {
        m_playlist.clear();
        const std::filesystem::path playlistDir = m_audioRoot / "playlist_ingame";
        std::error_code error;
        std::filesystem::create_directories(playlistDir, error);
        if (error) return;

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(playlistDir, error)) {
            if (error || !entry.is_regular_file()) continue;
            const std::string ext = entry.path().extension().string();
            if (ext == ".wav" || ext == ".WAV") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        for (const auto& file : files) {
            // Keep startup limited to directory metadata. PCM/ADPCM expansion happens only
            // when a gameplay track is actually selected, after the first frame is visible.
            m_playlist.push_back({{}, 0.0, file});
        }
    }

    std::optional<std::size_t> PickNextTrackIndex() {
        if (m_playlist.empty()) return std::nullopt;
        if (m_playlist.size() == 1) return std::size_t{0};

        std::uniform_int_distribution<std::size_t> distribution(0, m_playlist.size() - 1U);
        for (int attempt = 0; attempt < 12; ++attempt) {
            const std::size_t index = distribution(m_rng);
            if (static_cast<int>(index) != m_lastTrackIndex) return index;
        }
        return static_cast<std::size_t>((m_lastTrackIndex + 1) % static_cast<int>(m_playlist.size()));
    }

    void StartNextGameplayTrack() {
        const auto next = PickNextTrackIndex();
        if (!next.has_value()) {
            m_currentTrackIndex = -1;
            m_trackState = TrackState::Idle;
            return;
        }

        m_currentTrackIndex = static_cast<int>(*next);
        m_lastTrackIndex = m_currentTrackIndex;
        MusicTrack& track = m_playlist[*next];
        track.handle = m_audio.LoadMusic(track.path, voxels::AudioCategory::Music);
        if (!track.handle.IsValid()) {
            m_trackState = TrackState::Gap;
            m_trackGapRemaining = kTrackGapSeconds;
            return;
        }
        if (const voxels::PcmBuffer* clip = m_audio.GetMixer().GetClip({track.handle.id}); clip != nullptr &&
            clip->channels > 0 && clip->sampleRate > 0) {
            const std::size_t frames = clip->samples.size() / clip->channels;
            track.durationSeconds = static_cast<double>(frames) / static_cast<double>(clip->sampleRate);
        }
        m_trackElapsedSeconds = 0.0;
        m_currentVoice = m_audio.PlayMusic(track.handle, false, 0.0f);
        m_audio.GetMixer().SetVoiceEnvelope(m_currentVoice, 1.0f, kTrackFadeInSeconds);
        m_trackState = TrackState::FadingIn;
    }

    void UpdateGameplayPlaylist(double deltaSeconds, bool gameplayActive) {
        if (!gameplayActive) {
            if (m_gameplayActive) {
                m_audio.GetMixer().StopVoice(m_currentVoice, kContextFadeSeconds);
                m_audio.GetMixer().SetVoiceEnvelope(m_menuVoice, 1.0f, kContextFadeSeconds);
            }
            m_gameplayActive = false;
            m_trackState = TrackState::Idle;
            m_trackGapRemaining = 0.0;
            return;
        }

        if (!m_gameplayActive) {
            m_gameplayActive = true;
            m_audio.GetMixer().StopVoice(m_menuVoice, kContextFadeSeconds);
        }

        if (m_trackState == TrackState::Gap) {
            m_trackGapRemaining = std::max(0.0, m_trackGapRemaining - deltaSeconds);
            if (m_trackGapRemaining > 0.0) return;
            m_trackState = TrackState::Idle;
        }
        if (m_trackState == TrackState::Idle) {
            StartNextGameplayTrack();
        }

        if (m_trackState == TrackState::Gap || m_currentTrackIndex < 0 ||
            m_currentTrackIndex >= static_cast<int>(m_playlist.size())) {
            return;
        }

        const MusicTrack& track = m_playlist[static_cast<std::size_t>(m_currentTrackIndex)];
        m_trackElapsedSeconds += deltaSeconds;

        if (m_trackState == TrackState::FadingIn && m_trackElapsedSeconds >= kTrackFadeInSeconds) {
            m_trackState = TrackState::Playing;
        }
        if (m_trackState == TrackState::Playing && track.durationSeconds > 0.0) {
            const double remaining = track.durationSeconds - m_trackElapsedSeconds;
            if (remaining <= kTrackFadeOutSeconds) {
                if (remaining > 0.0) m_audio.GetMixer().SetVoiceEnvelope(m_currentVoice, 0.0f, static_cast<float>(remaining));
                m_trackState = TrackState::FadingOut;
            }
        }
        if (m_trackState == TrackState::FadingOut && m_trackElapsedSeconds >= track.durationSeconds) {
            m_trackState = TrackState::Gap;
            m_trackGapRemaining = kTrackGapSeconds;
        }
    }

    void ApplyGains() {
        m_audio.ApplyVolumes(m_masterVolume, m_musicVolume, m_sfxVolume, 0.7f);
    }

    voxels::AudioEngine& m_audio;
    std::unordered_map<std::string, voxels::SoundHandle>& m_soundBank;
    std::filesystem::path m_audioRoot;

    enum class TrackState : std::uint8_t { Idle, FadingIn, Playing, FadingOut, Gap };
    voxels::MusicHandle m_menuTrack{};
    voxels::VoiceHandle m_menuVoice{};
    voxels::VoiceHandle m_currentVoice{};
    std::vector<MusicTrack> m_playlist;
    std::mt19937_64 m_rng{};
    int m_lastTrackIndex = -1;
    int m_currentTrackIndex = -1;
    TrackState m_trackState = TrackState::Idle;
    bool m_gameplayActive = false;
    double m_trackElapsedSeconds = 0.0;
    double m_trackGapRemaining = 0.0;

    float m_masterVolume = 1.0f;
    float m_musicVolume = 1.0f;
    float m_sfxVolume = 1.0f;
};

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
#ifdef VOXELS_HAS_CEF
    // CEF forks this same executable for its renderer/GPU/utility subprocesses. Every one of
    // them must return here before any window/engine setup runs; the browser process falls
    // through with a negative result and continues into the normal desktop bootstrap below.
    if (const int subprocessExitCode = voxels::WebUIManager::ExecuteSubprocess(argc, argv); subprocessExitCode >= 0) {
        return subprocessExitCode;
    }
#endif
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
        if (!server.Start("0.0.0.0", options.serverPort)) {
            std::cerr << "Voxels server failed to start." << std::endl;
            return 1;
        }
        // Generate a joinable spawn area so remote clients receive real terrain.
        voxels::WorldOptions worldOptions{};
        worldOptions.seed = options.seedOverride ? options.seed : std::random_device{}();
        voxels::World& world = server.GetWorld();
        world.Initialize(worldOptions);
        voxels::WorldGenerator generator(worldOptions);
        for (int cz = -2; cz <= 2; ++cz) {
            for (int cx = -2; cx <= 2; ++cx) {
                for (int cy = 0; cy < 8; ++cy) {
                    world.GetOrCreateChunk({cx, cy, cz}) = generator.GenerateChunk({cx, cy, cz});
                }
                world.GetOrCreateChunk({cx, 8, cz});
            }
        }
        const voxels::Vec3I spawnBlock = voxels::FindSafeSpawn(world);
        const voxels::Vec3 spawn{static_cast<float>(spawnBlock.x) + 0.5f, static_cast<float>(spawnBlock.y) + 1.9f,
                                 static_cast<float>(spawnBlock.z) + 0.5f};
        server.SetWorldReady(worldOptions, spawn);
        std::cout << "Voxels server listening on port " << server.Port() << " (seed " << worldOptions.seed
                  << ", spawn " << spawn.x << ' ' << spawn.y << ' ' << spawn.z << ")." << std::endl;
        std::signal(SIGINT, HandleServerInterrupt);
        int tick = 0;
        while (g_serverRunning != 0 && (!options.maxTicksOverride || tick < options.maxTicks)) {
            server.Tick();
            ++tick;
            std::this_thread::sleep_for(voxels::networking::GameServer::kTickInterval);
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

    const std::filesystem::path settingsPath = voxels::Paths::UserDataDir() / "settings.json";
    const bool firstRun = !std::filesystem::exists(settingsPath);
    voxels::PreferencesManager preferencesManager(settingsPath, HostPlatformType());
    voxels::GamePreferences preferences = preferencesManager.Load();
    const voxels::Resolution runtimeResolution = ResolveWindowResolution(preferences, options);
    const voxels::WindowMode runtimeWindowMode = ResolveWindowMode(preferences, options);
    const voxels::RendererBackend activeBackend =
        voxels::PreferencesManager::ResolveRendererBackend(preferences.rendererBackend, HostPlatformType());

    voxels::WindowConfig windowConfig{};
    windowConfig.title = "Voxels Engine";
    windowConfig.width = runtimeResolution.width;
    windowConfig.height = runtimeResolution.height;
    windowConfig.fullscreen = false;
    windowConfig.resizable = false;
    windowConfig.graphicsApi = activeBackend == voxels::RendererBackend::OpenGL
                                   ? voxels::WindowGraphicsApi::OpenGL
                                   : voxels::WindowGraphicsApi::Native;

    voxels::Engine engine;
    if (!engine.initialize(false, windowConfig)) {
        std::cerr << "Voxels engine failed to initialize." << std::endl;
        return 1;
    }

    BootLog().Info("VoxelsEngine v" + std::string(voxels::kEngineVersion) + " (" + voxels::kEngineGitCommit +
                   ") built " + voxels::kEngineBuildTimestamp);

    const std::string windowTitle = "Voxels Engine v" + std::string(voxels::kEngineVersion) + " (" + voxels::kEngineGitCommit + ")";
    auto* platform = engine.getPlatform();
    if (platform != nullptr) {
        platform->SetWindowTitle(windowTitle);
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

    std::unique_ptr<voxels::graphics::IGraphicsRenderer> renderer;
#if defined(_WIN32)
    if (activeBackend == voxels::RendererBackend::Direct3D11) {
        renderer = std::make_unique<voxels::graphics::DX11Renderer>();
    }
#endif
    if (!renderer) renderer = std::make_unique<voxels::graphics::GLRenderer>();
    const bool vSync = options.vsyncOverride ? options.vsyncValue : true;
    if (!renderer->Initialize(*platform, textureAtlas, vSync)) {
        std::cerr << "Voxels " << renderer->GetName() << " renderer failed to initialize." << std::endl;
        engine.shutdown();
        return 1;
    }
    BootLog().Info("Active renderer: " + std::string(renderer->GetName()));

    voxels::ImGuiUIManager uiManager;
    if (!uiManager.Initialize(platform, renderer.get())) {
        std::cerr << "Voxels ImGui UI failed to initialize." << std::endl;
        renderer->Shutdown();
        engine.shutdown();
        return 1;
    }

    voxels::IPlayerUI* playerUi = &uiManager;
#ifdef VOXELS_HAS_CEF
    voxels::WebUIManager webUi;
    if (!webUi.Initialize(platform, renderer.get())) {
        std::cerr << "Voxels web UI failed to initialize." << std::endl;
        uiManager.Shutdown();
        renderer->Shutdown();
        engine.shutdown();
        return 1;
    }
    playerUi = &webUi;
#endif

    voxels::SetGlobalRenderer(renderer.get());

    voxels::InputManager inputManager;
    if (firstRun) {
        preferencesManager.Save(preferences);
        BootLog().Info("First run detected: wrote default settings to " + settingsPath.string());
    }
    std::unique_ptr<voxels::IPlatformServices> platformServices = voxels::CreatePlatformServices();
    platformServices->Initialize();
    std::unique_ptr<voxels::IAudioDevice> audioDevice = std::make_unique<voxels::SDLAudioDevice>();
    auto audio = std::make_unique<voxels::AudioEngine>(*audioDevice);
    std::string audioError;
    if (!audio->Initialize(audioError)) {
        BootLog().Warn("SDL audio output unavailable: " + audioError + ". Falling back to silent null device.");
        audioDevice = std::make_unique<voxels::NullAudioDevice>();
        audio = std::make_unique<voxels::AudioEngine>(*audioDevice);
        audio->Initialize(audioError);
    }
    audio->ApplyVolumes(preferences.masterVolume, preferences.musicVolume, preferences.sfxVolume, 0.7f);
    const std::filesystem::path audioRoot = voxels::Paths::AssetsDir() / "audio";
    std::unordered_map<std::string, voxels::SoundHandle> soundBank =
        LoadSoundBank(*audio, voxels::Paths::AssetsDir() / "data" / "sounds.json", audioRoot);
    if (const auto menuMusic = soundBank.find("music/menu_theme"); menuMusic != soundBank.end()) {
        if (const voxels::PcmBuffer* clip = audio->GetMixer().GetClip(menuMusic->second); clip != nullptr) {
            const std::size_t frames = clip->channels == 0 ? 0 : clip->samples.size() / clip->channels;
            const double seconds = clip->sampleRate == 0 ? 0.0
                                                          : static_cast<double>(frames) / static_cast<double>(clip->sampleRate);
            BootLog().Info("Menu music loaded (" + std::to_string(seconds) + "s, " +
                           std::to_string(clip->sampleRate) + " Hz, channels=" +
                           std::to_string(clip->channels) + ")");
        }
    } else {
        BootLog().Warn("Menu music sound id 'music/menu_theme' is missing from sounds.json.");
    }
    RemoveUnversionedSaves();
    voxels::SaveManager saveManager(voxels::Paths::SavesDir() / voxels::kCurrentSaveFormatDirectory);
    bool running = true;
    voxels::AppStateMachine stateMachine;
    voxels::AppContext appContext{};
    appContext.platform = platform;
    appContext.renderer = renderer.get();
    appContext.ui = playerUi;
    appContext.input = &inputManager;
    appContext.blockRegistry = &blockRegistry;
    appContext.textureAtlas = &textureAtlas;
    appContext.saveManager = &saveManager;
    appContext.preferences = &preferences;
    appContext.audio = audio.get();
    appContext.platformServices = platformServices.get();
    appContext.firstRun = firstRun;
    appContext.soundBank = std::move(soundBank);
    appContext.requestTransition = [&stateMachine](std::unique_ptr<voxels::IAppState> state) { stateMachine.RequestTransition(std::move(state)); };
    appContext.requestPushOverlay = [&stateMachine](std::unique_ptr<voxels::IAppState> state) { stateMachine.RequestPushOverlay(std::move(state)); };
    appContext.requestPopOverlay = [&stateMachine]() { stateMachine.RequestPopOverlay(); };
    appContext.requestQuit = [&running]() { running = false; };
    stateMachine.Start(std::make_unique<voxels::MainMenuState>(&appContext));
    if (!audio->IsAudible()) playerUi->ShowToast("Audio device unavailable; playing silently.");

    std::unique_ptr<voxels::networking::GameServer> localServer;
    voxels::networking::GameClient localClient;
    // The in-process server binds all interfaces so a LAN or second-instance peer can join;
    // non-loopback joiners are still gated server-side on world visibility. If the preferred
    // port is taken (a second instance on this machine), fall back to an ephemeral port.
    localServer = std::make_unique<voxels::networking::GameServer>();
    if (!localServer->Start("0.0.0.0", options.serverPort) && !localServer->Start("0.0.0.0", 0)) {
        std::cerr << "Voxels local server failed to start." << std::endl;
        uiManager.ShowToast("Local server could not bind a UDP port.");
        stateMachine.Shutdown();
        uiManager.Shutdown();
#ifdef VOXELS_HAS_CEF
        webUi.Shutdown();
#endif
        renderer->Shutdown();
        engine.shutdown();
        return 1;
    }
    const std::uint16_t localPort = localServer->Port();
    const auto connectToLocalServer = [&localClient, localPort]() {
        return localClient.Connect("127.0.0.1", localPort, voxels::networking::ClientKind::InProcessHost);
    };
    bool initialRemoteJoin = false;
    std::string joinHost;
    std::uint16_t joinPort = 0;
    if (options.joinEndpointOverride) {
        if (!ParseNetworkEndpoint(options.joinEndpoint, joinHost, joinPort)) {
            std::cerr << "Invalid --join endpoint; expected HOST:PORT." << std::endl;
            uiManager.ShowToast("Join address must use HOST:PORT.");
            stateMachine.Shutdown();
            uiManager.Shutdown();
#ifdef VOXELS_HAS_CEF
            webUi.Shutdown();
#endif
            renderer->Shutdown();
            engine.shutdown();
            return 1;
        }
        initialRemoteJoin = localClient.Connect(joinHost, joinPort, voxels::networking::ClientKind::Remote);
    }
    if (!initialRemoteJoin && !connectToLocalServer()) {
        std::cerr << "Voxels client failed to connect to the local server." << std::endl;
        uiManager.ShowToast("Unable to open the game connection.");
        engine.shutdown();
        return 1;
    }
    appContext.networkClient = &localClient;
    appContext.networkServer = localServer.get();
    appContext.connectRemote = [&localClient](const std::string& host, std::uint16_t port) {
        localClient.Disconnect();
        return localClient.Connect(host, port, voxels::networking::ClientKind::Remote);
    };
    appContext.resetNetworkToLocal = [&localClient, connectToLocalServer]() {
        localClient.Disconnect();
        if (!connectToLocalServer()) {
            std::cerr << "Voxels client failed to reconnect to the local server." << std::endl;
        }
    };
    if (initialRemoteJoin) {
        stateMachine.TransitionTo(std::make_unique<voxels::JoinLoadingState>(
            &appContext, joinHost + ":" + std::to_string(joinPort)));
    }

    WindowEventListener windowListener(running, *platform, *renderer, inputManager, *playerUi);
    if (platform != nullptr) {
        platform->RegisterEventListener(&uiManager, 1000);
#ifdef VOXELS_HAS_CEF
        platform->RegisterEventListener(&webUi, 1100);
#endif
        platform->RegisterEventListener(&windowListener, 100);
        const auto [drawableW, drawableH] = platform->GetDrawableSize();
        renderer->SetViewport(drawableW, drawableH);
    }

    voxels::FrameAccumulator frameAccumulator;
    const voxels::PlayerUIActionDispatcher playerUIActionDispatcher;
    MusicDirector musicDirector(*audio, appContext.soundBank, audioRoot);
    musicDirector.InitializeFromPreferences(preferences);
    auto lastTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    bool initialDisplayTransitionPending = runtimeWindowMode != voxels::WindowMode::Windowed;
    bool displayTransitionFailed = false;
    const int maxFrames = options.maxFrames > 0 ? options.maxFrames : std::numeric_limits<int>::max();

    while (running && frameCount < maxFrames) {
        if (platform != nullptr) {
            platform->PollEvents(nullptr);
        }
        if (const auto action = playerUi->ConsumeAction(); action.has_value()) {
            if (!playerUIActionDispatcher.Dispatch(PlayerUIRouteForState(stateMachine.GetVisibleState()), *action, appContext)) {
                std::cerr << "Web UI rejected action for the active route.\n";
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const double deltaSeconds = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        musicDirector.SyncPreferences(preferences);
        musicDirector.Update(deltaSeconds, stateMachine.GetCurrentState());
        frameAccumulator.Accumulate(deltaSeconds);

        const int simTicks = frameAccumulator.Resolve(kFixedStepSeconds, kMaxSimulationStepsPerFrame);
        for (int i = 0; i < simTicks; ++i) {
            if (localServer != nullptr) {
                const auto* inGame = dynamic_cast<const voxels::InGameState*>(stateMachine.GetCurrentState());
                localServer->Tick(inGame != nullptr);
            }
            localClient.Tick();
            stateMachine.Update(kFixedStepSeconds);
        }

        voxels::UpdateMainMenuBackdrop(deltaSeconds);

        platformServices->Update();

#ifdef VOXELS_HAS_CEF
    webUi.BeginFrame();
#endif
    std::array<float, 4> clearColor{0.12f, 0.16f, 0.19f, 1.0f};
    if (voxels::IsMainMenuBackdropActive()) {
        // Brighter atmospheric clear to avoid a flat dark horizon behind partially-loaded chunks.
        clearColor = {0.57f, 0.70f, 0.86f, 1.0f};
    }
    static_cast<void>(renderer->BeginFrame(clearColor));
        voxels::RenderMainMenuBackdrop();
        uiManager.BeginFrame();
        stateMachine.Render();
        voxels::UIDebugMetrics debugMetrics{};
        debugMetrics.frameMilliseconds = static_cast<float>(deltaSeconds * 1000.0);
        debugMetrics.framesPerSecond = deltaSeconds > 0.0 ? static_cast<float>(1.0 / deltaSeconds) : 0.0f;
        const auto& camera = renderer->GetCamera();
        debugMetrics.playerX = camera.position.x;
        debugMetrics.playerY = camera.position.y;
        debugMetrics.playerZ = camera.position.z;
        debugMetrics.chunkX = static_cast<int>(std::floor(camera.position.x / 16.0f));
        debugMetrics.chunkY = static_cast<int>(std::floor(camera.position.y / 16.0f));
        debugMetrics.chunkZ = static_cast<int>(std::floor(camera.position.z / 16.0f));
        if (const auto* inGame = dynamic_cast<const voxels::InGameState*>(stateMachine.GetCurrentState())) {
            debugMetrics.worldTick = inGame->GetWorldTick();
            debugMetrics.worldDay = voxels::WorldDayIndex(debugMetrics.worldTick);
            debugMetrics.worldDayFraction = voxels::WorldDayFraction(debugMetrics.worldTick);
            const auto& dropMetrics = inGame->GetItemDropMetrics();
            debugMetrics.activeItemDrops = dropMetrics.activeDrops;
            debugMetrics.itemDropCollisionQueries = dropMetrics.collisionQueries;
            debugMetrics.recoveredItemDrops = dropMetrics.recoveredDrops;
            debugMetrics.despawnedItemDrops = dropMetrics.despawnedDrops;
            debugMetrics.itemDropUpdateMicroseconds = dropMetrics.updateMicroseconds;
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
        if (renderer->GetBackend() == voxels::RendererBackend::OpenGL) {
            debugMetrics.glVendor = GetOpenGLString(GL_VENDOR);
            debugMetrics.glRenderer = GetOpenGLString(GL_RENDERER);
            debugMetrics.glVersion = GetOpenGLString(GL_VERSION);
        } else {
            debugMetrics.glVendor = "Microsoft / hardware adapter";
            debugMetrics.glRenderer = std::string(renderer->GetName());
            debugMetrics.glVersion = "Feature Level 11";
        }
        uiManager.SetDebugMetrics(std::move(debugMetrics));
        uiManager.EndFrame();
#ifdef VOXELS_HAS_CEF
        // Composited last so web-owned menu routes appear above the active render frame.
        webUi.EndFrame();
#endif
        static_cast<void>(renderer->EndFrame());
        static_cast<void>(renderer->Present());

        // Establish the GL/CEF presentation path with one windowed frame before entering
        // fullscreen. Windows otherwise applies a different initial colour state until the
        // player manually cycles modes; using the normal live transition also keeps SDL's
        // native window, drawable, and input bounds synchronized.
        if (initialDisplayTransitionPending) {
            initialDisplayTransitionPending = false;
            if (!voxels::ApplyWindowPreferences(*platform, runtimeWindowMode, runtimeResolution)) {
                std::cerr << "Voxels could not apply the requested display mode." << std::endl;
                displayTransitionFailed = true;
                running = false;
            }
        }

        ++frameCount;
    }

    localClient.Disconnect();
    if (localServer != nullptr) localServer->Stop();
    std::cout << "Voxels app loop exited after " << frameCount << " frames.\n";
    // Exit the active state (releasing any GPU resources it owns, e.g. InGameState's
    // ChunkRenderer) while the GL context is still alive, before the renderer/platform below
    // tear it down. Destroying stateMachine after engine.shutdown() would call GL delete
    // functions against an already-destroyed context.
    stateMachine.Shutdown();
    if (platform != nullptr) {
        platform->UnregisterEventListener(&uiManager);
        platform->UnregisterEventListener(&windowListener);
#ifdef VOXELS_HAS_CEF
        platform->UnregisterEventListener(&webUi);
#endif
    }
    uiManager.Shutdown();
#ifdef VOXELS_HAS_CEF
    // Release CEF's browser/renderer processes before the GL context and platform are torn
    // down below; releasing it after renderer->Shutdown() hangs or crashes on exit.
    webUi.Shutdown();
#endif
    audio->Shutdown();
    platformServices->Shutdown();
    voxels::StopMainMenuBackdrop();
    voxels::SetGlobalRenderer(nullptr);
    renderer->Shutdown();
    engine.shutdown();
    return displayTransitionFailed ? 1 : 0;
}
