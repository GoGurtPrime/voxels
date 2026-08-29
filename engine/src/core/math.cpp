/**
 * @file math.cpp
 * @brief Implementation of voxel grid coordinate conversions and AABB intersection tests.
 */

#include "voxels/core/math.hpp"

#include <cmath>

namespace voxels {

namespace {

[[nodiscard]] int FloorDiv(float value, int divisor) noexcept {
    return static_cast<int>(std::floor(value / static_cast<float>(divisor)));
}

[[nodiscard]] int FloorMod(float value, int modulus) noexcept {
    const int cell = static_cast<int>(std::floor(value));
    int local = cell % modulus;
    if (local < 0) {
        local += modulus;
    }
    return local;
}

} // namespace

ChunkCoordinate WorldPosToChunkPos(const Vec3& worldPos, int chunkSize) noexcept {
    return ChunkCoordinate{
        FloorDiv(worldPos.x, chunkSize),
        FloorDiv(worldPos.y, chunkSize),
        FloorDiv(worldPos.z, chunkSize)};
}

Vec3I WorldPosToLocalBlockPos(const Vec3& worldPos, int chunkSize) noexcept {
    return Vec3I{
        FloorMod(worldPos.x, chunkSize),
        FloorMod(worldPos.y, chunkSize),
        FloorMod(worldPos.z, chunkSize)};
}

bool IntersectsAABB(const BoundingBox& a, const BoundingBox& b) noexcept {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

} // namespace voxels
