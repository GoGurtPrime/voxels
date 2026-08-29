#pragma once

/**
 * @file math.hpp
 * @brief GLM-based math aliases and voxel grid coordinate utilities.
 *
 * @details Wraps the GLM vector/matrix/quaternion types with engine-local aliases and provides
 *          conversions between continuous world-space positions and the discrete chunk/local
 *          block coordinates used by world storage, plus axis-aligned bounding box tests.
 *
 *          Relation to the rest of the codebase: world generation, physics/collision, and
 *          rendering all rely on these helpers to translate between floating-point world
 *          positions and the integer chunk/block grid defined in voxels/world/chunk.hpp.
 */

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "voxels/world/chunk.hpp"
#include "voxels/world/geometry.hpp"

namespace voxels {

using Vec3 = glm::vec3;
using IVec3 = glm::ivec3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

inline constexpr int kDefaultChunkSize = 16;

/// Maps a continuous world-space position to the chunk coordinate that contains it.
[[nodiscard]] ChunkCoordinate WorldPosToChunkPos(const Vec3& worldPos, int chunkSize = kDefaultChunkSize) noexcept;

/// Maps a continuous world-space position to its local block index within its owning chunk,
/// in the range [0, chunkSize) on each axis.
[[nodiscard]] Vec3I WorldPosToLocalBlockPos(const Vec3& worldPos, int chunkSize = kDefaultChunkSize) noexcept;

/// Returns true if two axis-aligned bounding boxes overlap or touch.
[[nodiscard]] bool IntersectsAABB(const BoundingBox& a, const BoundingBox& b) noexcept;

} // namespace voxels
