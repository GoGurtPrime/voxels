#include "voxels/world/spawn_calculator.hpp"

#include <algorithm>

namespace voxels {

Vec3I FindSafeSpawn(const Chunk& chunk, int chunkOriginX, int chunkOriginZ) {
    (void)chunkOriginX;
    (void)chunkOriginZ;

    const int width = static_cast<int>(chunk.GetWidth());
    const int depth = static_cast<int>(chunk.GetDepth());
    const int height = static_cast<int>(chunk.GetHeight());

    int bestX = 0;
    int bestY = 1;
    int bestZ = 0;
    int highestSurface = -1;

    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            int topSolid = height - 1;
            while (topSolid > 0 && chunk.GetBlock(x, topSolid, z) == static_cast<BlockId>(BlockType::Air)) {
                --topSolid;
            }

            if (topSolid <= highestSurface) {
                continue;
            }

            const int spawnY = topSolid + 1;
            if (spawnY >= height) {
                continue;
            }

            const BlockId below = chunk.GetBlock(x, topSolid, z);
            const BlockId above = chunk.GetBlock(x, spawnY, z);
            if (above == static_cast<BlockId>(BlockType::Air) &&
                below != static_cast<BlockId>(BlockType::Air) &&
                below != static_cast<BlockId>(BlockType::Water)) {
                bestX = x;
                bestY = spawnY;
                bestZ = z;
                highestSurface = topSolid;
            }
        }
    }

    if (highestSurface < 0) {
        return Vec3I{0, 1, 0};
    }

    return Vec3I{bestX, bestY, bestZ};
}

} // namespace voxels
