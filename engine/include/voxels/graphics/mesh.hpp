/**
 * @file mesh.hpp
 * @brief Shared mesh data formats for voxel rendering.
 *
 * @details Keeps CPU mesh construction independent from a graphics API while defining the
 *          stable vertex layout consumed by RHI pipeline states on every platform.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace voxels::graphics {

/// Unpacked 52-byte float vertex consumed through the RHI vertex-layout path (tests/tools).
/// The shipping voxel world path uses the 16-byte packed ChunkVertex instead.
struct VoxelVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
    std::array<float, 4> color{};
    std::uint32_t subVoxelData{0}; ///< Reserved for sub-voxel detail encoding; currently unused.
};

/// Indexed triangle list in VoxelVertex format.
struct MeshData {
    std::vector<VoxelVertex> vertices;
    std::vector<std::uint32_t> indices;
};

} // namespace voxels::graphics