/**
 * @file save_manager.cpp
 * @brief Implementation of `SaveManager` directory layout and metadata (de)serialization.
 *
 * @details Metadata uses a minimal `key=value` line format (mirroring the flat-schema
 *          rationale used for `PreferencesManager`'s JSON) since a `GameSave` has no nested
 *          structures. Keeps the on-disk format trivial to read by hand while debugging saves.
 */

#include "voxels/app/save_manager.hpp"

#include <fstream>
#include <sstream>

namespace voxels {

namespace {
constexpr const char* kMetaFileName = "save.meta";
} // namespace

SaveManager::SaveManager(std::filesystem::path saveRootPath) : m_saveRoot(std::move(saveRootPath)) {
    std::error_code ec;
    std::filesystem::create_directories(m_saveRoot, ec);
}

std::filesystem::path SaveManager::GetSaveDirectory(const std::string& saveName) const {
    return m_saveRoot / saveName;
}

std::string SaveManager::ToMetaText(const GameSave& save) {
    std::ostringstream out;
    out << "saveName=" << save.saveName << '\n';
    out << "worldName=" << save.worldName << '\n';
    out << "playerName=" << save.playerName << '\n';
    out << "lastPlayedAt=" << save.lastPlayedAt << '\n';
    out << "seed=" << save.seed << '\n';
    out << "publicVisibility=" << (save.publicVisibility ? 1 : 0) << '\n';
    return out.str();
}

GameSave SaveManager::FromMetaText(const std::string& text) {
    GameSave save{};
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "saveName") save.saveName = value;
        else if (key == "worldName") save.worldName = value;
        else if (key == "playerName") save.playerName = value;
        else if (key == "lastPlayedAt") save.lastPlayedAt = value;
        else if (key == "seed") save.seed = static_cast<WorldSeed>(std::stoul(value));
        else if (key == "publicVisibility") save.publicVisibility = (value == "1");
    }
    return save;
}

bool SaveManager::Save(const GameSave& save) {
    if (save.saveName.empty()) {
        return false;
    }
    const std::filesystem::path dir = GetSaveDirectory(save.saveName);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }
    std::ofstream file(dir / kMetaFileName, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << ToMetaText(save);
    return static_cast<bool>(file);
}

bool SaveManager::Load(const std::string& saveName, GameSave& outSave) {
    const std::filesystem::path metaPath = GetSaveDirectory(saveName) / kMetaFileName;
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    outSave = FromMetaText(buffer.str());
    return true;
}

std::vector<SaveSlot> SaveManager::ListSaves() const {
    std::vector<SaveSlot> slots;
    std::error_code ec;
    if (!std::filesystem::exists(m_saveRoot, ec)) {
        return slots;
    }
    for (const auto& entry : std::filesystem::directory_iterator(m_saveRoot, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::filesystem::path metaPath = entry.path() / kMetaFileName;
        std::ifstream file(metaPath);
        if (!file.is_open()) {
            continue;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        SaveSlot slot{};
        slot.slotName = entry.path().filename().string();
        slot.save = FromMetaText(buffer.str());
        slots.push_back(std::move(slot));
    }
    return slots;
}

bool SaveManager::DeleteSave(const std::string& saveName) {
    std::error_code ec;
    const auto removed = std::filesystem::remove_all(GetSaveDirectory(saveName), ec);
    return !ec && removed > 0;
}

} // namespace voxels
