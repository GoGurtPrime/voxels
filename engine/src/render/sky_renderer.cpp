/**
 * @file sky_renderer.cpp
 * @brief OpenGL 3.3 procedural sky implementation.
 *
 * @details Draws a camera-relative full-screen triangle prior to chunk rendering, matching
 *          the celestial lighting sampled from the server-owned world clock.
 */

#include "voxels/render/sky_renderer.hpp"

#include <algorithm>
#include <vector>

#include "voxels/core/logger.hpp"

namespace voxels::graphics {

namespace {

constexpr char kSkyVertexShaderSource[] = R"(
#version 330 core
out vec2 vNdc;
const vec2 kVertices[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
void main() {
    vNdc = kVertices[gl_VertexID];
    gl_Position = vec4(vNdc, 0.0, 1.0);
})";

constexpr char kSkyFragmentShaderSource[] = R"(
#version 330 core
in vec2 vNdc;
uniform mat4 uInverseViewProjection;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
out vec4 FragColor;
void main() {
    vec4 farPoint = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 viewDirection = normalize(farPoint.xyz / farPoint.w - uCameraPosition);
    float height = clamp(viewDirection.y * 0.95 + 0.5, 0.0, 1.0);
    vec3 color = mix(uHorizonColor, uZenithColor, smoothstep(0.05, 0.82, height));
    float sun = pow(max(dot(viewDirection, normalize(uSunDirection)), 0.0), 768.0);
    color += uSunColor * sun;
    FragColor = vec4(color, 1.0);
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
    Logger{}.Error(std::string("Sky shader compile failed: ") + log.data());
    glDeleteShader(shader);
    shader = 0;
    return false;
}

} // namespace

FogRange FogRangeForRenderDistance(int renderDistanceChunks) noexcept {
    const float loadedBoundary = static_cast<float>(std::max(renderDistanceChunks, 2)) * kChunkWidthBlocks;
    const float end = std::max(kChunkWidthBlocks, loadedBoundary - kChunkWidthBlocks);
    return {std::max(0.0f, end * 0.60f), end};
}

Vec3 SkyHorizonColor(const CelestialLighting& lighting) noexcept {
    const float daylight = std::clamp((glm::length(lighting.skyColor) - 0.08f) / 1.15f, 0.0f, 1.0f);
    return glm::mix(lighting.skyColor * Vec3{0.30f, 0.42f, 0.70f}, Vec3{0.36f, 0.68f, 1.00f}, daylight);
}

SkyRenderer::~SkyRenderer() {
    Shutdown();
}

bool SkyRenderer::EnsureProgram() {
    if (m_program != 0) return true;
    if (glCreateShader == nullptr) return false;

    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    if (!CompileShader(GL_VERTEX_SHADER, kSkyVertexShaderSource, vertexShader) ||
        !CompileShader(GL_FRAGMENT_SHADER, kSkyFragmentShaderSource, fragmentShader)) {
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

    m_inverseViewProjectionUniform = glGetUniformLocation(m_program, "uInverseViewProjection");
    m_zenithColorUniform = glGetUniformLocation(m_program, "uZenithColor");
    m_horizonColorUniform = glGetUniformLocation(m_program, "uHorizonColor");
    m_cameraPositionUniform = glGetUniformLocation(m_program, "uCameraPosition");
    m_sunDirectionUniform = glGetUniformLocation(m_program, "uSunDirection");
    m_sunColorUniform = glGetUniformLocation(m_program, "uSunColor");
    glGenVertexArrays(1, &m_vertexArray);
    return true;
}

void SkyRenderer::Render(const Camera& camera, const CelestialLighting& lighting) {
    if (!EnsureProgram()) return;

    const glm::mat4 inverseViewProjection = glm::inverse(camera.ViewProjection());
    const float daylight = std::clamp((glm::length(lighting.skyColor) - 0.08f) / 1.15f, 0.0f, 1.0f);
    const Vec3 horizon = SkyHorizonColor(lighting);
    const Vec3 zenith = glm::mix(lighting.skyColor * Vec3{0.40f, 0.52f, 0.82f}, Vec3{0.08f, 0.38f, 0.92f}, daylight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glUseProgram(m_program);
    glUniformMatrix4fv(m_inverseViewProjectionUniform, 1, GL_FALSE, &inverseViewProjection[0][0]);
    glUniform3fv(m_zenithColorUniform, 1, &zenith[0]);
    glUniform3fv(m_horizonColorUniform, 1, &horizon[0]);
    glUniform3fv(m_cameraPositionUniform, 1, &camera.position[0]);
    glUniform3fv(m_sunDirectionUniform, 1, &lighting.sunDirection[0]);
    glUniform3fv(m_sunColorUniform, 1, &lighting.sunColor[0]);
    glBindVertexArray(m_vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void SkyRenderer::Shutdown() noexcept {
    if (m_vertexArray != 0 && glDeleteVertexArrays != nullptr) glDeleteVertexArrays(1, &m_vertexArray);
    if (m_program != 0 && glDeleteProgram != nullptr) glDeleteProgram(m_program);
    m_program = 0;
    m_vertexArray = 0;
}

} // namespace voxels::graphics