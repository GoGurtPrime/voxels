#pragma once

/*
 * Scope: Geometry and sub-voxel representation types.
 *
 * The groundwork supports large world blocks and shapes such as stairs or other partial
 * blocks that occupy a single logical block without behaving like full opaque voxels.
 *
 * Relation to the rest of the codebase: renderers, collision code, and editor previews use
 * these primitives to describe volumetric shapes and draw order.
 */

#include <array>
#include <cstdint>
#include <vector>

namespace voxels {

struct Vec3I {
    int x = 0;
    int y = 0;
    int z = 0;

    [[nodiscard]] bool operator==(const Vec3I&) const noexcept = default;
};

struct BoundingBox {
    Vec3I min;
    Vec3I max;
};

struct GeometryVoxel {
    Vec3I position;
    std::uint32_t size = 1;
    bool fullBlock = true;
};

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
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    Face face = Face::PosY;
};

/// Runs greedy meshing face culling over a dense solid/empty voxel grid (row-major, x fastest,
/// then y, then z). Interior faces between two solid voxels are skipped, and coplanar adjacent
/// exposed faces are merged into the largest possible quads.
[[nodiscard]] std::vector<MeshQuad> GreedyMeshFaces(const std::vector<std::uint8_t>& solidGrid,
                                                     int sizeX, int sizeY, int sizeZ);

} // namespace voxels
