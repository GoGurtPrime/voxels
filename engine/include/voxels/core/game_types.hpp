#pragma once

/**
 * @file game_types.hpp
 * @brief Core plain-data domain types: preferences, save metadata, and CLI options.
 *
 * @details Canonical representation of persistent state shared across gameplay, world
 *          generation, app logic, and networking. `GamePreferences` is persisted by
 *          `PreferencesManager` (settings.json), `GameSave` by `SaveManager`
 *          (per-save level.json), and `AppCommandLineOptions` is produced by `CliParser`
 *          at startup. Everything here is trivially copyable value data.
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "voxels/core/version.hpp"

namespace voxels {

/// Seed for deterministic world generation; identical seeds reproduce identical terrain.
using WorldSeed = std::uint32_t;

enum class WindowMode {
    Windowed,
    Borderless,
    Fullscreen
};

enum class ShadowQuality {
    Off,
    Low,
    Medium,
    High
};

/// Graphics API selected at process start. Automatic resolves to the platform default;
/// changing this preference takes effect on the next launch.
enum class RendererBackend {
    Automatic,
    OpenGL,
    Direct3D11
};

struct Resolution {
    int width = 1280;
    int height = 720;
    int refreshRate = 60; ///< Hz.

    [[nodiscard]] bool operator==(const Resolution&) const noexcept = default;
};

/// User-tunable settings, persisted as settings.json by `PreferencesManager`.
struct GamePreferences {
    WindowMode windowMode = WindowMode::Windowed;
    Resolution resolution;
    RendererBackend rendererBackend = RendererBackend::Automatic;
    int renderDistance = 8;          ///< In chunks, per horizontal axis.
    int simulationDistance = 4;      ///< In chunks; gameplay updates beyond this are skipped.
    float fieldOfView = 90.0f;       ///< Degrees.
    float mouseSensitivity = 1.0f;   ///< Multiplier on raw mouse deltas.
    bool invertY = false;
    int antiAliasingSamples = 4;
    ShadowQuality shadowQuality = ShadowQuality::Medium;
    float masterVolume = 1.0f;       ///< Volumes are 0..1 linear gains.
    float musicVolume = 0.7f;
    float sfxVolume = 0.8f;
    bool particles = true;           ///< Not serialized by PreferencesManager; resets each launch.
    float crosshairSize = 1.0f;      ///< Scalar applied to HUD crosshair rendering.
    bool highContrastCrosshair = false;
    bool reducedMotion = false;
    std::string heartColor = "#d94352"; ///< Validated opaque RGB hex color for the gameplay hearts.
    /// Persisted so the first-run controls card (work_items/18 §4) is shown exactly once.
    bool controlsCardSeen = false;
    std::map<std::string, std::string> keyBindings; ///< Action name -> key name.

    [[nodiscard]] bool operator==(const GamePreferences&) const noexcept = default;
};

/// Per-player progression snapshot carried in save data.
struct PlayerSaveData {
    std::string playerName;
    std::uint64_t experience = 0;
    std::uint32_t health = 100;
    std::uint32_t hunger = 100;
    bool peacefulMode = false;
    bool permadeath = false;
};

/// Per-world save metadata, round-tripped through `SaveManager::ToMetaText`/`FromMetaText`.
struct GameSave {
    std::string saveName;    ///< Doubles as the save directory name; must be filesystem-safe.
    std::string worldName;   ///< Display name shown in menus.
    std::string playerName;
    std::string createdUtc;
    std::string lastPlayedAt;
    WorldSeed seed = 0;
    std::uint32_t schemaVersion = 2;  ///< Loaders reject saves from newer schemas.
    std::uint64_t playTimeSeconds = 0;
    std::uint64_t worldTick = 0;      ///< Authoritative 60 Hz simulation tick persisted with the world.
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    float spawnZ = 0.0f;
    std::uint32_t generatorVersion = 1; ///< Guards against regenerating chunks with a mismatched generator.
    std::string engineVersion = kEngineVersion;
    bool peaceful = false;
    bool permadeath = false;
    bool alwaysSunny = true;
    bool sandboxMode = false;         ///< Instant block breaking, no inventory consumption.
    int renderDistanceChunks = 8;
    int simulationDistanceChunks = 4;
    bool publicVisibility = true;     ///< Gates whether non-loopback players may join when hosting.
};

/// Startup options parsed from argv. Each `*Override` flag records that the corresponding
/// CLI flag was present; the paired value is meaningful only when its flag is set.
struct AppCommandLineOptions {
    bool fullscreenOverride = false;
    bool fullscreenValue = true;
    bool resolutionOverride = false;
    int resolutionWidth = 1280;
    int resolutionHeight = 720;
    bool vsyncOverride = false;
    bool vsyncValue = true;
    bool headless = false;
    bool serverMode = false;
    bool serverPortOverride = false;
    std::uint16_t serverPort = 27015;
    bool joinEndpointOverride = false;
    std::string joinEndpoint;
    std::string configPath;
    std::string saveSlot;
    bool worldNameOverride = false;
    std::string worldName;
    bool seedOverride = false;
    WorldSeed seed = 0;
    bool renderDistanceOverride = false;
    int renderDistance = 8;
    bool maxTicksOverride = false;
    int maxTicks = 0;
    int maxFrames = 0;
    bool dumpAtlas = false;
    std::string dumpAtlasPath;
    bool forgeInteractionAssets = false;
    std::string forgeInteractionAssetsPath;
    bool forgeAudioAssets = false;
    std::string forgeAudioAssetsPath;
    bool genPreview = false;
    std::string genPreviewPath;
};

} // namespace voxels
