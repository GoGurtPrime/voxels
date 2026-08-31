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
#include "voxels/render/texture_forge.hpp"
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
        model.palette = {{214, 147, 79, 255, 1, 0, 0}, {94, 74, 48, 255, 2, 0, 0}};
        if (modelIndex != 0) {
            model.voxels.assign(model.GridVoxelCount(), 0);
            const auto fill = [&model](std::uint32_t minX, std::uint32_t maxX, std::uint32_t minY, std::uint32_t maxY,
                                       std::uint32_t minZ, std::uint32_t maxZ, std::uint16_t palette) {
                for (std::uint32_t z = minZ; z < maxZ; ++z) for (std::uint32_t y = minY; y < maxY; ++y) for (std::uint32_t x = minX; x < maxX; ++x) {
                    model.voxels[x + 16U * (y + 16U * z)] = palette;
                }
            };
            switch (modelIndex) {
                case 1: fill(0, 16, 0, 8, 0, 16, 1); break;
                case 2: fill(6, 10, 0, 16, 6, 10, 2); fill(2, 14, 6, 10, 6, 10, 1); break;
                case 3: fill(2, 14, 0, 16, 6, 10, 2); break;
                case 4: fill(7, 9, 0, 12, 7, 9, 1); fill(5, 11, 12, 14, 5, 11, 1); break;
                case 5: fill(2, 14, 0, 10, 2, 14, 2); fill(3, 13, 10, 13, 3, 13, 1); break;
                case 6: fill(7, 9, 0, 14, 7, 9, 2); fill(8, 16, 11, 14, 7, 9, 1); break;
                default: fill(4, 12, 0, 16, 4, 12, 1); fill(2, 14, 12, 16, 2, 14, 2); break;
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

TEST_CASE("Model block state rotates baked geometry", "[vmdl][render]") {
    voxels::BlockRegistry registry;
    registry.LoadFromJsonString(R"({"blocks":[{"id":"air","numeric_id":0,"solid":false,"opaque":false},{"id":"stairs","numeric_id":1,"solid":true,"opaque":true,"render_type":"model","model_id":"models/stairs.vmdl"}]})");
    voxels::graphics::ModelRegistry models;
    models.Register("models/stairs.vmdl", MakeStairModel());
    voxels::TextureAtlas atlas = MakeAtlas(registry);
    voxels::Chunk unrotated({0, 0, 0}, 1, 1, 1);
    voxels::Chunk rotated({0, 0, 0}, 1, 1, 1);
    unrotated.SetBlockAndState(0, 0, 0, 1, 0);
    rotated.SetBlockAndState(0, 0, 0, 1, 1);
    const auto originalMesh = voxels::graphics::BuildChunkMesh(unrotated, {}, registry, atlas, &models);
    const auto rotatedMesh = voxels::graphics::BuildChunkMesh(rotated, {}, registry, atlas, &models);
    REQUIRE(originalMesh.vertices.size() == rotatedMesh.vertices.size());
    REQUIRE((originalMesh.vertices[0].x != rotatedMesh.vertices[0].x || originalMesh.vertices[0].z != rotatedMesh.vertices[0].z));
}

TEST_CASE("Model blocks inherit their authored block texture layer", "[vmdl][render]") {
        voxels::BlockRegistry registry;
        registry.LoadFromJsonString(R"({"blocks":[
            {"id":"air","numeric_id":0,"solid":false,"opaque":false,"render_type":"cube"},
            {"id":"lantern","numeric_id":1,"solid":true,"opaque":true,"render_type":"model","model_id":"models/lantern.vmdl","textures":{"all":"blocks/lantern"}}
        ]})");
        voxels::TextureAtlas atlas(16, 16);
        atlas.RegisterTexture("blocks/lantern", voxels::TextureForge::GenerateTexture("planks"));
        REQUIRE(atlas.BuildGLTexture());
        voxels::graphics::ModelRegistry models;
        models.Register("models/lantern.vmdl", MakeStairModel());
        voxels::Chunk chunk({0, 0, 0}, 2, 2, 2);
        chunk.SetBlock(0, 0, 0, 1);
        const auto mesh = voxels::graphics::BuildChunkMesh(chunk, {}, registry, atlas, &models);
        REQUIRE_FALSE(mesh.vertices.empty());
        for (const auto& vertex : mesh.vertices) REQUIRE(vertex.atlasLayer == atlas.LayerFor("blocks/lantern"));
}