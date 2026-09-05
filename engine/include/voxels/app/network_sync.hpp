/**
 * @file network_sync.hpp
 * @brief Applies server-streamed chunk payloads into a client-side world.
 *
 * @details Used by the join loading screen and the remote in-game session to integrate
 *          reassembled chunk sections (DIAGRAMS.md diagram 12 chunk streaming). Sections are
 *          surfaced to the renderer one full column at a time, after a footprint skylight
 *          rebuild, so partially streamed columns never render half-lit.
 */

#pragma once

#include <unordered_set>
#include <vector>

#include "voxels/networking/client.hpp"
#include "voxels/world/world.hpp"

namespace voxels {

class RemoteChunkApplier {
public:
    /// Terrain sections 0..7 plus the all-air cap section 8 make one renderable column.
    static constexpr int kColumnSectionCount = 9;

    /// Drains completed chunks from the client into `world` and returns the coordinates that
    /// became renderable (whole columns only).
    [[nodiscard]] std::vector<ChunkCoordinate> Apply(networking::GameClient& client, World& world,
                                                      const BlockRegistry& registry);

    [[nodiscard]] std::size_t AppliedChunkCount() const noexcept { return m_appliedChunks; }
    [[nodiscard]] std::size_t CompletedColumnCount() const noexcept { return m_completedColumns.size(); }
    [[nodiscard]] bool IsColumnComplete(int columnX, int columnZ) const noexcept;

private:
    struct ColumnHash {
        std::size_t operator()(const std::pair<int, int>& column) const noexcept {
            return std::hash<long long>{}((static_cast<long long>(column.first) << 32) ^
                                          static_cast<unsigned int>(column.second));
        }
    };

    std::unordered_set<std::pair<int, int>, ColumnHash> m_completedColumns;
    std::size_t m_appliedChunks = 0;
};

} // namespace voxels
