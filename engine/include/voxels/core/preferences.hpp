#pragma once

/*
 * Scope: Preference model for platform-aware settings.
 *
 * This abstraction captures hardware-sensitive settings such as window mode, rendering quality,
 * simulation distance, and controls, while allowing future platform-specific restrictions
 * such as Dreamcast display settings or console-only input mappings.
 *
 * Relation to the rest of the codebase: this state is consumed by the app configuration flow
 * and the input system.
 */

#include <filesystem>

#include "voxels/core/game_types.hpp"
#include "voxels/platform/platform.hpp"

namespace voxels {

class IPreferencesManager {
public:
    virtual ~IPreferencesManager() = default;
    virtual GamePreferences Load() const = 0;
    virtual void Save(const GamePreferences& preferences) = 0;
};

/// Loads/saves `GamePreferences` as JSON on disk and enforces platform-specific constraints
/// (e.g. the Dreamcast target cannot toggle window mode and is locked to 640x480).
class PreferencesManager final : public IPreferencesManager {
public:
    explicit PreferencesManager(std::filesystem::path configPath,
                                 PlatformType platform = PlatformType::Windows);

    [[nodiscard]] GamePreferences Load() const override;
    void Save(const GamePreferences& preferences) override;

    /// Returns a copy of `preferences` with any platform-mandated overrides applied.
    [[nodiscard]] static GamePreferences ApplyPlatformConstraints(GamePreferences preferences,
                                                                   PlatformType platform);

    [[nodiscard]] static std::string ToJson(const GamePreferences& preferences);
    [[nodiscard]] static GamePreferences FromJson(const std::string& json);

private:
    std::filesystem::path m_configPath;
    PlatformType m_platform;
};

} // namespace voxels
