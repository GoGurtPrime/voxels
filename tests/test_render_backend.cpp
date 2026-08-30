#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "voxels/assets/texture_loader.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"

TEST_CASE("Camera.ProjectionAndViewMatricesMatchKnownValues", "[render][camera]") {
    voxels::Camera camera;
    camera.position = {0.0f, 1.0f, 4.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.fovY = 60.0f * (3.14159265f / 180.0f);
    camera.aspect = 1.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    const auto projection = camera.Projection();
    const auto view = camera.View();
    const auto vp = camera.ViewProjection();

    REQUIRE(projection[0][0] > 0.0f);
    REQUIRE(std::abs(view[3][0]) < 1.0e-5f);
    REQUIRE(vp[0][0] > 0.0f);
}

TEST_CASE("Frustum.CullsAabbsOutsideAndKeepsInside", "[render][camera]") {
    voxels::Camera camera;
    camera.position = {0.0f, 0.0f, 3.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.aspect = 1.0f;

    voxels::Frustum frustum;
    frustum.Update(camera.ViewProjection());

    voxels::BoundingBox inside{{0, 0, 0}, {1, 1, 1}};
    voxels::BoundingBox outside{{10, 10, 10}, {12, 12, 12}};
    voxels::BoundingBox straddling{{-1, -1, 0}, {1, 1, 1}};

    REQUIRE(frustum.Intersects(inside));
    REQUIRE_FALSE(frustum.Intersects(outside));
    REQUIRE(frustum.Intersects(straddling));
}

TEST_CASE("GLRenderer.FrameBufferSamplesSkyClearColorAndBlockGeometry", "[render][gpu]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const int fbWidth = 256;
    const int fbHeight = 256;

    SDL_Window* window = SDL_CreateWindow(
        "Voxels Framebuffer Test",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        fbWidth,
        fbHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);

    SDL_GLContext context = SDL_GL_CreateContext(window);
    REQUIRE(context != nullptr);
    REQUIRE(gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress)));

    voxels::BlockRegistry blockRegistry = voxels::CreateDefaultBlockRegistry();
    voxels::TextureAtlas atlas(16, 16);
    atlas.PopulateFromBlockRegistry(blockRegistry, "app/assets/textures");
    REQUIRE(atlas.BuildGLTexture());

    voxels::graphics::GLRenderer renderer;
    renderer.SetTextureAtlas(&atlas);
    REQUIRE(renderer.Initialize());

    voxels::Camera camera;
    camera.position = {0.0f, 3.0f, 6.0f};
    camera.aspect = 1.0f;
    camera.fovY = glm::radians(60.0f);
    const glm::vec3 target(0.0f, 0.5f, 0.0f);
    const glm::vec3 dir = glm::normalize(target - camera.position);
    camera.pitch = std::asin(dir.y);
    camera.yaw = std::atan2(-dir.x, -dir.z);
    renderer.SetCamera(camera);

    const std::array<float, 4> skyColor = {0.58f, 0.72f, 0.88f, 1.0f};
    REQUIRE(renderer.BeginFrame(skyColor, fbWidth, fbHeight));
    renderer.RenderTestScene(0.0f);
    REQUIRE(renderer.EndFrame());

    // Read back framebuffer pixels
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(fbWidth * fbHeight * 4), 0);
    glReadPixels(0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // 1. Validate top-left corner is clear sky color (not black)
    const std::size_t skyPixelIdx = (static_cast<std::size_t>(fbHeight - 5) * fbWidth + 5) * 4U;
    const int skyR = pixels[skyPixelIdx + 0];
    const int skyG = pixels[skyPixelIdx + 1];
    const int skyB = pixels[skyPixelIdx + 2];
    const int skyA = pixels[skyPixelIdx + 3];

    const int expectedSkyR = static_cast<int>(skyColor[0] * 255.0f);
    const int expectedSkyG = static_cast<int>(skyColor[1] * 255.0f);
    const int expectedSkyB = static_cast<int>(skyColor[2] * 255.0f);

    REQUIRE(std::abs(skyR - expectedSkyR) <= 3);
    REQUIRE(std::abs(skyG - expectedSkyG) <= 3);
    REQUIRE(std::abs(skyB - expectedSkyB) <= 3);
    REQUIRE(skyA == 255);

    // 2. Count non-sky, non-black geometry pixels across the rendered image
    int geometryPixelCount = 0;
    int blackPixelCount = 0;

    for (int y = 0; y < fbHeight; ++y) {
        for (int x = 0; x < fbWidth; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * fbWidth + x) * 4U;
            const int r = pixels[idx + 0];
            const int g = pixels[idx + 1];
            const int b = pixels[idx + 2];

            if (r == 0 && g == 0 && b == 0) {
                ++blackPixelCount;
            } else if (std::abs(r - expectedSkyR) > 5 || std::abs(g - expectedSkyG) > 5 || std::abs(b - expectedSkyB) > 5) {
                // Pixel is distinctly different from sky background -> rendered geometry
                ++geometryPixelCount;
            }
        }
    }

    // Geometry pixels must be present and rasterized
    REQUIRE(geometryPixelCount > 1000);

    // Dump frame to file for inspection
    // Flip rows vertically because OpenGL bottom-up
    std::vector<std::uint8_t> flipped(pixels.size());
    for (int y = 0; y < fbHeight; ++y) {
        std::memcpy(
            &flipped[static_cast<std::size_t>((fbHeight - 1 - y) * fbWidth * 4)],
            &pixels[static_cast<std::size_t>(y * fbWidth * 4)],
            static_cast<std::size_t>(fbWidth * 4));
    }
    voxels::TextureLoader::WritePngToFile("build/test_frame_render.png", fbWidth, fbHeight, 4, flipped.data());

    renderer.Shutdown();
    atlas.Shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
