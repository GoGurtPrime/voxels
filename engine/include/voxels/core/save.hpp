#pragma once

/*
 * Scope: Save management schema for world and player persistence.
 *
 * This layer defines the save contract used to store world state, per-player data, and
 * progression metadata in a single, versioned format. The implementation can later be
 * upgraded with compression, integrity checks, and migration logic.
 *
 * Relation to the rest of the codebase: game state transitions and world loading depend on
 * these capabilities for starts, saves, and ongoing session continuity.
 */

#include <string>
#include <vector>

#include "voxels/core/game_types.hpp"

namespace voxels {

struct SaveSlot {
    std::string slotName;
    GameSave save;
    std::vector<std::string> tags;
};

class ISaveManager {
public:
    virtual ~ISaveManager() = default;
    virtual bool Save(const GameSave& save) = 0;
    virtual bool Load(const std::string& saveName, GameSave& outSave) = 0;
    virtual std::vector<SaveSlot> ListSaves() const = 0;
};

} // namespace voxels
