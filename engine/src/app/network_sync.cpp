/**
 * @file network_sync.cpp
 * @brief Implements streamed-chunk integration and column skylight rebuilds.
 */

#include "voxels/app/network_sync.hpp"

namespace voxels {

std::vector<ChunkCoordinate> RemoteChunkApplier::Apply(networking::GameClient& client, World& world,
                                                       const BlockRegistry& registry) {
    std::vector<ChunkCoordinate> renderable;
    const int chunkSize = static_cast<int>(world.GetChunkSize());
    for (networking::NetworkChunk& networkChunk : client.TakeCompletedChunks()) {
        const ChunkCoordinate coordinate{networkChunk.coordinate.x, networkChunk.coordinate.y,
                                         networkChunk.coordinate.z};
        if (coordinate.y < 0 || coordinate.y >= kColumnSectionCount) continue;
        if (!world.HasChunk(coordinate)) {
            Chunk chunk = Chunk::DeserializeRLE(networkChunk.rleData, coordinate);
            chunk.ClearDirty();
            world.GetOrCreateChunk(coordinate) = std::move(chunk);
            ++m_appliedChunks;
        }
        const std::pair<int, int> column{coordinate.x, coordinate.z};
        if (m_completedColumns.contains(column)) continue;
        bool complete = true;
        for (int y = 0; y < kColumnSectionCount && complete; ++y) {
            complete = world.HasChunk({coordinate.x, y, coordinate.z});
        }
        if (!complete) continue;
        m_completedColumns.insert(column);
        const Vec3I columnCenter{coordinate.x * chunkSize + chunkSize / 2, 0,
                                 coordinate.z * chunkSize + chunkSize / 2};
        static_cast<void>(world.RebuildLightingAround(columnCenter, registry, chunkSize));
        for (int y = 0; y < kColumnSectionCount; ++y) {
            renderable.push_back({coordinate.x, y, coordinate.z});
        }
    }
    return renderable;
}

bool RemoteChunkApplier::IsColumnComplete(int columnX, int columnZ) const noexcept {
    return m_completedColumns.contains({columnX, columnZ});
}

} // namespace voxels
