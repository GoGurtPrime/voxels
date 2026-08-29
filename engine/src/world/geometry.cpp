/*
 * Scope: Sub-voxel geometry and greedy meshing face culling implementation.
 *
 * Implements a dense-grid greedy mesher (per axis mask sweep) that skips faces bordered by
 * another solid voxel and merges coplanar exposed faces into the largest possible quads.
 *
 * Relation to the rest of the codebase: chunk mesh generation and the editor preview use this
 * to turn dense block occupancy grids into a minimal set of renderable quads.
 */

#include "voxels/world/geometry.hpp"

#include <cstddef>

namespace voxels {

namespace {

bool SolidAt(const std::vector<std::uint8_t>& grid, int sizeX, int sizeY, int sizeZ, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= sizeX || y >= sizeY || z >= sizeZ) {
        return false;
    }
    const std::size_t index = (static_cast<std::size_t>(z) * sizeY + y) * sizeX + x;
    return grid[index] != 0;
}

Face FaceForAxis(int axis, bool positiveDirection) {
    switch (axis) {
        case 0:
            return positiveDirection ? Face::PosX : Face::NegX;
        case 1:
            return positiveDirection ? Face::PosY : Face::NegY;
        default:
            return positiveDirection ? Face::PosZ : Face::NegZ;
    }
}

} // namespace

std::vector<MeshQuad> GreedyMeshFaces(const std::vector<std::uint8_t>& solidGrid, int sizeX, int sizeY, int sizeZ) {
    std::vector<MeshQuad> quads;
    const int dims[3] = {sizeX, sizeY, sizeZ};

    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;

        int pos[3] = {0, 0, 0};
        int step[3] = {0, 0, 0};
        step[axis] = 1;

        std::vector<std::int8_t> mask(static_cast<std::size_t>(dims[u]) * static_cast<std::size_t>(dims[v]));

        for (pos[axis] = -1; pos[axis] < dims[axis];) {
            std::size_t maskIndex = 0;
            for (pos[v] = 0; pos[v] < dims[v]; ++pos[v]) {
                for (pos[u] = 0; pos[u] < dims[u]; ++pos[u], ++maskIndex) {
                    const bool a = SolidAt(solidGrid, sizeX, sizeY, sizeZ, pos[0], pos[1], pos[2]);
                    const bool b = SolidAt(solidGrid, sizeX, sizeY, sizeZ,
                                            pos[0] + step[0], pos[1] + step[1], pos[2] + step[2]);
                    if (a == b) {
                        mask[maskIndex] = 0;
                    } else if (a) {
                        mask[maskIndex] = 1;
                    } else {
                        mask[maskIndex] = -1;
                    }
                }
            }

            ++pos[axis];

            maskIndex = 0;
            for (int j = 0; j < dims[v]; ++j) {
                for (int i = 0; i < dims[u];) {
                    const std::int8_t value = mask[maskIndex];
                    if (value == 0) {
                        ++i;
                        ++maskIndex;
                        continue;
                    }

                    int width = 1;
                    while (i + width < dims[u] && mask[maskIndex + width] == value) {
                        ++width;
                    }

                    int height = 1;
                    bool blocked = false;
                    while (j + height < dims[v] && !blocked) {
                        for (int k = 0; k < width; ++k) {
                            if (mask[maskIndex + k + height * dims[u]] != value) {
                                blocked = true;
                                break;
                            }
                        }
                        if (!blocked) {
                            ++height;
                        }
                    }

                    int quadPos[3] = {pos[0], pos[1], pos[2]};
                    quadPos[u] = i;
                    quadPos[v] = j;

                    MeshQuad quad;
                    quad.position = Vec3I{quadPos[0], quadPos[1], quadPos[2]};
                    quad.width = static_cast<std::uint32_t>(width);
                    quad.height = static_cast<std::uint32_t>(height);
                    quad.face = FaceForAxis(axis, value > 0);
                    quads.push_back(quad);

                    for (int hh = 0; hh < height; ++hh) {
                        for (int ww = 0; ww < width; ++ww) {
                            mask[maskIndex + ww + hh * dims[u]] = 0;
                        }
                    }

                    i += width;
                    maskIndex += width;
                }
            }
        }
    }

    return quads;
}

} // namespace voxels
