#include "voxels/world/spawn_calculator.hpp"

#include <algorithm>
#include <cmath>

#include "voxels/world/world.hpp"

namespace voxels {
namespace {

bool IsTerrainSupport(BlockId block) noexcept {
    return block == static_cast<BlockId>(BlockType::Stone) || block == static_cast<BlockId>(BlockType::Dirt) ||
           block == static_cast<BlockId>(BlockType::Grass) || block == static_cast<BlockId>(BlockType::Sand) ||
           block == static_cast<BlockId>(BlockType::Gravel);
}

bool IsAir(const World& world, int x, int y, int z) noexcept {
    return world.GetBlock({x, y, z}) == static_cast<BlockId>(BlockType::Air);
}

int MaximumLoadedY(const World& world) noexcept {
    int maximum = 0;
    for (const auto& [coordinate, chunk] : world.GetChunks()) {
        (void)chunk;
        maximum = std::max(maximum, (coordinate.y + 1) * static_cast<int>(world.GetChunkSize()) - 1);
    }
    return maximum;
}

bool HasFlatClearSurface(const World& world, int x, int surfaceY, int z) noexcept {
    for (int offsetZ = -1; offsetZ <= 1; ++offsetZ) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            if (!IsTerrainSupport(world.GetBlock({x + offsetX, surfaceY, z + offsetZ})) ||
                !IsAir(world, x + offsetX, surfaceY + 1, z + offsetZ) ||
                !IsAir(world, x + offsetX, surfaceY + 2, z + offsetZ)) return false;
        }
    }
    return true;
}

} // namespace

Vec3I FindSafeSpawn(const Chunk& chunk, int chunkOriginX, int chunkOriginZ) {
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

    return Vec3I{chunkOriginX + bestX, bestY, chunkOriginZ + bestZ};
}

Vec3I FindSafeSpawn(const World& world, int centerX, int centerZ, int searchRadius) {
    const int maximumY = MaximumLoadedY(world);
    for (int radius = 0; radius <= std::max(0, searchRadius); ++radius) {
        for (int z = centerZ - radius; z <= centerZ + radius; ++z) {
            for (int x = centerX - radius; x <= centerX + radius; ++x) {
                if (radius != 0 && x != centerX - radius && x != centerX + radius && z != centerZ - radius && z != centerZ + radius) continue;
                for (int y = maximumY; y >= 0; --y) {
                    if (IsTerrainSupport(world.GetBlock({x, y, z})) && HasFlatClearSurface(world, x, y, z)) return {x, y, z};
                }
            }
        }
    }
    return {centerX, maximumY + 1, centerZ};
}

bool IsSafePlayerSpawn(const World& world, const Vec3& playerCenter) noexcept {
    const int minimumX = static_cast<int>(std::floor(playerCenter.x - 0.29f));
    const int maximumX = static_cast<int>(std::floor(playerCenter.x + 0.29f));
    const int minimumY = static_cast<int>(std::floor(playerCenter.y - 0.89f));
    const int maximumY = static_cast<int>(std::floor(playerCenter.y + 0.89f));
    const int minimumZ = static_cast<int>(std::floor(playerCenter.z - 0.29f));
    const int maximumZ = static_cast<int>(std::floor(playerCenter.z + 0.29f));
    for (int z = minimumZ; z <= maximumZ; ++z) {
        for (int y = minimumY; y <= maximumY; ++y) {
            for (int x = minimumX; x <= maximumX; ++x) if (!IsAir(world, x, y, z)) return false;
        }
    }
    return true;
}

} // namespace voxels
