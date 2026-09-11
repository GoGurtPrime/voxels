/**
 * @file save_manager.cpp
 * @brief Implementation of `SaveManager` directory layout and metadata (de)serialization.
 *
 * @details Metadata is written to `level.json` (kMetaFileName) as a flat, hand-rolled JSON
 *          object — scalar fields only, no JSON library — and read back by tolerant substring
 *          scanning on `"key":` markers. Writes stage to a temp file and atomically replace
 *          the target so a crash cannot truncate existing metadata. Player and world state
 *          delegate to the binary persistence helpers rather than this metadata path.
 */

#include "voxels/app/save_manager.hpp"

#include <fstream>
#include <sstream>

#include "voxels/world/world_clock.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace voxels {

namespace {
constexpr const char* kMetaFileName = "level.json";

bool IsSafeFolderName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("\\/:*?\"<>|") == std::string::npos;
}

bool AtomicReplace(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
}

bool AtomicWriteText(const std::filesystem::path& target, const std::string& text) {
    const std::filesystem::path temporary = target.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return false;
    output << text;
    output.flush();
    if (!output) return false;
    output.close();
    return AtomicReplace(temporary, target);
}

std::string JsonString(const std::string& value) { return "\"" + value + "\""; }

std::string ReadValue(const std::string& text, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    const std::size_t begin = text.find(marker);
    if (begin == std::string::npos) return {};
    std::size_t cursor = begin + marker.size();
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\"')) ++cursor;
    const std::size_t end = text.find_first_of(",}\n\"", cursor);
    return end == std::string::npos ? text.substr(cursor) : text.substr(cursor, end - cursor);
}
} // namespace

SaveManager::SaveManager(std::filesystem::path saveRootPath) : m_saveRoot(std::move(saveRootPath)) {
    std::error_code ec;
    std::filesystem::create_directories(m_saveRoot, ec);
}

std::filesystem::path SaveManager::GetSaveDirectory(const std::string& saveName) const {
    return IsSafeFolderName(saveName) ? m_saveRoot / saveName : std::filesystem::path{};
}

std::filesystem::path SaveManager::GetWorldPreviewPath(const std::string& saveName) const {
    const std::filesystem::path saveDirectory = GetSaveDirectory(saveName);
    return saveDirectory.empty() ? std::filesystem::path{} : saveDirectory / "preview.png";
}

bool SaveManager::SaveWorldPreview(
    const std::string& saveName,
    const std::function<bool(const std::filesystem::path&)>& writePreview) const {
    const std::filesystem::path target = GetWorldPreviewPath(saveName);
    if (target.empty() || !writePreview) return false;
    const std::filesystem::path temporary = target.string() + ".tmp";
    std::error_code error;
    std::filesystem::remove(temporary, error);
    if (!writePreview(temporary) || !std::filesystem::is_regular_file(temporary, error)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    if (!AtomicReplace(temporary, target)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::string SaveManager::ToMetaText(const GameSave& save) {
    std::ostringstream out;
    out << "{\n";
    out << "\"schemaVersion\":" << save.schemaVersion << ",\n";
    out << "\"displayName\":" << JsonString(save.worldName) << ",\n";
    out << "\"saveName\":" << JsonString(save.saveName) << ",\n";
    out << "\"playerName\":" << JsonString(save.playerName) << ",\n";
    out << "\"seed\":" << save.seed << ",\n";
    out << "\"createdUtc\":" << JsonString(save.createdUtc) << ",\n";
    out << "\"lastPlayedUtc\":" << JsonString(save.lastPlayedAt) << ",\n";
    out << "\"playTimeSeconds\":" << save.playTimeSeconds << ",\n";
    out << "\"worldTick\":" << save.worldTick << ",\n";
    out << "\"spawnX\":" << save.spawnX << ",\"spawnY\":" << save.spawnY << ",\"spawnZ\":" << save.spawnZ << ",\n";
    out << "\"generatorVersion\":" << save.generatorVersion << ",\"engineVersion\":" << JsonString(save.engineVersion) << ",\n";
    out << "\"peaceful\":" << save.peaceful << ",\"permadeath\":" << save.permadeath << ",\"alwaysSunny\":" << save.alwaysSunny << ",\"sandboxMode\":" << save.sandboxMode << ",\n";
    out << "\"renderDistanceChunks\":" << save.renderDistanceChunks << ",\"simulationDistanceChunks\":" << save.simulationDistanceChunks << ",\"publicVisibility\":" << save.publicVisibility << "\n}";
    return out.str();
}

GameSave SaveManager::FromMetaText(const std::string& text) {
    GameSave save{};
    try {
        save.schemaVersion = static_cast<std::uint32_t>(std::stoul(ReadValue(text, "schemaVersion")));
        if (save.schemaVersion > 2) return {};
        save.saveName = ReadValue(text, "saveName"); save.worldName = ReadValue(text, "displayName"); save.playerName = ReadValue(text, "playerName");
        save.createdUtc = ReadValue(text, "createdUtc"); save.lastPlayedAt = ReadValue(text, "lastPlayedUtc"); save.seed = static_cast<WorldSeed>(std::stoul(ReadValue(text, "seed")));
        save.playTimeSeconds = std::stoull(ReadValue(text, "playTimeSeconds"));
        if (save.schemaVersion >= 2) save.worldTick = std::stoull(ReadValue(text, "worldTick"));
        else save.worldTick = kInitialWorldTick;
        save.spawnX = std::stof(ReadValue(text, "spawnX")); save.spawnY = std::stof(ReadValue(text, "spawnY")); save.spawnZ = std::stof(ReadValue(text, "spawnZ"));
        save.generatorVersion = static_cast<std::uint32_t>(std::stoul(ReadValue(text, "generatorVersion"))); save.engineVersion = ReadValue(text, "engineVersion");
        save.peaceful = ReadValue(text, "peaceful") == "1"; save.permadeath = ReadValue(text, "permadeath") == "1"; save.alwaysSunny = ReadValue(text, "alwaysSunny") != "0"; save.sandboxMode = ReadValue(text, "sandboxMode") == "1";
        save.renderDistanceChunks = std::stoi(ReadValue(text, "renderDistanceChunks")); save.simulationDistanceChunks = std::stoi(ReadValue(text, "simulationDistanceChunks")); save.publicVisibility = ReadValue(text, "publicVisibility") != "0";
    } catch (const std::exception&) { return {}; }
    return save;
}

bool SaveManager::Save(const GameSave& save) {
    if (!IsSafeFolderName(save.saveName)) {
        return false;
    }
    const std::filesystem::path dir = GetSaveDirectory(save.saveName);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }
    return AtomicWriteText(dir / kMetaFileName, ToMetaText(save));
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
    return !outSave.saveName.empty();
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
        if (slot.save.saveName.empty()) continue;
        slots.push_back(std::move(slot));
    }
    return slots;
}

bool SaveManager::DeleteSave(const std::string& saveName) {
    if (!IsSafeFolderName(saveName)) return false;
    std::error_code ec;
    const auto removed = std::filesystem::remove_all(GetSaveDirectory(saveName), ec);
    return !ec && removed > 0;
}

bool SaveManager::SavePlayerState(const std::string& saveName,
                                 const std::string& playerId,
                                 const PlayerState& state) const {
    (void)playerId;
    const auto playerPath = GetSaveDirectory(saveName) / "player.dat";
    if (playerPath.empty()) return false;
    return voxels::SavePlayerState(playerPath, state);
}

bool SaveManager::LoadPlayerState(const std::string& saveName,
                                 const std::string& playerId,
                                 PlayerState& outState) const {
    (void)playerId;
    const auto playerPath = GetSaveDirectory(saveName) / "player.dat";
    if (playerPath.empty()) return false;
    return voxels::LoadPlayerState(playerPath, outState);
}

bool SaveManager::SaveWorldState(const std::string& saveName, const World& world) const {
    const auto directory = GetSaveDirectory(saveName);
    return !directory.empty() && voxels::SaveWorld(world, directory);
}

bool SaveManager::LoadWorldState(const std::string& saveName, World& world) const {
    const auto directory = GetSaveDirectory(saveName);
    return !directory.empty() && voxels::LoadWorld(world, directory);
}

} // namespace voxels
