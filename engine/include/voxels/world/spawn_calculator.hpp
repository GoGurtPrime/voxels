/**
 * @file spawn_calculator.hpp
 * @brief Safe-world spawn placement helpers.
 *
 * @details Computes a player spawn location that sits just above the highest valid solid support
 *          while avoiding water, floating terrain, and enclosed geometry. This supports the world
 *          loading flow before the player is placed into the generated voxel map.
 */

#pragma once

#include <optional>

#include "voxels/core/math.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/geometry.hpp"

namespace voxels {

class World;

[[nodiscard]] Vec3I FindSafeSpawn(const Chunk& chunk, int chunkOriginX = 0, int chunkOriginZ = 0);

/// Rings outward from (centerX, centerZ) searching only within searchRadius for a flat, dry,
/// player-clear surface. Returns std::nullopt rather than any fallback when nothing qualifies -
/// callers decide whether to expand generation/search or surface a visible error.
[[nodiscard]] std::optional<Vec3I> TryFindSafeSpawn(const World& world, int centerX = 0, int centerZ = 0,
                                                    int searchRadius = 32);

/// Exhaustively scans every currently loaded chunk for the dry surface closest to
/// (centerX, centerZ), ignoring searchRadius. Used only after TryFindSafeSpawn fails, so a
/// composed-but-unlucky search area (e.g. all ocean) does not force generating past the loaded set.
[[nodiscard]] std::optional<Vec3I> FindAnyLoadedDrySpawn(const World& world, int centerX = 0, int centerZ = 0);

/// Convenience wrapper used by non-interactive paths (dedicated server bootstrap, tests) that must
/// always return a position: tries the ring search, then any loaded dry surface, and only as a
/// last, explicitly unsafe resort (no dry ground anywhere loaded) returns a position above the
/// highest loaded section. Interactive loading should prefer TryFindSafeSpawn/FindAnyLoadedDrySpawn
/// and surface a visible error instead of relying on that last resort.
[[nodiscard]] Vec3I FindSafeSpawn(const World& world, int centerX = 0, int centerZ = 0, int searchRadius = 32);
[[nodiscard]] bool IsSafePlayerSpawn(const World& world, const Vec3& playerCenter) noexcept;

} // namespace voxels

