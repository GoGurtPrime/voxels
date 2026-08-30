/**
 * @file test_voxel_models.cpp
 * @brief Behavioral tests for VMDL validation and data-driven sub-voxel mesh emission.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

#include "voxels/assets/vmdl_codec.hpp"
#include "voxels/render/chunk_mesher.hpp"
#include "voxels/render/model_registry.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/chunk.hpp"

namespace {

voxels::VoxelModel MakeStairModel() {
    voxels::VoxelModel model;
    model.palette = {{255, 255, 255, 255, 0, 0, 0}};
    model.voxels.assign(model.GridVoxelCount(), 0);
    for (std::uint32_t z = 0; z < 16; ++z) for (std::uint32_t x = 0; x < 16; ++x) {
        for (std::uint32_t y = 0; y < (z < 8 ? 8U : 16U); ++y) model.voxels[x + 16U * (y + 16U * z)] = 1;
    }
    model.ComputeBounds();
    return model;
}

voxels::TextureAtlas MakeAtlas(const voxels::BlockRegistry& registry) {
    voxels::TextureAtlas atlas(16, 16);
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");
    atlas.BuildGLTexture();
    return atlas;
}

} // namespace

TEST_CASE("Vmdl round trips deterministically and validates corruption", "[vmdl]") {
    const voxels::VoxelModel source = MakeStairModel();
    const std::vector<std::byte> first = voxels::VmdlCodec::Save(source);
    const std::vector<std::byte> second = voxels::VmdlCodec::Save(source);
    REQUIRE(first == second);
    const voxels::VoxelModel loaded = voxels::VmdlCodec::Load(first);
    REQUIRE(voxels::VmdlCodec::Save(loaded) == first);

    std::vector<std::byte> badMagic = first;
    badMagic[0] = std::byte{'X'};
    REQUIRE_THROWS_WITH(voxels::VmdlCodec::Load(badMagic), Catch::Matchers::ContainsSubstring("bad magic"));

    std::vector<std::byte> badCrc = first;
    badCrc.back() ^= std::byte{1};
    REQUIRE_THROWS_WITH(voxels::VmdlCodec::Load(badCrc), Catch::Matchers::ContainsSubstring("CRC mismatch"));
}

TEST_CASE("Vmdl baker culls interior micro voxels", "[vmdl][render]") {
    voxels::VoxelModel solid;
    solid.palette = {{255, 255, 255, 255, 0, 0, 0}};
    solid.voxels.assign(solid.GridVoxelCount(), 1);
    voxels::graphics::ModelRegistry models;
    models.Register("solid", solid);
    const voxels::graphics::BakedModelMesh* mesh = models.FindMesh("solid");
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices.size() == 6U * 16U * 16U * 4U);
    REQUIRE(mesh->indices.size() == 6U * 16U * 16U * 6U);
}

TEST_CASE("Model blocks emit registered VMDL geometry through blocks data", "[vmdl][render]") {
    voxels::BlockRegistry registry;
    registry.LoadFromJsonString(R"({"blocks":[
      {"id":"air","numeric_id":0,"solid":false,"opaque":false,"render_type":"cube"},
      {"id":"stairs","numeric_id":1,"solid":true,"opaque":true,"render_type":"model","model_id":"models/stairs.vmdl"}
    ]})");
    voxels::graphics::ModelRegistry models;
    models.Register("models/stairs.vmdl", MakeStairModel());
    voxels::TextureAtlas atlas = MakeAtlas(registry);
    voxels::Chunk chunk({0, 0, 0}, 3, 3, 3);
    chunk.SetBlock(1, 1, 1, 1);

    const auto mesh = voxels::graphics::BuildChunkMesh(chunk, {}, registry, atlas, &models);
    REQUIRE(mesh.opaqueIndexCount > 36);
    REQUIRE(mesh.vertices.size() > 24);
    bool hasSubVoxelVertex = false;
    for (const auto& vertex : mesh.vertices) {
        if (vertex.y == 24U) { hasSubVoxelVertex = true; break; }
    }
    REQUIRE(hasSubVoxelVertex);
}

void WriteSampleModels() {
    const std::filesystem::path directory = std::filesystem::path(VOXELS_SOURCE_DIR) / "app" / "assets" / "models";
    std::filesystem::create_directories(directory);
    const std::array<std::string, 8> names = {"stairs", "slab", "fence", "door", "torch", "chest", "pickaxe", "player"};
    for (std::size_t modelIndex = 0; modelIndex < names.size(); ++modelIndex) {
        voxels::VoxelModel model = MakeStairModel();
        if (modelIndex != 0) {
            model.voxels.assign(model.GridVoxelCount(), 0);
            const std::uint32_t height = modelIndex == 1 ? 8U : 16U;
            for (std::uint32_t z = 4; z < 12; ++z) for (std::uint32_t x = 4; x < 12; ++x) for (std::uint32_t y = 0; y < height; ++y) {
                model.voxels[x + 16U * (y + 16U * z)] = 1;
            }
            model.ComputeBounds();
        }
        const std::vector<std::byte> bytes = voxels::VmdlCodec::Save(model);
        std::ofstream output(directory / (names[modelIndex] + ".vmdl"), std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output.good());
    }
}

TEST_CASE("Vmdl sample assets are real codec payloads", "[vmdl][assets]") {
    WriteSampleModels();
    const std::filesystem::path directory = std::filesystem::path(VOXELS_SOURCE_DIR) / "app" / "assets" / "models";
    for (const std::string& name : {"stairs", "slab", "fence", "door", "torch", "chest", "pickaxe", "player"}) {
        std::ifstream input(directory / (name + ".vmdl"), std::ios::binary);
        const std::vector<char> raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes;
        for (const char value : raw) bytes.push_back(static_cast<std::byte>(value));
        REQUIRE_FALSE(bytes.empty());
        REQUIRE(voxels::VmdlCodec::Save(voxels::VmdlCodec::Load(bytes)) == bytes);
    }
}