/**
 * @file spawn_calculator.hpp
 * @brief Safe-world spawn placement helpers.
 *
 * @details Computes a player spawn location that sits just above the highest valid solid support
 *          while avoiding water, floating terrain, and enclosed geometry. This supports the world
 *          loading flow before the player is placed into the generated voxel map.
 */

#pragma once

#include "voxels/world/chunk.hpp"
#include "voxels/world/geometry.hpp"

namespace voxels {

[[nodiscard]] Vec3I FindSafeSpawn(const Chunk& chunk, int chunkOriginX = 0, int chunkOriginZ = 0);

} // namespace voxels
