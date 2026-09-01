#pragma once

/**
 * @file geometry.hpp
 * @brief Voxel-grid geometry primitives and greedy-mesh quad generation.
 *
 * @details Declares the integer vector/box types shared by world and render code, the
 *          sub-voxel collision shape for partial blocks (stairs, slabs, fences), and the
 *          dense-grid GreedyMeshFaces() mesher. Tests exercise this mesher directly and it
 *          serves as the reference for the render layer's material-aware chunk mesher
 *          (render/chunk_mesher.hpp).
 */

#include <array>
#include <cstdint>
#include <vector>

namespace voxels {

/// Integer 3-vector used for world-space block positions and grid coordinates.
struct Vec3I {
    int x = 0;
    int y = 0;
    int z = 0;

    [[nodiscard]] bool operator==(const Vec3I&) const noexcept = default;
};

/// Axis-aligned box spanning `min` to `max` in integer grid units.
struct BoundingBox {
    Vec3I min;
    Vec3I max;
};

/// Volumetric cell that may span multiple voxels (`size` = edge length); groundwork for
/// large-block shapes, not yet consumed by engine code.
struct GeometryVoxel {
    Vec3I position;
    std::uint32_t size = 1;
    bool fullBlock = true;
};

/// Per-vertex attributes for simple mesh interchange; the render layer uses its own packed
/// ChunkVertex format instead.
struct MeshData {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> normal{0.0f, 0.0f, 0.0f};
    std::array<float, 2> uv{0.0f, 0.0f};
};

/// Direction of a cube face, used for meshing, raycast hit reporting, and occlusion checks.
enum class Face : std::uint8_t {
    PosX,
    NegX,
    PosY,
    NegY,
    PosZ,
    NegZ
};

/// Sub-voxel collision geometry: a fixed-capacity set of AABBs inside a single 1x1x1 block
/// unit, used for non-full-block shapes like stairs, slabs, and fences.
struct SubVoxelShape {
    static constexpr std::size_t kMaxBoxes = 4;

    std::array<BoundingBox, kMaxBoxes> collisionBoxes{};
    std::uint32_t boxCount = 0;

    /// Appends a collision box; false once the kMaxBoxes capacity is exhausted.
    bool AddBox(const BoundingBox& box) {
        if (boxCount >= collisionBoxes.size()) {
            return false;
        }
        collisionBoxes[boxCount++] = box;
        return true;
    }
};

/// A single merged quad face produced by greedy meshing, expressed in local grid units.
struct MeshQuad {
    Vec3I position;
    std::uint32_t width = 1;   ///< Extent along the first in-plane axis (axis pairing depends on face).
    std::uint32_t height = 1;  ///< Extent along the second in-plane axis.
    Face face = Face::PosY;
};

/// Runs greedy meshing face culling over a dense solid/empty voxel grid (row-major, x fastest,
/// then y, then z). Interior faces between two solid voxels are skipped, and coplanar adjacent
/// exposed faces are merged into the largest possible quads.
[[nodiscard]] std::vector<MeshQuad> GreedyMeshFaces(const std::vector<std::uint8_t>& solidGrid,
                                                     int sizeX, int sizeY, int sizeZ);

} // namespace voxels
