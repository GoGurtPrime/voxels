/*
 * Scope: Chunk block/light storage and RLE serialization implementation.
 *
 * Implements bounds-safe block and lighting accessors over a flat array plus a simple
 * run-length encoding scheme (16-bit block id, 32-bit run length pairs) for compact
 * serialization suited to low-memory streaming targets.
 *
 * Relation to the rest of the codebase: the world manager stores chunks in its spatial hash
 * map and (de)serializes them through this type for disk/network transfer.
 */

#include "voxels/world/chunk.hpp"

namespace voxels {

Chunk::Chunk(ChunkCoordinate coordinate, std::uint32_t sizeX, std::uint32_t sizeY, std::uint32_t sizeZ)
    : m_coordinate(coordinate)
    , m_width(sizeX)
    , m_height(sizeY)
    , m_depth(sizeZ)
    , m_blocks(static_cast<std::size_t>(sizeX) * sizeY * sizeZ, static_cast<BlockId>(BlockType::Air))
    , m_blockStates(m_blocks.size(), 0)
    , m_blockLight(m_blocks.size(), 0)
    , m_skyLight(m_blocks.size(), 0) {
}

bool Chunk::InBounds(int x, int y, int z) const noexcept {
    return x >= 0 && y >= 0 && z >= 0 &&
           static_cast<std::uint32_t>(x) < m_width &&
           static_cast<std::uint32_t>(y) < m_height &&
           static_cast<std::uint32_t>(z) < m_depth;
}

std::size_t Chunk::Index(int x, int y, int z) const noexcept {
    return (static_cast<std::size_t>(z) * m_height + static_cast<std::size_t>(y)) * m_width +
           static_cast<std::size_t>(x);
}

BlockId Chunk::GetBlock(int x, int y, int z) const noexcept {
    if (!InBounds(x, y, z)) {
        return static_cast<BlockId>(BlockType::Air);
    }
    return m_blocks[Index(x, y, z)];
}

bool Chunk::SetBlock(int x, int y, int z, BlockId block) noexcept {
    if (!InBounds(x, y, z)) {
        return false;
    }
    m_blocks[Index(x, y, z)] = block;
    m_blockStates[Index(x, y, z)] = 0;
    m_dirty = true;
    return true;
}

std::uint8_t Chunk::GetBlockState(int x, int y, int z) const noexcept {
    return InBounds(x, y, z) ? m_blockStates[Index(x, y, z)] : 0;
}

bool Chunk::SetBlockState(int x, int y, int z, std::uint8_t state) noexcept {
    if (!InBounds(x, y, z)) return false;
    m_blockStates[Index(x, y, z)] = static_cast<std::uint8_t>(state & 0x03U);
    m_dirty = true;
    return true;
}

bool Chunk::SetBlockAndState(int x, int y, int z, BlockId block, std::uint8_t state) noexcept {
    if (!InBounds(x, y, z)) return false;
    const std::size_t index = Index(x, y, z);
    m_blocks[index] = block;
    m_blockStates[index] = static_cast<std::uint8_t>(state & 0x03U);
    m_dirty = true;
    return true;
}

std::uint8_t Chunk::GetBlockLight(int x, int y, int z) const noexcept {
    if (!InBounds(x, y, z)) {
        return 0;
    }
    return m_blockLight[Index(x, y, z)];
}

bool Chunk::SetBlockLight(int x, int y, int z, std::uint8_t level) noexcept {
    if (!InBounds(x, y, z)) {
        return false;
    }
    m_blockLight[Index(x, y, z)] = level;
    m_dirty = true;
    return true;
}

std::uint8_t Chunk::GetSkyLight(int x, int y, int z) const noexcept {
    if (!InBounds(x, y, z)) {
        return 0;
    }
    return m_skyLight[Index(x, y, z)];
}

bool Chunk::SetSkyLight(int x, int y, int z, std::uint8_t level) noexcept {
    if (!InBounds(x, y, z)) {
        return false;
    }
    m_skyLight[Index(x, y, z)] = level;
    m_dirty = true;
    return true;
}

std::vector<std::uint8_t> Chunk::SerializeRLE() const {
    std::vector<std::uint8_t> buffer;
    if (m_blocks.empty()) {
        return buffer;
    }

    const auto appendU16 = [&buffer](std::uint16_t value) {
        buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
        buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    };
    const auto appendU32 = [&buffer](std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            buffer.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
        }
    };

    BlockId runValue = m_blocks[0];
    std::uint8_t runState = m_blockStates[0];
    std::uint32_t runLength = 1;
    for (std::size_t i = 1; i < m_blocks.size(); ++i) {
        if (m_blocks[i] == runValue && m_blockStates[i] == runState) {
            ++runLength;
        } else {
            appendU16(runValue);
            buffer.push_back(runState);
            appendU32(runLength);
            runValue = m_blocks[i];
            runState = m_blockStates[i];
            runLength = 1;
        }
    }
    appendU16(runValue);
    buffer.push_back(runState);
    appendU32(runLength);

    return buffer;
}

Chunk Chunk::DeserializeRLE(const std::vector<std::uint8_t>& buffer, ChunkCoordinate coordinate,
                            std::uint32_t sizeX, std::uint32_t sizeY, std::uint32_t sizeZ) {
    Chunk chunk(coordinate, sizeX, sizeY, sizeZ);

    const std::size_t totalVoxels = chunk.m_blocks.size();
    std::size_t voxelIndex = 0;
    std::size_t cursor = 0;

    while (cursor + 7 <= buffer.size() && voxelIndex < totalVoxels) {
        const std::uint16_t value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(buffer[cursor]) | (static_cast<std::uint16_t>(buffer[cursor + 1]) << 8));
        const std::uint8_t state = static_cast<std::uint8_t>(buffer[cursor + 2] & 0x03U);
        const std::uint32_t length = static_cast<std::uint32_t>(buffer[cursor + 3]) |
                          (static_cast<std::uint32_t>(buffer[cursor + 4]) << 8) |
                          (static_cast<std::uint32_t>(buffer[cursor + 5]) << 16) |
                          (static_cast<std::uint32_t>(buffer[cursor + 6]) << 24);
        cursor += 7;

        for (std::uint32_t i = 0; i < length && voxelIndex < totalVoxels; ++i, ++voxelIndex) {
            chunk.m_blocks[voxelIndex] = value;
            chunk.m_blockStates[voxelIndex] = state;
        }
    }

    return chunk;
}

ChunkData Chunk::ToChunkData() const {
    ChunkData data;
    data.coordinate = m_coordinate;
    data.width = m_width;
    data.height = m_height;
    data.depth = m_depth;
    data.blockPalette.assign(m_blocks.begin(), m_blocks.end());
    data.dirty = false;
    return data;
}

} // namespace voxels
