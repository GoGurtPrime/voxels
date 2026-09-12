/**
 * @file test_item_drop_render.cpp
 * @brief Material, winding, and GPU regressions for dropped-item rendering.
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "voxels/assets/asset_manager.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/render/item_drop_renderer.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"

namespace {

voxels::gameplay::ItemDrop MakeDrop(voxels::BlockId blockId, float age, voxels::Vec3 position = {}) {
    voxels::gameplay::ItemDrop drop;
    drop.id = static_cast<std::uint32_t>(blockId);
    drop.stack = {blockId, 1};
    drop.position = position;
    drop.age = age;
    return drop;
}

} // namespace

TEST_CASE("ItemDropRenderer.RoutesCatalogueMaterialsAndPreservesSimulationPositions",
          "[render][item_drop]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas;
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");
    REQUIRE(atlas.BuildGLTexture());
    const std::vector<voxels::gameplay::ItemDrop> drops = {
        MakeDrop(static_cast<voxels::BlockId>(voxels::BlockType::Stone), 0.5f, {1.0f, 2.0f, 3.0f}),
        MakeDrop(static_cast<voxels::BlockId>(voxels::BlockType::Grass), 0.75f),
        MakeDrop(static_cast<voxels::BlockId>(voxels::BlockType::Glass), 1.0f),
        MakeDrop(static_cast<voxels::BlockId>(voxels::BlockType::Water), 1.25f),
        MakeDrop(16, 1.5f),
    };
    const auto originalPosition = drops.front().position;

    const voxels::graphics::ItemDropRenderBatch batch =
        voxels::graphics::BuildItemDropRenderBatch(drops, registry, atlas);

    REQUIRE(batch.opaque.size() == 72U);
    REQUIRE(batch.transparent.size() == 72U);
    REQUIRE(batch.cutout.size() == 24U);
    REQUIRE(drops.front().position.x == originalPosition.x);
    REQUIRE(drops.front().position.y == originalPosition.y);
    REQUIRE(drops.front().position.z == originalPosition.z);
    REQUIRE(batch.cutout.front().atlasLayer == static_cast<float>(atlas.LayerFor("blocks/coal_ore")));
}

TEST_CASE("ItemDropRenderer.CubeTrianglesHaveOutwardCounterClockwiseWinding",
          "[render][item_drop][winding]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas;
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");
    REQUIRE(atlas.BuildGLTexture());
    const auto batch = voxels::graphics::BuildItemDropRenderBatch(
        {MakeDrop(static_cast<voxels::BlockId>(voxels::BlockType::Stone), 0.0f)}, registry, atlas);

    REQUIRE(batch.opaque.size() == 36U);
    for (std::size_t index = 0; index < batch.opaque.size(); index += 3U) {
        const auto& a = batch.opaque[index];
        const auto& b = batch.opaque[index + 1U];
        const auto& c = batch.opaque[index + 2U];
        const glm::vec3 geometricNormal = glm::cross(b.position - a.position, c.position - a.position);
        REQUIRE(glm::dot(geometricNormal, a.normal) > 0.0f);
    }
}

TEST_CASE("ItemDropRenderer.UsesBakedGeometryAndLiveAtlasForModelItems",
          "[render][item_drop][model]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas;
    atlas.PopulateFromBlockRegistry(registry, voxels::Paths::AssetsDir() / "textures");
    REQUIRE(atlas.BuildGLTexture());
    voxels::graphics::ModelRegistry models;
    models.LoadReferencedModels(registry, voxels::Paths::AssetsDir());

    const auto batch = voxels::graphics::BuildItemDropRenderBatch({MakeDrop(14, 0.0f)}, registry, atlas, &models);

    REQUIRE_FALSE(batch.opaque.empty());
    REQUIRE(batch.opaque.size() != 36U);
    const float planksLayer = static_cast<float>(atlas.LayerFor("blocks/planks"));
    for (const auto& vertex : batch.opaque) REQUIRE(vertex.atlasLayer == planksLayer);
}

TEST_CASE("ItemDropRenderer.GpuFrontAndBackViewsSampleAtlasMaterial",
          "[render][item_drop][gpu]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    constexpr int kSize = 128;
    SDL_Window* window = SDL_CreateWindow("Item Drop Renderer Test", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, kSize, kSize,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    REQUIRE(context != nullptr);
    REQUIRE(gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress)));

    voxels::graphics::GLRenderer frameRenderer;
    REQUIRE(frameRenderer.Initialize());
    voxels::BlockRegistry registry;
    voxels::BlockDefinition material;
    material.id = 1;
    material.name = "gpu_test_item";
    material.displayName = "GPU Test Item";
    material.textures.all = "items/gpu_test";
    registry.RegisterBlock(material);
    voxels::BlockDefinition rearMaterial = material;
    rearMaterial.id = 2;
    rearMaterial.name = "gpu_test_rear";
    rearMaterial.displayName = "GPU Test Rear Item";
    rearMaterial.textures.all = "items/gpu_test_rear";
    registry.RegisterBlock(rearMaterial);
    const auto solidImage = [](std::array<std::uint8_t, 4> color) {
        voxels::ImageData image;
        image.width = 16;
        image.height = 16;
        image.channels = 4;
        image.pixels.resize(16U * 16U * 4U);
        for (std::size_t pixel = 0; pixel < image.pixels.size(); pixel += 4U) {
            std::copy(color.begin(), color.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(pixel));
        }
        return image;
    };
    voxels::TextureAtlas atlas;
    atlas.RegisterTexture("items/gpu_test", solidImage({230, 20, 15, 255}));
    atlas.RegisterTexture("items/gpu_test_rear", solidImage({20, 230, 15, 255}));
    REQUIRE(atlas.BuildGLTexture());
    voxels::graphics::ItemDropRenderer itemRenderer(registry, atlas);
    const std::vector drops{MakeDrop(1, 0.0f)};

    for (const auto [z, yaw] : std::array<std::pair<float, float>, 2>{
             std::pair{2.0f, 0.0f}, std::pair{-2.0f, glm::pi<float>()}}) {
        REQUIRE(frameRenderer.BeginFrame({0.0f, 0.0f, 0.0f, 1.0f}, kSize, kSize));
        voxels::Camera camera;
        camera.position = {0.0f, 0.0f, z};
        camera.yaw = yaw;
        camera.aspect = 1.0f;
        itemRenderer.Render(camera, drops);
        std::array<std::uint8_t, 4> pixel{};
        glReadPixels(kSize / 2, kSize / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        REQUIRE(pixel[0] > 40U);
        REQUIRE(pixel[0] > pixel[1] * 2U);
        REQUIRE(itemRenderer.GetMetrics().drawCalls == 1U);
    }

    REQUIRE(frameRenderer.BeginFrame({0.0f, 0.0f, 0.0f, 1.0f}, kSize, kSize));
    voxels::Camera depthCamera;
    depthCamera.position = {0.0f, 0.0f, 2.0f};
    depthCamera.yaw = 0.0f;
    depthCamera.aspect = 1.0f;
    const std::vector depthDrops{MakeDrop(1, 0.0f, {0.0f, 0.0f, 0.3f}),
                                 MakeDrop(2, 0.0f, {0.0f, 0.0f, -0.3f})};
    itemRenderer.Render(depthCamera, depthDrops);
    std::array<std::uint8_t, 4> depthPixel{};
    glReadPixels(kSize / 2, kSize / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, depthPixel.data());
    REQUIRE(depthPixel[0] > depthPixel[1] * 2U);

    std::vector<voxels::gameplay::ItemDrop> stressDrops;
    stressDrops.reserve(256);
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            stressDrops.push_back(MakeDrop(1, 0.25f, {
                (static_cast<float>(x) - 7.5f) * 0.45f,
                (static_cast<float>(z) - 7.5f) * 0.18f,
                -static_cast<float>(z) * 0.35f}));
        }
    }
    REQUIRE(frameRenderer.BeginFrame({0.0f, 0.0f, 0.0f, 1.0f}, kSize, kSize));
    itemRenderer.Render(depthCamera, stressDrops);
    GLuint timer = 0;
    glGenQueries(1, &timer);
    glBeginQuery(GL_TIME_ELAPSED, timer);
    itemRenderer.Render(depthCamera, stressDrops);
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 elapsedNanoseconds = 0;
    glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &elapsedNanoseconds);
    glDeleteQueries(1, &timer);
    CAPTURE(elapsedNanoseconds);
    REQUIRE(itemRenderer.GetMetrics().visibleDrops == 256U);
    REQUIRE(itemRenderer.GetMetrics().drawCalls == 1U);
    REQUIRE(elapsedNanoseconds <= 500'000ULL);

    itemRenderer.Shutdown();
    atlas.Shutdown();
    frameRenderer.Shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}