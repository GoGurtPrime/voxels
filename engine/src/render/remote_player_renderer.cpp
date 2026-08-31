/**
 * @file remote_player_renderer.cpp
 * @brief GL 3.3 implementation of the replicated-player body/head box renderer.
 */

#include "voxels/render/remote_player_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace voxels::graphics {
namespace {

struct BoxVertex {
    glm::vec3 position;
    glm::vec3 normal;
};

GLuint Compile(GLenum stage, const char* source) {
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint valid = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &valid);
    if (valid == GL_FALSE) { glDeleteShader(shader); return 0; }
    return shader;
}

void AppendBox(std::vector<BoxVertex>& vertices, glm::vec3 center, glm::vec3 halfExtents) {
    const glm::vec3 lo = center - halfExtents;
    const glm::vec3 hi = center + halfExtents;
    const std::array<std::array<glm::vec3, 4>, 6> faces = {{
        {{{hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}}}, // +X
        {{{lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}}}, // -X
        {{{lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}}}, // +Y
        {{{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}}}, // -Y
        {{{lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}}}, // +Z
        {{{lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z}}}, // -Z
    }};
    const std::array<glm::vec3, 6> normals = {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        const auto& p = faces[face];
        const glm::vec3 n = normals[face];
        vertices.push_back({p[0], n});
        vertices.push_back({p[1], n});
        vertices.push_back({p[2], n});
        vertices.push_back({p[0], n});
        vertices.push_back({p[2], n});
        vertices.push_back({p[3], n});
    }
}

glm::vec3 PlayerColor(std::uint32_t playerId) {
    // Deterministic hue per player id so each remote player is visually distinct.
    const float hue = static_cast<float>((playerId * 2654435761u) % 360u);
    const float c = 0.55f;
    const float x = c * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float m = 0.35f;
    if (hue < 60.0f) return {c + m, x + m, m};
    if (hue < 120.0f) return {x + m, c + m, m};
    if (hue < 180.0f) return {m, c + m, x + m};
    if (hue < 240.0f) return {m, x + m, c + m};
    if (hue < 300.0f) return {x + m, m, c + m};
    return {c + m, m, x + m};
}

} // namespace

RemotePlayerRenderer::~RemotePlayerRenderer() { Shutdown(); }

void RemotePlayerRenderer::Render(const Camera& camera, const std::vector<RemotePlayerVisual>& players,
                                  float deltaSeconds) {
    if (glCreateShader == nullptr) return;
    if (m_program == 0) {
        constexpr const char* vertexSource =
            "#version 330 core\nlayout(location=0) in vec3 p; layout(location=1) in vec3 n;"
            "uniform mat4 mvp; uniform mat4 model; out vec3 normal;"
            "void main(){gl_Position=mvp*vec4(p,1);normal=mat3(model)*n;}";
        constexpr const char* fragmentSource =
            "#version 330 core\nin vec3 normal; uniform vec3 tint; out vec4 outColor;"
            "void main(){float light=0.55+0.45*max(dot(normalize(normal),normalize(vec3(0.4,0.8,0.3))),0.0);"
            "outColor=vec4(tint*light,1.0);}";
        const GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
        const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (vertex == 0 || fragment == 0) {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            return;
        }
        m_program = glCreateProgram();
        glAttachShader(m_program, vertex);
        glAttachShader(m_program, fragment);
        glLinkProgram(m_program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = GL_FALSE;
        glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) { glDeleteProgram(m_program); m_program = 0; return; }

        // Body torso plus a smaller head and a nose marker showing facing direction, in local
        // space around the replicated position (the physics AABB center, 0.6 x 1.8 x 0.6).
        std::vector<BoxVertex> vertices;
        AppendBox(vertices, {0.0f, -0.25f, 0.0f}, {0.3f, 0.65f, 0.3f});
        AppendBox(vertices, {0.0f, 0.65f, 0.0f}, {0.25f, 0.25f, 0.25f});
        AppendBox(vertices, {0.0f, 0.65f, -0.29f}, {0.08f, 0.08f, 0.08f});
        m_vertexCount = static_cast<int>(vertices.size());
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(BoxVertex)),
                     vertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BoxVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BoxVertex),
                              reinterpret_cast<void*>(sizeof(glm::vec3)));
        glBindVertexArray(0);
    }
    if (m_program == 0 || players.empty()) {
        if (players.empty()) m_smoothed.clear();
        return;
    }

    for (auto& [id, smoothed] : m_smoothed) smoothed.seenThisFrame = false;
    const float blend = std::clamp(deltaSeconds * 12.0f, 0.0f, 1.0f);

    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    const glm::mat4 viewProjection = camera.ViewProjection();
    const GLint mvpLocation = glGetUniformLocation(m_program, "mvp");
    const GLint modelLocation = glGetUniformLocation(m_program, "model");
    const GLint tintLocation = glGetUniformLocation(m_program, "tint");
    for (const RemotePlayerVisual& player : players) {
        auto [entry, inserted] = m_smoothed.try_emplace(player.playerId,
                                                        SmoothedPlayer{player.position, player.yawRadians, true});
        SmoothedPlayer& smoothed = entry->second;
        if (!inserted) {
            smoothed.position += (player.position - smoothed.position) * blend;
            float yawDelta = player.yawRadians - smoothed.yawRadians;
            while (yawDelta > glm::pi<float>()) yawDelta -= glm::two_pi<float>();
            while (yawDelta < -glm::pi<float>()) yawDelta += glm::two_pi<float>();
            smoothed.yawRadians += yawDelta * blend;
        }
        smoothed.seenThisFrame = true;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), smoothed.position);
        model = glm::rotate(model, smoothed.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 mvp = viewProjection * model;
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        const glm::vec3 tint = PlayerColor(player.playerId);
        glUniform3fv(tintLocation, 1, glm::value_ptr(tint));
        glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    }
    glBindVertexArray(0);
    glUseProgram(0);
    std::erase_if(m_smoothed, [](const auto& pair) { return !pair.second.seenThisFrame; });
}

void RemotePlayerRenderer::Shutdown() {
    if (glDeleteProgram == nullptr) { m_program = 0; m_vao = 0; m_vbo = 0; return; }
    if (m_program != 0) { glDeleteProgram(m_program); m_program = 0; }
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_smoothed.clear();
}

} // namespace voxels::graphics
