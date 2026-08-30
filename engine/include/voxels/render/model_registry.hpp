/**
 * @file model_registry.hpp
 * @brief Loads and caches baked sub-voxel meshes for data-defined block models.
 *
 * @details Bridges VMDL asset data to the existing chunk vertex format without adding a
 * graphics dependency to the world module. See ARCHITECTURE.md §6.6.
 */

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "voxels/assets/vmdl_codec.hpp"
#include "voxels/render/chunk_vertex.hpp"

namespace voxels {
class BlockRegistry;

namespace graphics {

struct BakedModelMesh {
    std::vector<ChunkVertex> vertices;
    std::vector<std::uint32_t> indices;
};

class ModelRegistry {
public:
    /// Loads every model referenced by `blocks`; corrupt or absent files receive a visible mesh.
    void LoadReferencedModels(const BlockRegistry& blocks, const std::filesystem::path& assetRoot);
    void Register(std::string modelId, VoxelModel model);

    [[nodiscard]] const BakedModelMesh* FindMesh(const std::string& modelId) const noexcept;
    [[nodiscard]] const VoxelModel* FindModel(const std::string& modelId) const noexcept;

private:
    static BakedModelMesh Bake(const VoxelModel& model);
    static VoxelModel MakeFallbackModel();

    std::unordered_map<std::string, VoxelModel> m_models;
    std::unordered_map<std::string, BakedModelMesh> m_meshes;
};

} // namespace graphics
} // namespace voxels