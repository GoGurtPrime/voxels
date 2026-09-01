#pragma once

/**
 * @file chunk.hpp
 * @brief Dense voxel chunk storage: blocks, per-voxel light, and RLE serialization.
 *
 * @details Declares the 16^3-default Chunk (block ids, 2-bit block states, block/sky light
 *          levels), the ChunkData transfer snapshot with its storage contract, and the
 *          coordinate/hash types keying the World's sparse chunk map. Generation, simulation,
 *          persistence, and networking all treat the chunk as the unit of loading, saving,
 *          and synchronization (ARCHITECTURE.md §6.2).
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "voxels/world/block.hpp"

namespace voxels {

/// Position of a chunk on the chunk grid (units of whole chunks, not blocks).
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

/// Serialized state of a dynamic object anchored to a chunk (payload is opaque to the chunk).
struct ChunkObjectState {
    std::string id;
    std::string type;
    std::string serializedState;
};

/// Plain-data chunk snapshot exchanged with IChunkStorage and network transfer code.
struct ChunkData {
    ChunkCoordinate coordinate;
    std::uint32_t width = 16;
    std::uint32_t height = 16;
    std::uint32_t depth = 16;
    std::vector<std::uint32_t> blockPalette; ///< Dense per-voxel block ids (not a palette, despite the name).
    std::vector<ChunkObjectState> objectStates;
    bool dirty = false;
};

/// Pluggable persistence backend that loads/saves chunk snapshots keyed by coordinate.
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
    /// Default edge length in voxels; a standard chunk section is a 16^3 cube.
    static constexpr std::uint32_t kDefaultSize = 16;

    explicit Chunk(ChunkCoordinate coordinate,
                   std::uint32_t sizeX = kDefaultSize,
                   std::uint32_t sizeY = kDefaultSize,
                   std::uint32_t sizeZ = kDefaultSize);

    /// Block id at chunk-local coordinates; out-of-bounds reads return Air.
    [[nodiscard]] BlockId GetBlock(int x, int y, int z) const noexcept;
    /// Writes a block (resetting its state to 0) and marks the chunk dirty; false if out of bounds.
    bool SetBlock(int x, int y, int z, BlockId block) noexcept;
    /// 2-bit auxiliary state (e.g. orientation) at chunk-local coordinates; 0 when out of bounds.
    [[nodiscard]] std::uint8_t GetBlockState(int x, int y, int z) const noexcept;
    /// Stores the low 2 bits of `state`; false if out of bounds.
    bool SetBlockState(int x, int y, int z, std::uint8_t state) noexcept;
    /// Writes block id and 2-bit state together in one dirty-marking update; false if out of bounds.
    bool SetBlockAndState(int x, int y, int z, BlockId block, std::uint8_t state) noexcept;

    /// Block-emitted light level 0-15 at chunk-local coordinates; 0 when out of bounds.
    [[nodiscard]] std::uint8_t GetBlockLight(int x, int y, int z) const noexcept;
    bool SetBlockLight(int x, int y, int z, std::uint8_t level) noexcept;

    /// Sky light level 0-15 (15 = direct sky) at chunk-local coordinates; 0 when out of bounds.
    [[nodiscard]] std::uint8_t GetSkyLight(int x, int y, int z) const noexcept;
    bool SetSkyLight(int x, int y, int z, std::uint8_t level) noexcept;

    [[nodiscard]] bool InBounds(int x, int y, int z) const noexcept;

    [[nodiscard]] const ChunkCoordinate& GetCoordinate() const noexcept { return m_coordinate; }
    [[nodiscard]] std::uint32_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept { return m_height; }
    [[nodiscard]] std::uint32_t GetDepth() const noexcept { return m_depth; }
    /// True if any voxel changed since the last ClearDirty(); streaming only evicts clean chunks.
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
