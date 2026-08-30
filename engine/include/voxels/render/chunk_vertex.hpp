#pragma once

/**
 * @file chunk_vertex.hpp
 * @brief Packed GPU vertex format for chunk mesh geometry.
 *
 * @details 16 bytes/vertex, bandwidth-conscious layout consumed by chunk.glsl.
 *          Reference ARCHITECTURE.md §6.2 and work_items/05_chunk_mesh_pipeline_and_world_rendering.md.
 *
 * Packing scheme (one byte/field rather than sub-byte bitfields, traded for simplicity and
 * portable `glVertexAttribPointer` wiring - still a 2.5x reduction versus the prior 40-byte
 * VoxelVertex format):
 *  - x,y,z:      chunk-local position in 1/16th-of-a-block fixed point (value = blocks * 16),
 *                so a water surface can sit a fraction of a block below the full-height face.
 *  - faceIndex:  voxels::Face enum value (0..5); selects the per-face brightness in the shader.
 *  - ao:         ambient occlusion term for this corner, 0 (darkest) .. 3 (unoccluded).
 *  - skyLight:   sky light level at this face, 0..15.
 *  - blockLight: emitted block light level at this face, 0..15.
 *  - atlasLayer: layer index into the block texture 2D array.
 *  - u,v:        unnormalized texture coordinate in whole tiles (0..chunk size); the atlas
 *                sampler uses GL_REPEAT wrapping so merged quads tile correctly.
 *  - tint:       0 = no tint (opaque white), 1 = foliage tint (grass/leaves green multiply).
 */

#include <cstdint>

namespace voxels::graphics {

inline constexpr float kChunkVertexPositionScale = 16.0f;

struct ChunkVertex {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t z = 0;
    std::uint8_t faceIndex = 0;
    std::uint8_t ao = 3;
    std::uint8_t skyLight = 15;
    std::uint8_t blockLight = 0;
    std::uint16_t atlasLayer = 0;
    std::uint8_t u = 0;
    std::uint8_t v = 0;
    std::uint8_t tint = 0;
    std::uint8_t reserved = 0;

    [[nodiscard]] bool operator==(const ChunkVertex&) const noexcept = default;
};

} // namespace voxels::graphics
