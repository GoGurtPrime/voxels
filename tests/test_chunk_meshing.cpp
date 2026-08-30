/**
 * @file test_chunk_meshing.cpp
 * @brief Automated regression tests for the chunk mesher and GPU chunk render cache.
 *
 * @details Reference work_items/05_chunk_mesh_pipeline_and_world_rendering.md §6. Every test
 *          asserts a real, observable side effect of the mesh data (vertex/index counts, AO
 *          values, dirty-set membership) rather than merely that a function returns.
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "voxels/assets/texture_loader.hpp"
#include "voxels/core/job_system.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/render/chunk_mesher.hpp"
#include "voxels/render/chunk_renderer.hpp"
#include "voxels/render/gameplay_hud.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/chunk.hpp"
#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/world.hpp"

namespace {

voxels::TextureAtlas MakeAtlas(const voxels::BlockRegistry& registry) {
    voxels::TextureAtlas atlas(16, 16);
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");
    atlas.BuildGLTexture(); // safe headless: only builds the deterministic layer table if no GL context
    return atlas;
}

} // namespace

TEST_CASE("Mesher.SingleBlockProducesSixQuads", "[render][mesher]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);

    voxels::Chunk chunk({0, 0, 0}, 3, 3, 3);
    chunk.SetBlock(1, 1, 1, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    voxels::graphics::ChunkNeighborhood neighborhood; // all null: block is fully interior
    const auto mesh = voxels::graphics::BuildChunkMesh(chunk, neighborhood, registry, atlas);

    REQUIRE(mesh.transparentIndexCount == 0);
    REQUIRE(mesh.opaqueIndexCount == 36); // 6 quads * 2 triangles * 3 indices
    REQUIRE(mesh.vertices.size() == 24);  // 6 quads * 4 vertices, unmerged (isolated faces)
    REQUIRE_FALSE(mesh.provisional);
}

TEST_CASE("Mesher.SolidChunkProducesOnlyBoundaryFaces", "[render][mesher]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);

    constexpr std::uint32_t kSize = 4;
    voxels::Chunk chunk({0, 0, 0}, kSize, kSize, kSize);
    for (std::uint32_t z = 0; z < kSize; ++z)
        for (std::uint32_t y = 0; y < kSize; ++y)
            for (std::uint32_t x = 0; x < kSize; ++x)
                chunk.SetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                                static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    // All-air neighbours on every side so every boundary face is known-exposed.
    voxels::Chunk airPosX({1, 0, 0}, kSize, kSize, kSize);
    voxels::Chunk airNegX({-1, 0, 0}, kSize, kSize, kSize);
    voxels::Chunk airPosY({0, 1, 0}, kSize, kSize, kSize);
    voxels::Chunk airNegY({0, -1, 0}, kSize, kSize, kSize);
    voxels::Chunk airPosZ({0, 0, 1}, kSize, kSize, kSize);
    voxels::Chunk airNegZ({0, 0, -1}, kSize, kSize, kSize);

    voxels::graphics::ChunkNeighborhood neighborhood;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::PosX)] = &airPosX;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::NegX)] = &airNegX;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::PosY)] = &airPosY;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::NegY)] = &airNegY;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::PosZ)] = &airPosZ;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::NegZ)] = &airNegZ;

    const auto mesh = voxels::graphics::BuildChunkMesh(chunk, neighborhood, registry, atlas);

    // A fully solid, uniformly-lit cube greedy-merges each of its 6 faces into exactly one quad.
    REQUIRE_FALSE(mesh.provisional);
    REQUIRE(mesh.opaqueIndexCount == 36); // 6 faces * 1 merged quad each * 6 indices
    REQUIRE(mesh.vertices.size() == 24);
}

TEST_CASE("Mesher.GreedyMergingReducesQuadCountForFlatPlane", "[render][mesher]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);

    constexpr std::uint32_t kPlaneSize = 8;
    voxels::Chunk chunk({0, 0, 0}, kPlaneSize, 1, kPlaneSize);
    for (std::uint32_t z = 0; z < kPlaneSize; ++z)
        for (std::uint32_t x = 0; x < kPlaneSize; ++x)
            chunk.SetBlock(static_cast<int>(x), 0, static_cast<int>(z),
                            static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    voxels::Chunk airAbove({0, 1, 0}, kPlaneSize, 1, kPlaneSize);
    voxels::Chunk airBelow({0, -1, 0}, kPlaneSize, 1, kPlaneSize);

    voxels::graphics::ChunkNeighborhood neighborhood;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::PosY)] = &airAbove;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::NegY)] = &airBelow;
    // X/Z neighbours intentionally left unknown: side faces are not part of this assertion.

    const auto mesh = voxels::graphics::BuildChunkMesh(chunk, neighborhood, registry, atlas);

    // Without merging, an 8x8 flat plane would produce 64 top quads + 64 bottom quads. Greedy
    // merging collapses the whole uniform top and bottom faces into exactly 2 quads total.
    REQUIRE(mesh.opaqueIndexCount == 12); // 2 quads * 6 indices
    REQUIRE(mesh.vertices.size() == 8);   // 2 quads * 4 vertices
}

TEST_CASE("Mesher.NeighbourAwarenessCullsSharedBoundaryFaces", "[render][mesher]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);

    constexpr std::uint32_t kSize = 4;
    const auto makeSolid = [&](voxels::ChunkCoordinate coord) {
        voxels::Chunk chunk(coord, kSize, kSize, kSize);
        for (std::uint32_t z = 0; z < kSize; ++z)
            for (std::uint32_t y = 0; y < kSize; ++y)
                for (std::uint32_t x = 0; x < kSize; ++x)
                    chunk.SetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                                    static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        return chunk;
    };

    voxels::Chunk center = makeSolid({0, 0, 0});
    voxels::Chunk posX = makeSolid({1, 0, 0});
    voxels::Chunk negX = makeSolid({-1, 0, 0});
    voxels::Chunk posY = makeSolid({0, 1, 0});
    voxels::Chunk negY = makeSolid({0, -1, 0});
    voxels::Chunk posZ = makeSolid({0, 0, 1});
    voxels::Chunk negZ = makeSolid({0, 0, -1});

    voxels::graphics::ChunkNeighborhood neighborhood;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::PosX)] = &posX;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::NegX)] = &negX;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::PosY)] = &posY;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::NegY)] = &negY;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::PosZ)] = &posZ;
    neighborhood.neighbors[static_cast<std::size_t>(voxels::Face::NegZ)] = &negZ;

    const auto mesh = voxels::graphics::BuildChunkMesh(center, neighborhood, registry, atlas);

    // Every neighbour is known and equally solid/opaque, so all 6 shared boundary planes cull:
    // no faces at all, and definitely no "wall of faces" at the seams.
    REQUIRE_FALSE(mesh.provisional);
    REQUIRE(mesh.opaqueIndexCount == 0);
    REQUIRE(mesh.transparentIndexCount == 0);
    REQUIRE(mesh.vertices.empty());
}

TEST_CASE("Mesher.TransparentBlocksGoToTransparentRangeOnly", "[render][mesher]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);

    voxels::Chunk chunk({0, 0, 0}, 3, 3, 3);
    chunk.SetBlock(1, 1, 1, static_cast<voxels::BlockId>(voxels::BlockType::Glass));

    voxels::graphics::ChunkNeighborhood neighborhood;
    const auto mesh = voxels::graphics::BuildChunkMesh(chunk, neighborhood, registry, atlas);

    REQUIRE(mesh.opaqueIndexCount == 0);
    REQUIRE(mesh.transparentIndexCount == 36);
    REQUIRE(mesh.vertices.size() == 24);
}

TEST_CASE("Mesher.SideTexturesKeepAuthoredTopAtWorldTop", "[render][mesher][uv]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);
    voxels::Chunk chunk({0, 0, 0}, 3, 3, 3);
    chunk.SetBlock(1, 1, 1, static_cast<voxels::BlockId>(voxels::BlockType::Grass));

    const auto mesh = voxels::graphics::BuildChunkMesh(chunk, {}, registry, atlas);
    for (const voxels::Face face : {voxels::Face::PosX, voxels::Face::NegX, voxels::Face::PosZ, voxels::Face::NegZ}) {
        std::uint16_t minX = UINT16_MAX, maxX = 0, minY = UINT16_MAX, maxY = 0, minZ = UINT16_MAX, maxZ = 0;
        for (const auto& vertex : mesh.vertices) {
            if (vertex.faceIndex == static_cast<std::uint8_t>(face)) {
                minX = std::min(minX, vertex.x);
                maxX = std::max(maxX, vertex.x);
                minY = std::min(minY, vertex.y);
                maxY = std::max(maxY, vertex.y);
                minZ = std::min(minZ, vertex.z);
                maxZ = std::max(maxZ, vertex.z);
            }
        }
        for (const auto& vertex : mesh.vertices) {
            if (vertex.faceIndex == static_cast<std::uint8_t>(face)) {
                if (face == voxels::Face::PosX || face == voxels::Face::NegX) {
                    REQUIRE(vertex.u == (vertex.z - minZ) / static_cast<std::uint16_t>(voxels::graphics::kChunkVertexPositionScale));
                } else {
                    REQUIRE(vertex.u == (vertex.x - minX) / static_cast<std::uint16_t>(voxels::graphics::kChunkVertexPositionScale));
                }
                REQUIRE(vertex.v == (maxY - vertex.y) / static_cast<std::uint16_t>(voxels::graphics::kChunkVertexPositionScale));
            }
        }
    }
}

TEST_CASE("Mesher.AmbientOcclusionValuesMatchExpectedCornerCases", "[render][mesher]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);

    // A single stone block at (1,0,1) exposes a top face at the y=1 boundary. Corner 0 of that
    // face samples occluders at (x=1,z=0,y=1) [side1], (x=0,z=1,y=1) [side2], and (x=0,z=0,y=1)
    // [diagonal corner], all at the same height as the exposed air above the block.
    struct Case {
        const char* name;
        bool side1;
        bool side2;
        bool corner;
        std::uint8_t expectedAO;
    };
    const std::array<Case, 4> cases = {{
        {"no occluders", false, false, false, 3},
        {"side1 only", true, false, false, 2},
        {"corner only", false, false, true, 2},
        {"both sides (corner irrelevant)", true, true, false, 0},
    }};

    for (const auto& testCase : cases) {
        INFO(testCase.name);
        voxels::Chunk chunk({0, 0, 0}, 3, 2, 3);
        chunk.SetBlock(1, 0, 1, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        if (testCase.side1) chunk.SetBlock(1, 1, 0, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        if (testCase.side2) chunk.SetBlock(0, 1, 1, static_cast<voxels::BlockId>(voxels::BlockType::Stone));
        if (testCase.corner) chunk.SetBlock(0, 1, 0, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

        voxels::graphics::ChunkNeighborhood neighborhood;
        const auto mesh = voxels::graphics::BuildChunkMesh(chunk, neighborhood, registry, atlas);

        // The block also exposes side faces (its X/Z neighbours are open air); isolate the one
        // top (PosY) quad and inspect its first corner, which is the one under test here.
        int topFaceVertexIndex = -1;
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            if (mesh.vertices[i].faceIndex == static_cast<std::uint8_t>(voxels::Face::PosY)) {
                topFaceVertexIndex = static_cast<int>(i);
                break;
            }
        }
        REQUIRE(topFaceVertexIndex >= 0);
        REQUIRE(mesh.vertices[static_cast<std::size_t>(topFaceVertexIndex)].ao == testCase.expectedAO);
    }
}

TEST_CASE("Mesher.IsDeterministicForIdenticalChunkData", "[render][mesher]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);

    constexpr std::uint32_t kSize = 6;
    const auto buildChunk = [&] {
        voxels::Chunk chunk({0, 0, 0}, kSize, kSize, kSize);
        for (std::uint32_t z = 0; z < kSize; ++z) {
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const std::uint32_t h = 1 + ((x * 3 + z * 5) % 3);
                for (std::uint32_t y = 0; y < h; ++y) {
                    chunk.SetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                                    static_cast<voxels::BlockId>(voxels::BlockType::Stone));
                }
            }
        }
        return chunk;
    };

    voxels::Chunk chunkA = buildChunk();
    voxels::Chunk chunkB = buildChunk();
    voxels::graphics::ChunkNeighborhood neighborhood;

    const auto meshA = voxels::graphics::BuildChunkMesh(chunkA, neighborhood, registry, atlas);
    const auto meshB = voxels::graphics::BuildChunkMesh(chunkB, neighborhood, registry, atlas);

    REQUIRE(meshA.indices == meshB.indices);
    REQUIRE(meshA.opaqueIndexCount == meshB.opaqueIndexCount);
    REQUIRE(meshA.transparentIndexCount == meshB.transparentIndexCount);
    REQUIRE(meshA.vertices.size() == meshB.vertices.size());
    for (std::size_t i = 0; i < meshA.vertices.size(); ++i) {
        REQUIRE(meshA.vertices[i] == meshB.vertices[i]);
    }
}

TEST_CASE("ChunkRenderer.BlockEditMarksOwnerAndAffectedNeighbours", "[render][chunkrenderer]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);
    voxels::JobSystem jobSystem(1);
    voxels::graphics::ChunkRenderer renderer(registry, atlas, jobSystem);

    // Interior edit: only the owning chunk is dirtied.
    renderer.MarkBlockEdited({0, 0, 0}, {8, 8, 8}, 16);
    REQUIRE(renderer.GetDirtyCount() == 1);
    REQUIRE(renderer.IsDirty({0, 0, 0}));

    voxels::graphics::ChunkRenderer rendererCorner(registry, atlas, jobSystem);
    // Corner edit (x=0,y=0,z=0 local): owner + 3 neighbours = 4 chunks dirtied.
    rendererCorner.MarkBlockEdited({2, 2, 2}, {0, 0, 0}, 16);
    REQUIRE(rendererCorner.GetDirtyCount() == 4);
    REQUIRE(rendererCorner.IsDirty({2, 2, 2}));
    REQUIRE(rendererCorner.IsDirty({1, 2, 2}));
    REQUIRE(rendererCorner.IsDirty({2, 1, 2}));
    REQUIRE(rendererCorner.IsDirty({2, 2, 1}));
}

TEST_CASE("ChunkRenderer.UploadBudgetIsRespected", "[render][chunkrenderer]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);
    voxels::JobSystem jobSystem(2);
    voxels::World world;

    constexpr int kBudget = 5;
    voxels::graphics::ChunkRenderer renderer(registry, atlas, jobSystem);
    renderer.SetUploadBudget(kBudget, 1000.0); // generous time budget: only the count caps here

    for (int i = 0; i < 100; ++i) {
        voxels::ChunkCoordinate coord{i, 0, 0};
        world.GetOrCreateChunk(coord); // all-air chunk is enough to produce a completed mesh job
        renderer.MarkChunkDirty(coord);
    }
    REQUIRE(renderer.GetDirtyCount() == 100);

    renderer.EnqueueDirtyMeshJobs(world, glm::vec3(0.0f));
    jobSystem.Shutdown(); // block until every enqueued mesh job has completed

    renderer.UploadCompletedMeshes();
    REQUIRE(renderer.GetResidentMeshCount() == static_cast<std::size_t>(kBudget));
    REQUIRE(renderer.GetDirtyCount() == 100 - kBudget);
}

TEST_CASE("ChunkRenderer.EditMeshesBypassTheBackgroundUploadBudget", "[render][chunkrenderer][edit]") {
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas = MakeAtlas(registry);
    voxels::JobSystem jobSystem(1);
    voxels::World world;
    world.GetOrCreateChunk({0, 0, 0});
    world.GetOrCreateChunk({1, 0, 0});

    voxels::graphics::ChunkRenderer renderer(registry, atlas, jobSystem);
    renderer.SetUploadBudget(0, 0.0);
    renderer.MarkChunkDirty({0, 0, 0});
    renderer.MarkBlockEdited({1, 0, 0}, {8, 8, 8}, 16);
    renderer.EnqueueDirtyMeshJobs(world, glm::vec3(0.0f));
    jobSystem.Shutdown();
    renderer.UploadCompletedMeshes();

    REQUIRE_FALSE(renderer.HasMesh({0, 0, 0}));
    REQUIRE(renderer.HasMesh({1, 0, 0}));
}

TEST_CASE("ChunkRenderer.RendersGeneratedChunkToOffscreenTarget", "[render][chunkrenderer][gpu]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const int fbWidth = 256;
    const int fbHeight = 256;
    SDL_Window* window = SDL_CreateWindow("Voxels ChunkRenderer Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           fbWidth, fbHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    REQUIRE(context != nullptr);
    REQUIRE(gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress)));

    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas(16, 16);
    atlas.PopulateFromBlockRegistry(registry, "app/assets/textures");
    REQUIRE(atlas.BuildGLTexture());

    voxels::graphics::GLRenderer glRenderer;
    glRenderer.SetTextureAtlas(&atlas);
    REQUIRE(glRenderer.Initialize());

    voxels::WorldOptions options;
    options.seed = 777u;
    voxels::WorldGenerator generator(options);
    voxels::World world;
    for (int cz = -1; cz <= 1; ++cz) {
        for (int cx = -1; cx <= 1; ++cx) {
            for (int cy = 0; cy < 3; ++cy) {
                const voxels::ChunkCoordinate coord{cx, cy, cz};
                world.GetOrCreateChunk(coord) = generator.GenerateChunk(coord);
            }
            world.GetOrCreateChunk({cx, 3, cz}); // all-air cap chunk
        }
    }

    voxels::JobSystem jobSystem(2);
    voxels::graphics::ChunkRenderer chunkRenderer(registry, atlas, jobSystem);
    for (const auto& [coordinate, chunk] : world.GetChunks()) {
        (void)chunk;
        chunkRenderer.MarkChunkDirty(coordinate);
    }

    voxels::Camera camera;
    camera.position = {8.0f, 55.0f, 45.0f};
    camera.aspect = 1.0f;
    camera.fovY = glm::radians(70.0f);
    camera.farPlane = 300.0f;
    const glm::vec3 target(8.0f, 30.0f, 8.0f);
    const glm::vec3 dir = glm::normalize(target - camera.position);
    camera.pitch = std::asin(dir.y);
    camera.yaw = std::atan2(-dir.x, -dir.z);

    chunkRenderer.EnqueueDirtyMeshJobs(world, camera.position);
    jobSystem.Shutdown(); // wait for every mesh job so this frame renders the whole 3x3 area
    chunkRenderer.SetUploadBudget(1000, 10000.0);
    chunkRenderer.UploadCompletedMeshes();
    REQUIRE(chunkRenderer.GetResidentMeshCount() == 36); // 9 columns * 4 vertical sections (incl. cap)

    const std::array<float, 4> skyColor = {0.58f, 0.72f, 0.88f, 1.0f};
    REQUIRE(glRenderer.BeginFrame(skyColor, fbWidth, fbHeight));
    chunkRenderer.Render(camera);
    voxels::graphics::GameplayHudRenderer hudRenderer;
    voxels::gameplay::Inventory inventory;
    hudRenderer.Render(camera, {}, 0.0f, inventory, "", 0.0f, {});
    REQUIRE(glRenderer.EndFrame());

    // The HUD is a later render pass. It must not leak GL state into the next terrain frame.
    REQUIRE(glRenderer.BeginFrame(skyColor, fbWidth, fbHeight));
    chunkRenderer.Render(camera);
    REQUIRE(glRenderer.EndFrame());

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(fbWidth * fbHeight * 4), 0);
    glReadPixels(0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    const int expectedSkyR = static_cast<int>(skyColor[0] * 255.0f);
    const int expectedSkyG = static_cast<int>(skyColor[1] * 255.0f);
    const int expectedSkyB = static_cast<int>(skyColor[2] * 255.0f);

    int geometryPixelCount = 0;
    for (int y = 0; y < fbHeight; ++y) {
        for (int x = 0; x < fbWidth; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * fbWidth + x) * 4U;
            const int r = pixels[idx + 0];
            const int g = pixels[idx + 1];
            const int b = pixels[idx + 2];
            if (std::abs(r - expectedSkyR) > 5 || std::abs(g - expectedSkyG) > 5 || std::abs(b - expectedSkyB) > 5) {
                ++geometryPixelCount;
            }
        }
    }
    // Terrain fills most of the view looking down from above spawn; this is not a background-only frame.
    REQUIRE(geometryPixelCount > 2000);
    REQUIRE(chunkRenderer.GetMetrics().drawCalls > 0);
    REQUIRE(chunkRenderer.GetMetrics().triangles > 0);

    std::vector<std::uint8_t> flipped(pixels.size());
    for (int y = 0; y < fbHeight; ++y) {
        std::memcpy(&flipped[static_cast<std::size_t>((fbHeight - 1 - y) * fbWidth * 4)],
                    &pixels[static_cast<std::size_t>(y * fbWidth * 4)], static_cast<std::size_t>(fbWidth * 4));
    }
    const bool dumped = voxels::TextureLoader::WritePngToFile("build/test_chunk_render.png", fbWidth, fbHeight, 4, flipped.data());
    REQUIRE(dumped);

    hudRenderer.Shutdown();
    chunkRenderer.Shutdown();
    glRenderer.Shutdown();
    atlas.Shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
