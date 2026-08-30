#pragma once

/*
 * Scope: Filesystem-backed implementation of `ISaveManager`.
 *
 * Each world save is stored as its own directory under a configurable save root
 * (`<root>/<saveName>/save.meta`) so future per-world assets (chunk region files, player
 * data) can be added alongside the metadata without a format migration.
 *
 * Relation to the rest of the codebase: the main menu / world creation flow uses this to
 * list, create, and load saves before handing a `GameSave` off to the world loading state.
 */

#include <filesystem>

#include "voxels/core/save.hpp"
#include "voxels/gameplay/player.hpp"
#include "voxels/world/world_serialization.hpp"

namespace voxels {

class SaveManager final : public ISaveManager {
public:
    explicit SaveManager(std::filesystem::path saveRootPath);

    bool Save(const GameSave& save) override;
    bool Load(const std::string& saveName, GameSave& outSave) override;
    [[nodiscard]] std::vector<SaveSlot> ListSaves() const override;

    bool DeleteSave(const std::string& saveName);
    bool SavePlayerState(const std::string& saveName,
                         const std::string& playerId,
                         const PlayerState& state) const;
    bool LoadPlayerState(const std::string& saveName,
                         const std::string& playerId,
                         PlayerState& outState) const;
    [[nodiscard]] std::filesystem::path GetSaveDirectory(const std::string& saveName) const;

    [[nodiscard]] static std::string ToMetaText(const GameSave& save);
    [[nodiscard]] static GameSave FromMetaText(const std::string& text);

private:
    std::filesystem::path m_saveRoot;
};

} // namespace voxels
