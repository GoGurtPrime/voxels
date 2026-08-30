#pragma once

/*
 * Scope: Chunk representation and streaming-friendly world storage.
 *
 * This type is a contract for serialized chunk data, block storage, and object state
 * serialization. It is designed to support efficient streaming and low-memory operation
 * for low-end hardware while retaining enough structure for larger desktop markets.
 *
 * Relation to the rest of the codebase: world generation, simulation, and networking all
 * rely on chunk boundaries as the unit of loading, saving, and synchronization.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "voxels/world/block.hpp"

namespace voxels {

struct ChunkCoordinate {
    int x = 0;
    int y = 0;
    int z = 0;

    [[nodiscard]] bool operator==(const ChunkCoordinate&) const noexcept = default;
};

/// Hashes a ChunkCoordinate for use as a key in a sparse spatial hash map of loaded chunks.
struct ChunkCoordinateHash {
    [[nodiscard]] std::size_t operator()(const ChunkCoordinate& coordinate) const noexcept {
        std::size_t seed = static_cast<std::size_t>(coordinate.x) * 73856093u;
        seed ^= static_cast<std::size_t>(coordinate.y) * 19349663u;
        seed ^= static_cast<std::size_t>(coordinate.z) * 83492791u;
        return seed;
    }
};

struct ChunkObjectState {
    std::string id;
    std::string type;
    std::string serializedState;
};

struct ChunkData {
    ChunkCoordinate coordinate;
    std::uint32_t width = 16;
    std::uint32_t height = 16;
    std::uint32_t depth = 16;
    std::vector<std::uint32_t> blockPalette;
    std::vector<ChunkObjectState> objectStates;
    bool dirty = false;
};

class IChunkStorage {
public:
    virtual ~IChunkStorage() = default;
    virtual ChunkData LoadChunk(const ChunkCoordinate& coordinate) = 0;
    virtual void SaveChunk(const ChunkCoordinate& coordinate, const ChunkData& chunk) = 0;
    virtual bool ChunkExists(const ChunkCoordinate& coordinate) const = 0;
};

/// Dense in-memory voxel chunk: block storage, per-voxel block/sky light maps, and
/// run-length-encoded (de)serialization for compact disk/memory representation.
class Chunk {
public:
    static constexpr std::uint32_t kDefaultSize = 16;

    explicit Chunk(ChunkCoordinate coordinate,
                   std::uint32_t sizeX = kDefaultSize,
                   std::uint32_t sizeY = kDefaultSize,
                   std::uint32_t sizeZ = kDefaultSize);

    [[nodiscard]] BlockId GetBlock(int x, int y, int z) const noexcept;
    bool SetBlock(int x, int y, int z, BlockId block) noexcept;
    [[nodiscard]] std::uint8_t GetBlockState(int x, int y, int z) const noexcept;
    bool SetBlockState(int x, int y, int z, std::uint8_t state) noexcept;
    bool SetBlockAndState(int x, int y, int z, BlockId block, std::uint8_t state) noexcept;

    [[nodiscard]] std::uint8_t GetBlockLight(int x, int y, int z) const noexcept;
    bool SetBlockLight(int x, int y, int z, std::uint8_t level) noexcept;

    [[nodiscard]] std::uint8_t GetSkyLight(int x, int y, int z) const noexcept;
    bool SetSkyLight(int x, int y, int z, std::uint8_t level) noexcept;

    [[nodiscard]] bool InBounds(int x, int y, int z) const noexcept;

    [[nodiscard]] const ChunkCoordinate& GetCoordinate() const noexcept { return m_coordinate; }
    [[nodiscard]] std::uint32_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept { return m_height; }
    [[nodiscard]] std::uint32_t GetDepth() const noexcept { return m_depth; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
    void ClearDirty() noexcept { m_dirty = false; }

    /// Compresses the block array with run-length encoding into a compact byte buffer.
    [[nodiscard]] std::vector<std::uint8_t> SerializeRLE() const;

    /// Rebuilds a chunk of the given dimensions from a buffer produced by SerializeRLE().
    [[nodiscard]] static Chunk DeserializeRLE(const std::vector<std::uint8_t>& buffer,
                                               ChunkCoordinate coordinate,
                                               std::uint32_t sizeX = kDefaultSize,
                                               std::uint32_t sizeY = kDefaultSize,
                                               std::uint32_t sizeZ = kDefaultSize);

    /// Snapshots this chunk into the plain serialization DTO used by IChunkStorage/network code.
    [[nodiscard]] ChunkData ToChunkData() const;

private:
    [[nodiscard]] std::size_t Index(int x, int y, int z) const noexcept;

    ChunkCoordinate m_coordinate;
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_depth;
    std::vector<BlockId> m_blocks;
    std::vector<std::uint8_t> m_blockStates;
    std::vector<std::uint8_t> m_blockLight;
    std::vector<std::uint8_t> m_skyLight;
    bool m_dirty = false;
};

} // namespace voxels
