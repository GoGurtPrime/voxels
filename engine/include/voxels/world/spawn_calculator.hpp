/**
 * @file spawn_calculator.hpp
 * @brief Safe-world spawn placement helpers.
 *
 * @details Computes a player spawn location that sits just above the highest valid solid support
 *          while avoiding water, floating terrain, and enclosed geometry. This supports the world
 *          loading flow before the player is placed into the generated voxel map.
 */

#pragma once

#include "voxels/core/math.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/geometry.hpp"

namespace voxels {

class World;

[[nodiscard]] Vec3I FindSafeSpawn(const Chunk& chunk, int chunkOriginX = 0, int chunkOriginZ = 0);
[[nodiscard]] Vec3I FindSafeSpawn(const World& world, int centerX = 0, int centerZ = 0, int searchRadius = 32);
[[nodiscard]] bool IsSafePlayerSpawn(const World& world, const Vec3& playerCenter) noexcept;

} // namespace voxels
