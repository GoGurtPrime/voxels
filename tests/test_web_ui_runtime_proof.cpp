/**
 * @file test_web_ui_runtime_proof.cpp
 * @brief Hidden-window GPU readback proof for transparent web UI composition.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

#include <SDL.h>
#include <glad/glad.h>

namespace {
GLuint CompileShader(const GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) { glDeleteShader(shader); return 0U; }
    return shader;
}
}

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
    constexpr const char* vertexSource = "#version 330 core\nconst vec2 p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));void main(){gl_Position=vec4(p[gl_VertexID],0,1);}";
    constexpr const char* fragmentSource = "#version 330 core\nout vec4 color;void main(){color=vec4(1,0,0,0.5);}";
    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    REQUIRE(vertex != 0U);
    REQUIRE(fragment != 0U);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    REQUIRE(linked == GL_TRUE);
    glViewport(0, 0, 16, 16);
    glClearColor(0.0F, 0.0F, 1.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLuint vertexArray = 0U;
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    glUseProgram(program);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::array<unsigned char, 4> pixel{};
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    REQUIRE(pixel[0] == Catch::Approx(128).margin(4));
    REQUIRE(pixel[1] == Catch::Approx(0).margin(2));
    REQUIRE(pixel[2] == Catch::Approx(127).margin(4));
    REQUIRE(pixel[3] == 255U);
    glDeleteVertexArrays(1, &vertexArray);
    glDeleteProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}