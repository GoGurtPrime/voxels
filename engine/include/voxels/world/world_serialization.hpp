#pragma once

/**
 * @file world_serialization.hpp
 * @brief World and player save/load helpers for persistent runtime sessions.
 *
 * @details Stores chunk data in a per-world region directory plus a compact metadata file so the
 *          game can resume from a previous session without needing to regenerate the entirety of the
 *          world on startup. The save layout intentionally favors lazy chunk loading and simple file
 *          inspection for debug and tooling workflows.
 */

#include <filesystem>
#include <string>

#include "voxels/gameplay/player.hpp"
#include "voxels/world/world.hpp"

namespace voxels {

bool SaveWorld(const World& world, const std::filesystem::path& saveRoot);
bool LoadWorld(World& world, const std::filesystem::path& saveRoot);

bool SavePlayerState(const std::filesystem::path& playerFile, const PlayerState& state);
bool LoadPlayerState(const std::filesystem::path& playerFile, PlayerState& outState);

} // namespace voxels
