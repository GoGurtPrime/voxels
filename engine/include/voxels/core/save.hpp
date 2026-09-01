#pragma once

/**
 * @file save.hpp
 * @brief Save persistence contract (`ISaveManager`) and the menu-facing `SaveSlot` view.
 *
 * @details Defines how world metadata (`GameSave`, versioned via its schemaVersion field)
 *          is stored and enumerated. Menu state transitions and world loading depend on
 *          this contract; `SaveManager` (voxels/app/save_manager.hpp) is the filesystem
 *          implementation.
 */

#include <string>
#include <vector>

#include "voxels/core/game_types.hpp"

namespace voxels {

/// One entry in the main menu's save list.
struct SaveSlot {
    std::string slotName; ///< Save directory name; usually equals save.saveName.
    GameSave save;
    std::vector<std::string> tags; ///< Reserved; not currently populated by any producer.
};

/// Persistence contract for world save metadata.
class ISaveManager {
public:
    virtual ~ISaveManager() = default;
    /// Persists `save` keyed by its saveName; false on invalid name or write failure.
    virtual bool Save(const GameSave& save) = 0;
    /// Fills `outSave` for the named save; false (leaving no valid data) when missing or corrupt.
    virtual bool Load(const std::string& saveName, GameSave& outSave) = 0;
    /// Enumerates all readable saves; implementations must skip corrupt entries, not fail.
    virtual std::vector<SaveSlot> ListSaves() const = 0;
};

} // namespace voxels
