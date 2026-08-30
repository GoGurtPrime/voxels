#pragma once

/**
 * @file chunk_mesher.hpp
 * @brief Neighbour-aware, lit, ambient-occluded chunk mesher producing GPU-ready mesh data.
 *
 * @details Consumes a `Chunk` plus its six face neighbours (any of which may be absent) and
 *          emits a `ChunkMeshData`: a packed `ChunkVertex` buffer, an index buffer split into an
 *          opaque range followed by a transparent range, ambient occlusion, sky/block light, and
 *          greedy-merged quads. This lives under `render/` (not `world/`) because it depends on
 *          `TextureAtlas` - see ARCHITECTURE.md's forbidden-edges rule (`world` never includes
 *          `graphics`). Reference work_items/05_chunk_mesh_pipeline_and_world_rendering.md.
 */

#include <array>
#include <cstdint>
#include <vector>

#include "voxels/render/chunk_vertex.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/geometry.hpp"

namespace voxels::graphics {

/// The six face-adjacent neighbours of the chunk being meshed, indexed by `voxels::Face`.
/// A null entry means that neighbour is not yet resident; the mesher never draws a face against
/// an unknown neighbour (it assumes occluded) and sets `ChunkMeshData::provisional` so the
/// caller knows to re-mesh once the neighbour becomes available.
struct ChunkNeighborhood {
    std::array<const Chunk*, 6> neighbors{};
};

struct ChunkMeshData {
    std::vector<ChunkVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t opaqueIndexCount = 0;      // indices[0, opaqueIndexCount)
    std::uint32_t transparentIndexCount = 0; // indices[opaqueIndexCount, opaqueIndexCount + transparentIndexCount)
    bool provisional = false;
};

/// Builds a chunk mesh. `chunk` and every present neighbour in `neighborhood` must share the
/// same dimensions as `chunk`.
[[nodiscard]] ChunkMeshData BuildChunkMesh(const Chunk& chunk,
                                            const ChunkNeighborhood& neighborhood,
                                            const BlockRegistry& registry,
                                            const TextureAtlas& atlas);

} // namespace voxels::graphics
