#pragma once

/**
 * @file platform_services.hpp
 * @brief Optional platform-integration seam (achievements, rich presence, overlay, cloud saves).
 *
 * @details `IPlatformServices` sits alongside `IPlatform` in the hardware-abstraction layer
 *          (ARCHITECTURE.md module map): `NullPlatformServices` is the default, dependency-free
 *          implementation and `SteamPlatformServices` (compiled only when `VOXELS_ENABLE_STEAM`
 *          is on and the Steamworks SDK is present) backs the same interface with real Steam
 *          API calls. The desktop app always owns exactly one instance; nothing above this
 *          layer branches on which backend is active (work_items/18_packaging...).
 */

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace voxels {

/// The launch achievement set (work_items/18 §7). Each value maps 1:1 to a Steam achievement
/// API name of the same identifier (see steam_platform_services.cpp).
enum class Achievement : std::uint8_t {
    FirstBlockBroken,
    FirstWorldCreated,
    FirstCaveEntered,
    FirstStructureBuilt,
};

[[nodiscard]] std::string_view ToApiName(Achievement achievement) noexcept;

class IPlatformServices {
public:
    virtual ~IPlatformServices() = default;

    /// Establishes the backing service (e.g. `SteamAPI_Init`). Returns false on failure; callers
    /// must keep running with the instance regardless (achievements/overlay simply stay inert).
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    /// Pumps backend callbacks; must be called once per frame from the main thread.
    virtual void Update() = 0;

    [[nodiscard]] virtual bool IsAvailable() const noexcept = 0;
    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

    /// Unlocks `achievement` exactly once; safe to call repeatedly for the same achievement.
    virtual void UnlockAchievement(Achievement achievement) = 0;
    [[nodiscard]] virtual bool IsAchievementUnlocked(Achievement achievement) const = 0;

    /// Sets a rich-presence key/value pair (e.g. "status" -> "Exploring a cave").
    virtual void SetRichPresence(const std::string& key, const std::string& value) = 0;
    [[nodiscard]] virtual bool IsOverlayActive() const noexcept = 0;

    /// Hooks against the existing local save path (ARCHITECTURE.md §6.5): called after a world
    /// directory is written/read so a real backend can mirror the same files to cloud storage.
    virtual void OnWorldSaved(const std::filesystem::path& saveDirectory) = 0;
    virtual void OnWorldLoaded(const std::filesystem::path& saveDirectory) = 0;
};

/// Dependency-free default: tracks achievement/rich-presence state in memory so gameplay code
/// and tests behave identically whether or not Steam is compiled in, but touches no external
/// service. This is the implementation used whenever `VOXELS_ENABLE_STEAM` is off (always true
/// for the default build) and whenever the Steam backend fails to initialize at runtime.
class NullPlatformServices final : public IPlatformServices {
public:
    bool Initialize() override;
    void Shutdown() override;
    void Update() override {}

    [[nodiscard]] bool IsAvailable() const noexcept override { return false; }
    [[nodiscard]] std::string_view Name() const noexcept override { return "Null"; }

    void UnlockAchievement(Achievement achievement) override;
    [[nodiscard]] bool IsAchievementUnlocked(Achievement achievement) const override;

    void SetRichPresence(const std::string& key, const std::string& value) override;
    [[nodiscard]] bool IsOverlayActive() const noexcept override { return false; }
    [[nodiscard]] const std::string& GetRichPresence(const std::string& key) const;

    void OnWorldSaved(const std::filesystem::path&) override {}
    void OnWorldLoaded(const std::filesystem::path&) override {}

private:
    static constexpr std::size_t kAchievementCount = 4;
    bool m_unlocked[kAchievementCount] = {};
    std::string m_richPresenceKeys[8];
    std::string m_richPresenceValues[8];
    std::size_t m_richPresenceCount = 0;
};

/// Creates the platform services instance configured for this build: `SteamPlatformServices`
/// when `VOXELS_ENABLE_STEAM` was on at compile time and `Initialize()` succeeds at runtime,
/// falling back to `NullPlatformServices` otherwise (mirrors the SDLAudioDevice/NullAudioDevice
/// fallback pattern already used in app/src/main.cpp).
[[nodiscard]] std::unique_ptr<IPlatformServices> CreatePlatformServices();

} // namespace voxels
