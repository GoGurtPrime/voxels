/**
 * @file world.cpp
 * @brief World manager implementation: chunk map, block access, lighting, and raycasting.
 *
 * @details Maps world-space block coordinates onto chunks via floor division/modulo, lazily
 *          allocating chunks on write. RebuildSkyLightAround recomputes one skylight column
 *          top-down (world Y 255..0) across resident sections after an edit. Raycast is an
 *          Amanatides-Woo voxel traversal reporting the entered face. IWorld's Generate()
 *          and Serialize() remain stubs; generation lives in the WorldGenerator pipeline.
 */

#include "voxels/world/world.hpp"

#include <cmath>
#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <unordered_set>

namespace voxels {

namespace {

int FloorDiv(int value, int divisor) noexcept {
    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

int FloorMod(int value, int divisor) noexcept {
    int modulo = value % divisor;
    if (modulo < 0) {
        modulo += divisor;
    }
    return modulo;
}

} // namespace

World::World() : World(Chunk::kDefaultSize) {
}

World::World(std::uint32_t chunkSize) : m_chunkSize(chunkSize) {
}

bool World::Initialize(const WorldOptions& options) {
    m_options = options;
    return true;
}

void World::Generate() {
}

Chunk& World::GetOrCreateChunk(const ChunkCoordinate& coordinate) {
    auto it = m_chunks.find(coordinate);
    if (it == m_chunks.end()) {
        it = m_chunks
                 .emplace(coordinate, std::make_shared<Chunk>(coordinate, m_chunkSize, m_chunkSize, m_chunkSize))
                 .first;
    }
    return *it->second;
}

bool World::HasChunk(const ChunkCoordinate& coordinate) const {
    return m_chunks.find(coordinate) != m_chunks.end();
}

std::size_t World::UnloadCleanChunksOutsideRadius(const ChunkCoordinate& center, int radius,
                                                  std::vector<ChunkCoordinate>* unloaded) {
    std::size_t unloadedCount = 0;
    for (auto chunk = m_chunks.begin(); chunk != m_chunks.end();) {
        const ChunkCoordinate& coordinate = chunk->first;
        const bool outsideRadius = std::abs(coordinate.x - center.x) > radius ||
                                   std::abs(coordinate.z - center.z) > radius;
        if (outsideRadius && !chunk->second->IsDirty()) {
            if (unloaded != nullptr) unloaded->push_back(coordinate);
            chunk = m_chunks.erase(chunk);
            ++unloadedCount;
        } else {
            ++chunk;
        }
    }
    return unloadedCount;
}

ChunkData World::GetChunk(const ChunkCoordinate& coordinate) const {
    const auto it = m_chunks.find(coordinate);
    if (it == m_chunks.end()) {
        return {};
    }
    return it->second->ToChunkData();
}

void World::SetPlayerSpawn(const ChunkCoordinate& coordinate) {
    m_spawnChunk = coordinate;
}

std::string World::Serialize() const {
    return {};
}

BlockId World::GetBlock(const Vec3I& worldBlockPos) const {
    const int size = static_cast<int>(m_chunkSize);
    const ChunkCoordinate coordinate{FloorDiv(worldBlockPos.x, size), FloorDiv(worldBlockPos.y, size),
                                      FloorDiv(worldBlockPos.z, size)};
    const auto it = m_chunks.find(coordinate);
    if (it == m_chunks.end()) {
        return static_cast<BlockId>(BlockType::Air);
    }
    return it->second->GetBlock(FloorMod(worldBlockPos.x, size), FloorMod(worldBlockPos.y, size),
                                 FloorMod(worldBlockPos.z, size));
}

bool World::SetBlock(const Vec3I& worldBlockPos, BlockId block) {
    const int size = static_cast<int>(m_chunkSize);
    const ChunkCoordinate coordinate{FloorDiv(worldBlockPos.x, size), FloorDiv(worldBlockPos.y, size),
                                      FloorDiv(worldBlockPos.z, size)};
    Chunk& chunk = GetOrCreateChunk(coordinate);
    return chunk.SetBlock(FloorMod(worldBlockPos.x, size), FloorMod(worldBlockPos.y, size),
                           FloorMod(worldBlockPos.z, size), block);
}

std::uint8_t World::GetSkyLight(const Vec3I& worldBlockPos) const {
    const int size = static_cast<int>(m_chunkSize);
    const ChunkCoordinate coordinate{FloorDiv(worldBlockPos.x, size), FloorDiv(worldBlockPos.y, size),
                                      FloorDiv(worldBlockPos.z, size)};
    const auto it = m_chunks.find(coordinate);
    if (it == m_chunks.end()) return 0;
    return it->second->GetSkyLight(FloorMod(worldBlockPos.x, size), FloorMod(worldBlockPos.y, size),
                                    FloorMod(worldBlockPos.z, size));
}

std::uint8_t World::GetBlockLight(const Vec3I& worldBlockPos) const {
    const int size = static_cast<int>(m_chunkSize);
    const ChunkCoordinate coordinate{FloorDiv(worldBlockPos.x, size), FloorDiv(worldBlockPos.y, size),
                                      FloorDiv(worldBlockPos.z, size)};
    const auto it = m_chunks.find(coordinate);
    if (it == m_chunks.end()) return 0;
    return it->second->GetBlockLight(FloorMod(worldBlockPos.x, size), FloorMod(worldBlockPos.y, size),
                                      FloorMod(worldBlockPos.z, size));
}

std::size_t World::RebuildSkyLightAround(const Vec3I& center, int radiusBlocks) {
    const BlockRegistry registry = CreateDefaultBlockRegistry();
    return RebuildLightingAround(center, registry, radiusBlocks).touchedVoxels;
}

LightingUpdate World::RebuildLightingAround(const Vec3I& center, const BlockRegistry& registry,
                                             int radiusBlocks) {
    LightingUpdate update;
    if (m_chunks.empty()) return update;
    radiusBlocks = std::clamp(radiusBlocks, 0, 15);
    const int size = static_cast<int>(m_chunkSize);
    int minimumY = 255;
    int maximumY = 0;
    for (const auto& [coordinate, chunk] : m_chunks) {
        (void)chunk;
        minimumY = std::min(minimumY, coordinate.y * size);
        maximumY = std::max(maximumY, (coordinate.y + 1) * size - 1);
    }
    minimumY = std::max(minimumY, 0);
    maximumY = std::min(maximumY, 255);

    const auto residentChunk = [&](const Vec3I& position) -> Chunk* {
        const ChunkCoordinate coordinate{FloorDiv(position.x, size), FloorDiv(position.y, size),
                                         FloorDiv(position.z, size)};
        const auto found = m_chunks.find(coordinate);
        return found == m_chunks.end() ? nullptr : found->second.get();
    };
    const auto isOpaque = [&](BlockId block) {
        const BlockDefinition* definition = registry.GetDefinition(block);
        return definition == nullptr ? block != static_cast<BlockId>(BlockType::Air) : definition->isOpaque;
    };

    int minimumYRebuild = std::max(minimumY, center.y - radiusBlocks);
    int maximumYRebuild = std::min(maximumY, center.y + radiusBlocks);
    bool exposed = true;
    int firstDirectMismatch = maximumY + 1;
    int lastDirectMismatch = minimumY - 1;
    for (int y = maximumY; y >= minimumY; --y) {
        const Vec3I position{center.x, y, center.z};
        if (residentChunk(position) == nullptr) continue;
        const bool opaque = isOpaque(GetBlock(position));
        const bool expectedDirectSky = exposed && !opaque;
        if ((GetSkyLight(position) == 15) != expectedDirectSky) {
            firstDirectMismatch = std::min(firstDirectMismatch, y);
            lastDirectMismatch = std::max(lastDirectMismatch, y);
        }
        if (opaque) exposed = false;
    }
    if (lastDirectMismatch >= firstDirectMismatch) {
        minimumYRebuild = std::min(minimumYRebuild,
                                   std::max(minimumY, firstDirectMismatch - radiusBlocks));
        maximumYRebuild = std::max(maximumYRebuild,
                                   std::min(maximumY, lastDirectMismatch + radiusBlocks));
    }
    if (minimumYRebuild > maximumYRebuild) return update;

    const int minimumX = center.x - radiusBlocks;
    const int maximumX = center.x + radiusBlocks;
    const int minimumZ = center.z - radiusBlocks;
    const int maximumZ = center.z + radiusBlocks;
    struct LightSnapshot {
        Vec3I position;
        std::uint8_t sky;
        std::uint8_t block;
    };
    std::vector<LightSnapshot> snapshots;
    const std::size_t width = static_cast<std::size_t>(maximumX - minimumX + 1);
    const std::size_t depth = static_cast<std::size_t>(maximumZ - minimumZ + 1);
    const std::size_t height = static_cast<std::size_t>(maximumYRebuild - minimumYRebuild + 1);
    snapshots.reserve(width * depth * height);
    for (int z = minimumZ; z <= maximumZ; ++z) {
        for (int y = minimumYRebuild; y <= maximumYRebuild; ++y) {
            for (int x = minimumX; x <= maximumX; ++x) {
                const Vec3I position{x, y, z};
                if (residentChunk(position) == nullptr) continue;
                snapshots.push_back({position, GetSkyLight(position), GetBlockLight(position)});
            }
        }
    }
    update.examinedVoxels = snapshots.size();

    const auto setSky = [&](const Vec3I& position, std::uint8_t level) {
        Chunk* chunk = residentChunk(position);
        if (chunk == nullptr) return false;
        const std::uint8_t clamped = std::min<std::uint8_t>(level, 15);
        const int localX = FloorMod(position.x, size);
        const int localY = FloorMod(position.y, size);
        const int localZ = FloorMod(position.z, size);
        if (chunk->GetSkyLight(localX, localY, localZ) == clamped) return false;
        chunk->SetSkyLight(localX, localY, localZ, clamped);
        return true;
    };
    const auto setBlockLight = [&](const Vec3I& position, std::uint8_t level) {
        Chunk* chunk = residentChunk(position);
        if (chunk == nullptr) return false;
        const std::uint8_t clamped = std::min<std::uint8_t>(level, 15);
        const int localX = FloorMod(position.x, size);
        const int localY = FloorMod(position.y, size);
        const int localZ = FloorMod(position.z, size);
        if (chunk->GetBlockLight(localX, localY, localZ) == clamped) return false;
        chunk->SetBlockLight(localX, localY, localZ, clamped);
        return true;
    };

    for (int z = minimumZ; z <= maximumZ; ++z) {
        for (int x = minimumX; x <= maximumX; ++x) {
            bool columnExposed = maximumYRebuild == maximumY;
            if (!columnExposed) {
                const Vec3I above{x, maximumYRebuild + 1, z};
                columnExposed = residentChunk(above) == nullptr ||
                                (GetSkyLight(above) == 15 && !isOpaque(GetBlock(above)));
            }
            for (int y = maximumYRebuild; y >= minimumYRebuild; --y) {
                const Vec3I position{x, y, z};
                if (residentChunk(position) == nullptr) continue;
                const BlockId block = GetBlock(position);
                const bool opaque = isOpaque(block);
                setSky(position, columnExposed && !opaque ? 15 : 0);
                setBlockLight(position, 0);
                if (opaque) columnExposed = false;
            }
        }
    }

    struct LightNode { Vec3I position; std::uint8_t level; };
    std::deque<LightNode> skyQueue;
    std::deque<LightNode> blockQueue;
    static constexpr std::array<Vec3I, 6> neighbors = {
        Vec3I{1, 0, 0}, Vec3I{-1, 0, 0}, Vec3I{0, 1, 0},
        Vec3I{0, -1, 0}, Vec3I{0, 0, 1}, Vec3I{0, 0, -1}};
    const auto inRegion = [&](const Vec3I& position) {
        return position.x >= minimumX && position.x <= maximumX &&
               position.y >= minimumYRebuild && position.y <= maximumYRebuild &&
               position.z >= minimumZ && position.z <= maximumZ;
    };
    for (int z = minimumZ; z <= maximumZ; ++z) {
        for (int x = minimumX; x <= maximumX; ++x) {
            for (int y = minimumYRebuild; y <= maximumYRebuild; ++y) {
                const Vec3I position{x, y, z};
                if (residentChunk(position) == nullptr) continue;
                const std::uint8_t sky = GetSkyLight(position);
                if (sky == 15) {
                    const bool bordersUnlitVoxel = std::any_of(
                        neighbors.begin(), neighbors.end(), [&](const Vec3I& offset) {
                            const Vec3I neighbor{position.x + offset.x, position.y + offset.y,
                                                 position.z + offset.z};
                            return inRegion(neighbor) && residentChunk(neighbor) != nullptr &&
                                   !isOpaque(GetBlock(neighbor)) && GetSkyLight(neighbor) < 14;
                        });
                    if (bordersUnlitVoxel) skyQueue.push_back({position, sky});
                }
                const BlockDefinition* definition = registry.GetDefinition(GetBlock(position));
                if (definition != nullptr && definition->lightEmission > 0) {
                    setBlockLight(position, definition->lightEmission);
                    blockQueue.push_back({position, definition->lightEmission});
                }
                const bool boundary = x == minimumX || x == maximumX || y == minimumYRebuild ||
                                      y == maximumYRebuild || z == minimumZ || z == maximumZ;
                if (!boundary || isOpaque(GetBlock(position))) continue;
                for (const Vec3I& offset : neighbors) {
                    const Vec3I outside{position.x + offset.x, position.y + offset.y,
                                        position.z + offset.z};
                    if (inRegion(outside) || residentChunk(outside) == nullptr) continue;
                    const std::uint8_t outsideSky = GetSkyLight(outside);
                    if (outsideSky > 1 && GetSkyLight(position) < outsideSky - 1) {
                        const std::uint8_t level = static_cast<std::uint8_t>(outsideSky - 1);
                        setSky(position, level);
                        skyQueue.push_back({position, level});
                    }
                }
            }
        }
    }
    const auto propagate = [&](std::deque<LightNode>& queue, bool skyLight) {
        while (!queue.empty()) {
            const LightNode current = queue.front();
            queue.pop_front();
            if (current.level <= 1) continue;
            const std::uint8_t nextLevel = static_cast<std::uint8_t>(current.level - 1);
            for (const Vec3I& offset : neighbors) {
                const Vec3I next{current.position.x + offset.x, current.position.y + offset.y,
                                 current.position.z + offset.z};
                if (!inRegion(next) || residentChunk(next) == nullptr || isOpaque(GetBlock(next))) continue;
                const std::uint8_t existing = skyLight ? GetSkyLight(next) : GetBlockLight(next);
                if (existing >= nextLevel) continue;
                if (skyLight) setSky(next, nextLevel); else setBlockLight(next, nextLevel);
                queue.push_back({next, nextLevel});
            }
        }
    };
    propagate(skyQueue, true);
    propagate(blockQueue, false);
    std::unordered_set<ChunkCoordinate, ChunkCoordinateHash> dirtyChunks;
    for (const LightSnapshot& snapshot : snapshots) {
        if (snapshot.sky == GetSkyLight(snapshot.position) &&
            snapshot.block == GetBlockLight(snapshot.position)) continue;
        ++update.touchedVoxels;
        dirtyChunks.insert({FloorDiv(snapshot.position.x, size), FloorDiv(snapshot.position.y, size),
                            FloorDiv(snapshot.position.z, size)});
    }
    update.dirtyChunks.assign(dirtyChunks.begin(), dirtyChunks.end());
    std::sort(update.dirtyChunks.begin(), update.dirtyChunks.end(), [](const ChunkCoordinate& lhs,
                                                                       const ChunkCoordinate& rhs) {
        if (lhs.x != rhs.x) return lhs.x < rhs.x;
        if (lhs.y != rhs.y) return lhs.y < rhs.y;
        return lhs.z < rhs.z;
    });
    return update;
}

RaycastHit World::Raycast(const Vec3& origin, const Vec3& direction, float maxDistance) const {
    RaycastHit result;

    const float lengthSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (lengthSq <= 0.0f) {
        return result;
    }
    const float invLength = 1.0f / std::sqrt(lengthSq);
    const Vec3 dir{direction.x * invLength, direction.y * invLength, direction.z * invLength};

    Vec3I voxel{static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                static_cast<int>(std::floor(origin.z))};

    const int stepX = dir.x > 0.0f ? 1 : (dir.x < 0.0f ? -1 : 0);
    const int stepY = dir.y > 0.0f ? 1 : (dir.y < 0.0f ? -1 : 0);
    const int stepZ = dir.z > 0.0f ? 1 : (dir.z < 0.0f ? -1 : 0);

    constexpr float kInfinity = std::numeric_limits<float>::infinity();

    const auto computeTMax = [kInfinity](float originComponent, int voxelComponent, float dirComponent, int step) {
        if (step == 0) {
            return kInfinity;
        }
        const float boundary = static_cast<float>(step > 0 ? voxelComponent + 1 : voxelComponent);
        return (boundary - originComponent) / dirComponent;
    };
    const auto computeTDelta = [kInfinity](float dirComponent) {
        if (dirComponent == 0.0f) {
            return kInfinity;
        }
        return std::fabs(1.0f / dirComponent);
    };

    float tMaxX = computeTMax(origin.x, voxel.x, dir.x, stepX);
    float tMaxY = computeTMax(origin.y, voxel.y, dir.y, stepY);
    float tMaxZ = computeTMax(origin.z, voxel.z, dir.z, stepZ);

    const float tDeltaX = computeTDelta(dir.x);
    const float tDeltaY = computeTDelta(dir.y);
    const float tDeltaZ = computeTDelta(dir.z);

    float traveled = 0.0f;
    Face hitFace = Face::PosY;

    while (traveled <= maxDistance) {
        const BlockId block = GetBlock(voxel);
        if (block != static_cast<BlockId>(BlockType::Air)) {
            result.hit = true;
            result.blockPosition = voxel;
            result.face = hitFace;
            result.distance = traveled;
            return result;
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            voxel.x += stepX;
            traveled = tMaxX;
            tMaxX += tDeltaX;
            hitFace = stepX > 0 ? Face::NegX : Face::PosX;
        } else if (tMaxY < tMaxZ) {
            voxel.y += stepY;
            traveled = tMaxY;
            tMaxY += tDeltaY;
            hitFace = stepY > 0 ? Face::NegY : Face::PosY;
        } else {
            voxel.z += stepZ;
            traveled = tMaxZ;
            tMaxZ += tDeltaZ;
            hitFace = stepZ > 0 ? Face::NegZ : Face::PosZ;
        }
    }

    return result;
}

} // namespace voxels
