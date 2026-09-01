#pragma once

/**
 * @file world.hpp
 * @brief Runtime world manager: sparse chunk map, world-space block access, and raycasting.
 *
 * @details World keys loaded chunks by ChunkCoordinate in a spatial hash and floor-divides
 *          world-space block coordinates into chunk-local ones. It provides skylight queries
 *          and column relighting after edits, radius-based eviction of clean chunks for
 *          streaming, and Amanatides-Woo voxel traversal for block picking. GameSession and
 *          the networking server drive it as the authoritative block store
 *          (ARCHITECTURE.md §6.2).
 */

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "voxels/core/game_types.hpp"
#include "voxels/core/math.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/geometry.hpp"
#include "voxels/world/spawn_calculator.hpp"
#include "voxels/world/world_options.hpp"

namespace voxels {

/// Result of a voxel-grid raycast against loaded world chunks.
struct RaycastHit {
    bool hit = false;
    Vec3I blockPosition{};   ///< World-space coordinate of the solid block that was hit.
    Face face = Face::PosY;  ///< Face the ray entered through (faces back toward the ray origin).
    float distance = 0.0f;   ///< Distance from the ray origin in world units.
};

/// Abstract world lifecycle contract consumed by the app flow and tests.
class IWorld {
public:
    virtual ~IWorld() = default;
    virtual bool Initialize(const WorldOptions& options) = 0;
    virtual void Generate() = 0;
    virtual ChunkData GetChunk(const ChunkCoordinate& coordinate) const = 0;
    virtual void SetPlayerSpawn(const ChunkCoordinate& coordinate) = 0;
    virtual std::string Serialize() const = 0;
};

/// Concrete world manager: owns a sparse spatial hash map of loaded chunks, exposes
/// world-space block access, and implements fast voxel-grid raycasting (Amanatides-Woo)
/// for block picking and sub-voxel collision detection.
class World : public IWorld {
public:
    World();
    explicit World(std::uint32_t chunkSize);

    bool Initialize(const WorldOptions& options) override;
    void Generate() override;
    ChunkData GetChunk(const ChunkCoordinate& coordinate) const override;
    void SetPlayerSpawn(const ChunkCoordinate& coordinate) override;
    std::string Serialize() const override;

    /// Returns the chunk at the given coordinate, creating and loading it (all-air) if absent.
    Chunk& GetOrCreateChunk(const ChunkCoordinate& coordinate);
    [[nodiscard]] bool HasChunk(const ChunkCoordinate& coordinate) const;
    /// Drops every loaded chunk; used when an authoritative world is rebuilt for a new session.
    void Clear() noexcept { m_chunks.clear(); }
    /// Evicts clean chunk columns outside the horizontal streaming radius and returns their count.
    [[nodiscard]] std::size_t UnloadCleanChunksOutsideRadius(const ChunkCoordinate& center, int radius,
                                                               std::vector<ChunkCoordinate>* unloaded = nullptr);
    [[nodiscard]] std::size_t LoadedChunkCount() const noexcept { return m_chunks.size(); }
    [[nodiscard]] std::uint32_t GetChunkSize() const noexcept { return m_chunkSize; }
    [[nodiscard]] const std::unordered_map<ChunkCoordinate, std::shared_ptr<Chunk>, ChunkCoordinateHash>&
    GetChunks() const noexcept {
        return m_chunks;
    }

    /// Block id at a world-space position; Air when the containing chunk is not resident.
    [[nodiscard]] BlockId GetBlock(const Vec3I& worldBlockPos) const;
    /// Writes a block at a world-space position, creating the containing chunk if needed.
    bool SetBlock(const Vec3I& worldBlockPos, BlockId block);
    /// Sky light (0-15) at a world-space block position; 0 for an unresident chunk. Zero means
    /// no direct line to the open sky — used as the "enclosed/underground" signal for the
    /// first-cave-entered platform achievement (work_items/18).
    [[nodiscard]] std::uint8_t GetSkyLight(const Vec3I& worldBlockPos) const;

    /// Rebuilds the edited skylight column across resident sections and returns touched voxels.
    [[nodiscard]] std::size_t RebuildSkyLightAround(const Vec3I& center, int radiusBlocks = 0);

    /// Casts a ray through the voxel grid starting at `origin` along `direction` (need not be
    /// normalized), up to `maxDistance` world units, returning the first solid block hit.
    [[nodiscard]] RaycastHit Raycast(const Vec3& origin, const Vec3& direction, float maxDistance) const;

private:
    WorldOptions m_options;
    ChunkCoordinate m_spawnChunk;
    std::uint32_t m_chunkSize;
    std::unordered_map<ChunkCoordinate, std::shared_ptr<Chunk>, ChunkCoordinateHash> m_chunks;
};

} // namespace voxels
