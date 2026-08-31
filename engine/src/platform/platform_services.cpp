/**
 * @file platform_services.cpp
 * @brief `NullPlatformServices` implementation and backend selection factory.
 */

#include "voxels/platform/platform_services.hpp"

#include <algorithm>

namespace voxels {

std::string_view ToApiName(Achievement achievement) noexcept {
    switch (achievement) {
        case Achievement::FirstBlockBroken: return "ACH_FIRST_BLOCK_BROKEN";
        case Achievement::FirstWorldCreated: return "ACH_FIRST_WORLD_CREATED";
        case Achievement::FirstCaveEntered: return "ACH_FIRST_CAVE_ENTERED";
        case Achievement::FirstStructureBuilt: return "ACH_FIRST_STRUCTURE_BUILT";
    }
    return "ACH_UNKNOWN";
}

bool NullPlatformServices::Initialize() { return true; }
void NullPlatformServices::Shutdown() {}

void NullPlatformServices::UnlockAchievement(Achievement achievement) {
    m_unlocked[static_cast<std::size_t>(achievement)] = true;
}

bool NullPlatformServices::IsAchievementUnlocked(Achievement achievement) const {
    return m_unlocked[static_cast<std::size_t>(achievement)];
}

void NullPlatformServices::SetRichPresence(const std::string& key, const std::string& value) {
    for (std::size_t i = 0; i < m_richPresenceCount; ++i) {
        if (m_richPresenceKeys[i] == key) {
            m_richPresenceValues[i] = value;
            return;
        }
    }
    if (m_richPresenceCount < std::size(m_richPresenceKeys)) {
        m_richPresenceKeys[m_richPresenceCount] = key;
        m_richPresenceValues[m_richPresenceCount] = value;
        ++m_richPresenceCount;
    }
}

const std::string& NullPlatformServices::GetRichPresence(const std::string& key) const {
    static const std::string kEmpty;
    for (std::size_t i = 0; i < m_richPresenceCount; ++i) {
        if (m_richPresenceKeys[i] == key) return m_richPresenceValues[i];
    }
    return kEmpty;
}

#if !defined(VOXELS_ENABLE_STEAM)
std::unique_ptr<IPlatformServices> CreatePlatformServices() {
    return std::make_unique<NullPlatformServices>();
}
#endif

} // namespace voxels
