/**
 * @file underwater_overlay.cpp
 * @brief OpenGL 3.3 implementation of the full-screen underwater tint pass.
 */

#include "voxels/render/underwater_overlay.hpp"

#include <algorithm>
#include <vector>

#include "voxels/core/logger.hpp"

namespace voxels::graphics {

namespace {

constexpr char kVertexShaderSource[] = R"(
#version 330 core
const vec2 kVertices[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
void main() {
    gl_Position = vec4(kVertices[gl_VertexID], 0.0, 1.0);
})";

constexpr char kFragmentShaderSource[] = R"(
#version 330 core
uniform vec3 uTintColor;
uniform float uTintAlpha;
out vec4 FragColor;
void main() {
    FragColor = vec4(uTintColor, uTintAlpha);
})";

bool CompileShader(GLenum type, const char* source, GLuint& shader) {
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return true;

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(static_cast<std::size_t>(logLength) + 1U, '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    Logger{}.Error(std::string("Underwater overlay shader compile failed: ") + log.data());
    glDeleteShader(shader);
    shader = 0;
    return false;
}

} // namespace

UnderwaterOverlay::~UnderwaterOverlay() {
    Shutdown();
}

bool UnderwaterOverlay::EnsureProgram() {
    if (m_program != 0) return true;
    if (glCreateShader == nullptr) return false;

    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    if (!CompileShader(GL_VERTEX_SHADER, kVertexShaderSource, vertexShader) ||
        !CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource, fragmentShader)) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, fragmentShader);
    glLinkProgram(m_program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_tintColorUniform = glGetUniformLocation(m_program, "uTintColor");
    m_tintAlphaUniform = glGetUniformLocation(m_program, "uTintAlpha");
    glGenVertexArrays(1, &m_vertexArray);
    return true;
}

void UnderwaterOverlay::Render(float submersionFraction) {
    if (!EnsureProgram()) return;

    // Blue-green absorption tint; alpha ramps with submersion but stays readable even at 100%.
    constexpr Vec3 kTintColor{0.02f, 0.16f, 0.32f};
    const float alpha = std::clamp(submersionFraction, 0.0f, 1.0f) * 0.55f;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_program);
    glUniform3fv(m_tintColorUniform, 1, &kTintColor[0]);
    glUniform1f(m_tintAlphaUniform, alpha);
    glBindVertexArray(m_vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void UnderwaterOverlay::Shutdown() noexcept {
    if (m_vertexArray != 0 && glDeleteVertexArrays != nullptr) glDeleteVertexArrays(1, &m_vertexArray);
    if (m_program != 0 && glDeleteProgram != nullptr) glDeleteProgram(m_program);
    m_program = 0;
    m_vertexArray = 0;
}

} // namespace voxels::graphics
