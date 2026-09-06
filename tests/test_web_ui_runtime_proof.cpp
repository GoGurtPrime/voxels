/**
 * @file test_web_ui_runtime_proof.cpp
 * @brief Hidden-window GPU readback proof for transparent web UI composition.
 */

#include <catch2/catch_test_macros.hpp>

#include <array>

#include <SDL.h>
#include <glad/glad.h>

#include "voxels/ui/web_ui_compositor.hpp"

TEST_CASE("WebUiRuntimeProof.TransparentSourcePreservesGameFramebuffer", "[web-ui][WebUiRuntimeProof]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("web-ui-alpha-proof", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    REQUIRE(context != nullptr);
    REQUIRE(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) != 0);
    glViewport(0, 0, 16, 16);
    glClearColor(0.0F, 0.0F, 1.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    {
        voxels::WebUiFrameQueue queue;
        const std::array<unsigned char, 16> pixels{0U, 0U, 128U, 128U, 0U, 0U, 128U, 128U,
                                                    0U, 0U, 128U, 128U, 0U, 0U, 128U, 128U};
        REQUIRE(queue.Submit(2, 2, pixels, {}));
        voxels::WebUiOpenGLCompositor compositor;
        REQUIRE(compositor.UploadAndComposite(queue));
        REQUIRE(compositor.LastUploadCount() == 1U);
        std::array<unsigned char, 4> pixel{};
        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        REQUIRE(pixel[0] >= 124U);
        REQUIRE(pixel[0] <= 132U);
        REQUIRE(pixel[1] <= 2U);
        REQUIRE(pixel[2] >= 123U);
        REQUIRE(pixel[2] <= 131U);
        REQUIRE(pixel[3] == 255U);
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}