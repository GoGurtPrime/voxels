#pragma once

/**
 * @file save_manager.hpp
 * @brief Filesystem-backed implementation of `ISaveManager`.
 *
 * @details Each world save is its own directory under a configurable save root: metadata in
 *          `<root>/<saveName>/level.json` (flat JSON-style text), player state in
 *          `player.dat`, and chunk data alongside via the world serialization module. All
 *          metadata writes go through an atomic temp-file-then-rename. The main menu /
 *          world creation flow uses this to list, create, and load saves before handing a
 *          `GameSave` off to the world loading state.
 */

#include <filesystem>

#include "voxels/core/save.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/world/world_serialization.hpp"

namespace voxels {

class SaveManager final : public ISaveManager {
public:
    /// Creates the save root directory immediately (best-effort; errors are deferred to Save).
    explicit SaveManager(std::filesystem::path saveRootPath);

    /// Writes `<root>/<saveName>/level.json` atomically; false if the name is not
    /// filesystem-safe or the directory/file cannot be written.
    bool Save(const GameSave& save) override;
    /// False if the metadata file is missing, unparseable, or from a newer schema version.
    bool Load(const std::string& saveName, GameSave& outSave) override;
    /// Scans the save root for directories with readable metadata; unreadable ones are skipped.
    [[nodiscard]] std::vector<SaveSlot> ListSaves() const override;

    /// Recursively deletes the save directory; false if the name is unsafe or nothing was removed.
    bool DeleteSave(const std::string& saveName);
    /// Persists player state to `<save>/player.dat`. `playerId` is currently unused: each
    /// save holds a single player file.
    bool SavePlayerState(const std::string& saveName,
                         const std::string& playerId,
                         const PlayerState& state) const;
    bool LoadPlayerState(const std::string& saveName,
                         const std::string& playerId,
                         PlayerState& outState) const;
    /// Persists chunk data into the save directory via the world serialization module.
    bool SaveWorldState(const std::string& saveName, const World& world) const;
    bool LoadWorldState(const std::string& saveName, World& world) const;
    /// Returns `<root>/<saveName>`, or an empty path if the name is not filesystem-safe.
    [[nodiscard]] std::filesystem::path GetSaveDirectory(const std::string& saveName) const;

    /// Serializes save metadata to the flat JSON-style `level.json` text (bools as 0/1).
    [[nodiscard]] static std::string ToMetaText(const GameSave& save);
    /// Parses `ToMetaText` output; returns a default `GameSave` (empty saveName) on any
    /// parse failure or if `schemaVersion` is newer than this build supports.
    [[nodiscard]] static GameSave FromMetaText(const std::string& text);

private:
    std::filesystem::path m_saveRoot;
};

} // namespace voxels
