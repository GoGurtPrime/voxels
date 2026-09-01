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

/// Model geometry pre-baked into the packed chunk-vertex format (same 1/16-block fixed point),
/// so model blocks splice straight into chunk meshes.
struct BakedModelMesh {
    std::vector<ChunkVertex> vertices;
    std::vector<std::uint32_t> indices;
};

/// Cache of loaded VMDL models and their baked meshes, keyed by model id.
class ModelRegistry {
public:
    /// Loads every model referenced by `blocks`; corrupt or absent files receive a visible mesh.
    void LoadReferencedModels(const BlockRegistry& blocks, const std::filesystem::path& assetRoot);
    /// Stores or replaces the model and immediately (re)bakes its mesh.
    void Register(std::string modelId, VoxelModel model);
    /// Bakes a model using the same exposed-face algorithm used by runtime model blocks.
    [[nodiscard]] static BakedModelMesh BakeModel(const VoxelModel& model);

    [[nodiscard]] const BakedModelMesh* FindMesh(const std::string& modelId) const noexcept;
    [[nodiscard]] const VoxelModel* FindModel(const std::string& modelId) const noexcept;

private:
    static VoxelModel MakeFallbackModel();

    std::unordered_map<std::string, VoxelModel> m_models;
    std::unordered_map<std::string, BakedModelMesh> m_meshes;
};

} // namespace graphics
} // namespace voxels