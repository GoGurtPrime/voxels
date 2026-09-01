#pragma once

/**
 * @file preferences.hpp
 * @brief Load/save contract and JSON-backed implementation for `GamePreferences`.
 *
 * @details Captures hardware-sensitive settings (window mode, rendering quality,
 *          simulation distance, controls) with platform-specific constraints applied on
 *          every load and save (e.g. the Dreamcast target is locked to fullscreen
 *          640x480). Consumed by the app configuration flow, the settings screen, and the
 *          input system.
 */

#include <filesystem>

#include "voxels/core/game_types.hpp"
#include "voxels/platform/platform.hpp"

namespace voxels {

/// Persistence contract for user settings.
class IPreferencesManager {
public:
    virtual ~IPreferencesManager() = default;
    /// Implementations must return usable defaults when no settings have been persisted yet.
    virtual GamePreferences Load() const = 0;
    virtual void Save(const GamePreferences& preferences) = 0;
};

/// Loads/saves `GamePreferences` as JSON on disk and enforces platform-specific constraints
/// (e.g. the Dreamcast target cannot toggle window mode and is locked to 640x480).
class PreferencesManager final : public IPreferencesManager {
public:
    explicit PreferencesManager(std::filesystem::path configPath,
                                 PlatformType platform = PlatformType::Windows);

    /// Missing file yields platform-constrained defaults; a malformed file throws
    /// std::runtime_error from the JSON parser.
    [[nodiscard]] GamePreferences Load() const override;
    /// Atomic write (temp file + rename) that preserves unrecognized keys already present
    /// in the file. Throws std::runtime_error on I/O failure.
    void Save(const GamePreferences& preferences) override;

    /// Returns a copy of `preferences` with any platform-mandated overrides applied.
    [[nodiscard]] static GamePreferences ApplyPlatformConstraints(GamePreferences preferences,
                                                                   PlatformType platform);

    /// Round-trips every `GamePreferences` field except `particles`, which is session-only.
    [[nodiscard]] static std::string ToJson(const GamePreferences& preferences);
    /// Missing keys keep their defaults; throws std::runtime_error on malformed JSON.
    [[nodiscard]] static GamePreferences FromJson(const std::string& json);

private:
    std::filesystem::path m_configPath;
    PlatformType m_platform;
};

} // namespace voxels
