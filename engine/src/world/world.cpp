/*
 * Scope: World manager implementation.
 *
 * Implements the sparse spatial hash of loaded chunks, world-space block access (with
 * floor-division mapping into chunk-local coordinates), and Amanatides-Woo fast voxel
 * traversal raycasting for block picking and sub-voxel collision detection.
 *
 * Relation to the rest of the codebase: the app uses this module to orchestrate the world
 * setup flow and player spawn placement before gameplay begins.
 */

#include "voxels/world/world.hpp"

#include <cmath>
#include <limits>

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

bool IsOpaqueForSkyLight(BlockId block) noexcept {
    return block != static_cast<BlockId>(BlockType::Air) &&
           block != static_cast<BlockId>(BlockType::Water) &&
           block != static_cast<BlockId>(BlockType::Glass) &&
           block != static_cast<BlockId>(BlockType::Leaf);
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

std::size_t World::RebuildSkyLightAround(const Vec3I& center, int radiusBlocks) {
    (void)radiusBlocks;
    const int chunkSize = static_cast<int>(m_chunkSize);
    std::size_t touched = 0;
    const auto setLightIfResident = [this, center, chunkSize, &touched](int worldY, std::uint8_t light) {
        const Vec3I position{center.x, worldY, center.z};
        const ChunkCoordinate coordinate{FloorDiv(position.x, chunkSize), FloorDiv(position.y, chunkSize),
                                         FloorDiv(position.z, chunkSize)};
        const auto found = m_chunks.find(coordinate);
        if (found == m_chunks.end()) return false;
        const bool changed = found->second->SetSkyLight(FloorMod(position.x, chunkSize), FloorMod(position.y, chunkSize),
                                                         FloorMod(position.z, chunkSize), light);
        if (changed) ++touched;
        return true;
    };
    bool exposed = true;
    for (int worldY = 255; worldY >= 0; --worldY) {
        const BlockId block = GetBlock({center.x, worldY, center.z});
        setLightIfResident(worldY, exposed ? 15 : 0);
        if (IsOpaqueForSkyLight(block)) exposed = false;
    }
    return touched;
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
