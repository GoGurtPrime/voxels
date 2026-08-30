/**
 * @file model_registry.cpp
 * @brief VMDL file loading and CPU mesh baking for chunk-compatible vertices.
 *
 * @details Faces between solid micro-voxels are omitted before the mesh reaches the GPU.
 */

#include "voxels/render/model_registry.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include "voxels/core/logger.hpp"
#include "voxels/world/block.hpp"

namespace voxels::graphics {
namespace {

void EmitQuad(BakedModelMesh& mesh, const std::array<std::array<std::uint16_t, 3>, 4>& positions, Face face,
              const VoxelPaletteEntry& palette) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr std::array<std::array<std::uint8_t, 2>, 4> kUvs = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        ChunkVertex vertex;
        vertex.x = positions[index][0]; vertex.y = positions[index][1]; vertex.z = positions[index][2];
        vertex.faceIndex = static_cast<std::uint8_t>(face); vertex.atlasLayer = palette.textureLayer;
        vertex.u = kUvs[index][0]; vertex.v = kUvs[index][1]; vertex.blockLight = palette.emissive;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.insert(mesh.indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
}

} // namespace

void ModelRegistry::LoadReferencedModels(const BlockRegistry& blocks, const std::filesystem::path& assetRoot) {
    for (const auto& [blockId, definition] : blocks.GetAllDefinitions()) {
        (void)blockId;
        if (definition.renderType != "model" || !definition.modelId.has_value() || m_models.contains(*definition.modelId)) continue;
        const std::filesystem::path path = assetRoot / *definition.modelId;
        try {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) throw std::runtime_error("file could not be opened");
            const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            std::vector<std::byte> bytes;
            bytes.reserve(raw.size());
            for (const char value : raw) bytes.push_back(static_cast<std::byte>(value));
            Register(*definition.modelId, VmdlCodec::Load(bytes));
        } catch (const std::exception& error) {
            Logger logger;
            logger.Warn("Model '" + *definition.modelId + "' failed to load from " + path.string() + ": " + error.what() + "; using visible fallback.");
            Register(*definition.modelId, MakeFallbackModel());
        }
    }
}

void ModelRegistry::Register(std::string modelId, VoxelModel model) {
    BakedModelMesh mesh = Bake(model);
    m_models.insert_or_assign(modelId, std::move(model));
    m_meshes.insert_or_assign(std::move(modelId), std::move(mesh));
}

const BakedModelMesh* ModelRegistry::FindMesh(const std::string& modelId) const noexcept {
    const auto found = m_meshes.find(modelId);
    return found == m_meshes.end() ? nullptr : &found->second;
}

const VoxelModel* ModelRegistry::FindModel(const std::string& modelId) const noexcept {
    const auto found = m_models.find(modelId);
    return found == m_models.end() ? nullptr : &found->second;
}

BakedModelMesh ModelRegistry::Bake(const VoxelModel& model) {
    BakedModelMesh mesh;
    for (std::uint32_t z = 0; z < model.gridSize[2]; ++z) for (std::uint32_t y = 0; y < model.gridSize[1]; ++y) for (std::uint32_t x = 0; x < model.gridSize[0]; ++x) {
        if (!model.IsSolidAt(x, y, z)) continue;
        const std::size_t voxelIndex = x + static_cast<std::size_t>(model.gridSize[0]) * (y + static_cast<std::size_t>(model.gridSize[1]) * z);
        const VoxelPaletteEntry& palette = model.palette[model.voxels[voxelIndex] - 1U];
        const std::uint16_t x0 = static_cast<std::uint16_t>(x * 16U / model.gridSize[0]);
        const std::uint16_t x1 = static_cast<std::uint16_t>((x + 1U) * 16U / model.gridSize[0]);
        const std::uint16_t y0 = static_cast<std::uint16_t>(y * 16U / model.gridSize[1]);
        const std::uint16_t y1 = static_cast<std::uint16_t>((y + 1U) * 16U / model.gridSize[1]);
        const std::uint16_t z0 = static_cast<std::uint16_t>(z * 16U / model.gridSize[2]);
        const std::uint16_t z1 = static_cast<std::uint16_t>((z + 1U) * 16U / model.gridSize[2]);
        if (!model.IsSolidAt(x + 1U, y, z)) EmitQuad(mesh, {{{x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0}}}, Face::PosX, palette);
        if (x == 0 || !model.IsSolidAt(x - 1U, y, z)) EmitQuad(mesh, {{{x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1}}}, Face::NegX, palette);
        if (!model.IsSolidAt(x, y + 1U, z)) EmitQuad(mesh, {{{x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}}}, Face::PosY, palette);
        if (y == 0 || !model.IsSolidAt(x, y - 1U, z)) EmitQuad(mesh, {{{x0,y0,z1},{x1,y0,z1},{x1,y0,z0},{x0,y0,z0}}}, Face::NegY, palette);
        if (!model.IsSolidAt(x, y, z + 1U)) EmitQuad(mesh, {{{x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1}}}, Face::PosZ, palette);
        if (z == 0 || !model.IsSolidAt(x, y, z - 1U)) EmitQuad(mesh, {{{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0}}}, Face::NegZ, palette);
    }
    return mesh;
}

VoxelModel ModelRegistry::MakeFallbackModel() {
    VoxelModel model;
    model.palette = {{255, 0, 255, 255, 0, 0, 0}};
    model.voxels.assign(model.GridVoxelCount(), 0);
    for (std::uint32_t z = 4; z < 12; ++z) for (std::uint32_t y = 0; y < 8; ++y) for (std::uint32_t x = 4; x < 12; ++x) {
        model.voxels[x + 16U * (y + 16U * z)] = 1;
    }
    model.ComputeBounds();
    return model;
}

} // namespace voxels::graphics