#include <catch2/catch_test_macros.hpp>

#include <array>

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/render/camera.hpp"

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

TEST_CASE("GLRenderer.InitializesAndRendersOffscreenFrame", "[render][gpu]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow(
        "Voxels Renderer Test",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        128,
        128,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);

    SDL_GLContext context = SDL_GL_CreateContext(window);
    REQUIRE(context != nullptr);
    REQUIRE(gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress)));

    voxels::graphics::GLRenderer renderer;
    REQUIRE(renderer.Initialize());
    REQUIRE(renderer.BeginFrame({0.2f, 0.3f, 0.4f, 1.0f}));
    REQUIRE(renderer.EndFrame());
    renderer.Shutdown();

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
