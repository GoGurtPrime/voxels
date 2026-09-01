/**
 * @file steam_platform_services.cpp
 * @brief `SteamPlatformServices`: real Steamworks-backed `IPlatformServices` implementation.
 *
 * @details Only compiled into `voxels_engine` when `VOXELS_ENABLE_STEAM` is on and the
 *          Steamworks SDK (`engine/third_party/steam`) is present (root CMakeLists.txt /
 *          engine/CMakeLists.txt). The default build never links or includes this file, so the
 *          shipping desktop configuration has zero Steam dependency (AGENT_RULES.md §2.3).
 *          Achievement/rich-presence/overlay state falls back to in-memory bookkeeping whenever
 *          `SteamAPI_Init()` has not succeeded, so calling any method is always safe even if the
 *          Steam client is not running.
 */

#if defined(VOXELS_ENABLE_STEAM)

#include "voxels/platform/platform_services.hpp"

#include <fstream>
#include <vector>

#include <steam/steam_api.h>

namespace voxels {

namespace {

/// Reads a file fully into memory; returns false (with `outBytes` untouched) if it cannot be
/// opened, which is the normal case for optional per-save files (e.g. no player.dat yet).
bool ReadFileBytes(const std::filesystem::path& path, std::vector<char>& outBytes) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return false;
    const std::streamsize size = stream.tellg();
    if (size < 0) return false;
    outBytes.resize(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (size > 0 && !stream.read(outBytes.data(), size)) return false;
    return true;
}

} // namespace

/// Mirrors the two small metadata files SaveManager always writes (ARCHITECTURE.md §6.5) into
/// Steam Cloud under a path scoped by save name, so two different worlds never collide. Region
/// files are intentionally not mirrored yet (they can be large; full-world cloud sync is a
/// follow-up once a chunked upload strategy is designed).
class SteamPlatformServices final : public IPlatformServices {
public:
    bool Initialize() override {
        SteamErrMsg errMsg{};
        m_initialized = SteamAPI_InitEx(&errMsg) == k_ESteamAPIInitResult_OK;
        if (!m_initialized) return false;
        // RequestCurrentStats() is intentionally not called: this SDK version documents stats
        // and achievements as synchronized by the Steam client before the game process begins.
        m_overlayCallback.Register(this, &SteamPlatformServices::OnOverlayActivated);
        return true;
    }

    ~SteamPlatformServices() override { m_overlayCallback.Unregister(); }

    void Shutdown() override {
        if (!m_initialized) return;
        SteamAPI_Shutdown();
        m_initialized = false;
    }

    void Update() override {
        if (m_initialized) SteamAPI_RunCallbacks();
    }

    [[nodiscard]] bool IsAvailable() const noexcept override { return m_initialized; }
    [[nodiscard]] std::string_view Name() const noexcept override { return "Steam"; }

    void UnlockAchievement(Achievement achievement) override {
        const std::size_t index = static_cast<std::size_t>(achievement);
        if (index >= kAchievementCount || m_unlocked[index]) return;
        if (!m_initialized) return;
        const std::string apiName{ToApiName(achievement)};
        if (SteamUserStats()->SetAchievement(apiName.c_str())) {
            SteamUserStats()->StoreStats();
            m_unlocked[index] = true;
        }
    }

    [[nodiscard]] bool IsAchievementUnlocked(Achievement achievement) const override {
        const std::size_t index = static_cast<std::size_t>(achievement);
        if (index >= kAchievementCount) return false;
        if (!m_initialized) return m_unlocked[index];
        bool achieved = false;
        const std::string apiName{ToApiName(achievement)};
        return SteamUserStats()->GetAchievement(apiName.c_str(), &achieved) && achieved;
    }

    void SetRichPresence(const std::string& key, const std::string& value) override {
        if (m_initialized) SteamFriends()->SetRichPresence(key.c_str(), value.c_str());
    }

    [[nodiscard]] bool IsOverlayActive() const noexcept override { return m_overlayActive; }

    void OnWorldSaved(const std::filesystem::path& saveDirectory) override { MirrorToCloud(saveDirectory); }
    void OnWorldLoaded(const std::filesystem::path& saveDirectory) override { MirrorToCloud(saveDirectory); }

private:
    static constexpr std::size_t kAchievementCount = 4;

    void MirrorToCloud(const std::filesystem::path& saveDirectory) {
        if (!m_initialized || !SteamRemoteStorage()->IsCloudEnabledForApp()) return;
        const std::string saveName = saveDirectory.filename().string();
        for (const char* fileName : {"level.json", "player.dat"}) {
            std::vector<char> bytes;
            if (!ReadFileBytes(saveDirectory / fileName, bytes)) continue;
            const std::string cloudPath = "saves/" + saveName + "/" + fileName;
            SteamRemoteStorage()->FileWrite(cloudPath.c_str(), bytes.data(), static_cast<int32>(bytes.size()));
        }
    }

    bool m_initialized = false;
    bool m_overlayActive = false;
    bool m_unlocked[kAchievementCount] = {};
#ifdef __DOXYGEN__
    /// Updates the overlay-active state from a Steamworks callback payload.
    void OnOverlayActivated(GameOverlayActivated_t* data);
#endif
    // STEAM_CALLBACK_MANUAL declares both `m_overlayCallback` (a CCallbackManual) and the
    // `OnOverlayActivated` handler method above; Register()/Unregister() are called explicitly
    // (in Initialize()/the destructor) rather than at construction time, since construction
    // happens before SteamAPI_Init() runs.
    STEAM_CALLBACK_MANUAL(SteamPlatformServices, OnOverlayActivated, GameOverlayActivated_t, m_overlayCallback);
};

// The Steamworks macro declares this callback inside the class.
void SteamPlatformServices::OnOverlayActivated(GameOverlayActivated_t* data) { m_overlayActive = data->m_bActive != 0; }

std::unique_ptr<IPlatformServices> CreatePlatformServices() {
    auto steam = std::make_unique<SteamPlatformServices>();
    if (steam->Initialize()) return steam;
    // No Steam client running / SDK not usable at runtime: degrade to the dependency-free
    // implementation rather than failing app startup (mirrors the SDLAudioDevice/NullAudioDevice
    // fallback already used for the audio device in app/src/main.cpp).
    return std::make_unique<NullPlatformServices>();
}

} // namespace voxels

#endif // VOXELS_ENABLE_STEAM
