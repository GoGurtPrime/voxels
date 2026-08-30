#pragma once

/*
 * Scope: Core domain types for the engine runtime.
 *
 * This header defines the base data model for player state, save data, world metadata,
 * preferences, and global runtime configuration. It must later be extended with actual
 * save serialization, world generation configuration, and platform-specific overrides.
 *
 * Relation to the rest of the codebase: all gameplay systems, world generation, app logic,
 * and networking rely on these types as the canonical representation of persistent state.
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace voxels {

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

struct Resolution {
    int width = 1280;
    int height = 720;
    int refreshRate = 60;

    [[nodiscard]] bool operator==(const Resolution&) const noexcept = default;
};

struct GamePreferences {
    WindowMode windowMode = WindowMode::Windowed;
    Resolution resolution;
    int renderDistance = 8;
    int simulationDistance = 4;
    float fieldOfView = 90.0f;
    float mouseSensitivity = 1.0f;
    bool invertY = false;
    int antiAliasingSamples = 4;
    ShadowQuality shadowQuality = ShadowQuality::Medium;
    float masterVolume = 1.0f;
    float musicVolume = 0.7f;
    float sfxVolume = 0.8f;
    bool particles = true;
    std::map<std::string, std::string> keyBindings;

    [[nodiscard]] bool operator==(const GamePreferences&) const noexcept = default;
};

struct PlayerSaveData {
    std::string playerName;
    std::uint64_t experience = 0;
    std::uint32_t health = 100;
    std::uint32_t hunger = 100;
    bool peacefulMode = false;
    bool permadeath = false;
};

struct GameSave {
    std::string saveName;
    std::string worldName;
    std::string playerName;
    std::string lastPlayedAt;
    WorldSeed seed = 0;
    bool publicVisibility = true;
};

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
};

} // namespace voxels
