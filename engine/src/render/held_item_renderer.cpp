/**
 * @file held_item_renderer.cpp
 * @brief Implements procedural first-person hand and tool rendering.
 *
 * @details Uses camera-local cuboids for the six shipped tool families, avoiding a file-backed
 * model dependency while retaining atlas materials and a deterministic held-input swing.
 */

#include "voxels/render/held_item_renderer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace voxels::graphics {
namespace {

void AddQuad(std::vector<HeldItemVertex>& vertices, const std::array<glm::vec3, 4>& positions,
             const glm::vec3& normal, int layer, const glm::vec4& tint) {
    constexpr std::array<glm::vec2, 4> uvs = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    for (const std::size_t index : {0U, 1U, 2U, 0U, 2U, 3U}) {
        vertices.push_back({positions[index], normal, uvs[index], tint, static_cast<float>(layer)});
    }
}

void AddBox(std::vector<HeldItemVertex>& vertices, const glm::mat4& transform, glm::vec3 center,
            glm::vec3 halfExtent, int layer, const glm::vec4& tint) {
    const std::array<glm::vec3, 8> points = {{
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
    }};
    std::array<glm::vec3, 8> transformed{};
    for (std::size_t index = 0; index < points.size(); ++index) {
        transformed[index] = transform * glm::vec4(center + points[index] * halfExtent, 1.0f);
    }
    const std::array<std::array<std::size_t, 4>, 6> faces = {{{{1, 0, 3, 2}}, {{4, 5, 6, 7}},
        {{3, 7, 6, 2}}, {{0, 1, 5, 4}}, {{5, 1, 2, 6}}, {{0, 4, 7, 3}}}};
    const std::array<glm::vec3, 6> normals = {{{0, 0, -1}, {0, 0, 1}, {0, 1, 0},
        {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}}};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        const auto& indices = faces[face];
        AddQuad(vertices, {transformed[indices[0]], transformed[indices[1]], transformed[indices[2]], transformed[indices[3]]},
                glm::mat3(transform) * normals[face], layer, tint);
    }
}

bool Named(std::string_view name, std::string_view needle) {
    return name.find(needle) != std::string_view::npos;
}

} // namespace

std::vector<HeldItemVertex> BuildHeldItemMesh(const gameplay::ItemStack& held,
                                               const BlockRegistry& registry, const TextureAtlas& atlas,
                                               bool swinging, float elapsedSeconds) {
    std::vector<HeldItemVertex> vertices;
    vertices.reserve(360);
    const float swing = swinging ? std::sin(elapsedSeconds * 9.0f) : 0.0f;
    glm::mat4 transform{1.0f};
    transform = glm::translate(transform, {0.34f + swing * 0.08f, -0.48f - std::abs(swing) * 0.13f, -0.72f});
    transform = glm::rotate(transform, glm::radians(-24.0f + swing * 42.0f), {0.0f, 0.0f, 1.0f});
    transform = glm::rotate(transform, glm::radians(18.0f - swing * 18.0f), {1.0f, 0.0f, 0.0f});

    const int armLayer = atlas.LayerFor("blocks/dirt");
    AddBox(vertices, transform, {0.02f, -0.03f, 0.0f}, {0.095f, 0.26f, 0.09f}, armLayer,
           {1.0f, 0.72f, 0.52f, 1.0f});
    if (held.IsEmpty()) return vertices;

    const BlockDefinition* definition = registry.GetDefinition(held.blockId);
    if (definition == nullptr) return vertices;
    const int layer = atlas.LayerFor(definition->GetFaceTexture(Face::PosY));
    const glm::vec4 tint{definition->tintColor[0], definition->tintColor[1], definition->tintColor[2], definition->tintColor[3]};
    const std::string_view name = definition->name;
    AddBox(vertices, transform, {0.00f, 0.21f, -0.03f}, {0.035f, 0.34f, 0.035f}, layer, tint);
    if (Named(name, "pickaxe")) {
        AddBox(vertices, transform, {0.0f, 0.53f, -0.03f}, {0.25f, 0.055f, 0.055f}, layer, tint);
    } else if (Named(name, "axe")) {
        AddBox(vertices, transform, {-0.11f, 0.51f, -0.03f}, {0.16f, 0.13f, 0.055f}, layer, tint);
    } else if (Named(name, "shovel")) {
        AddBox(vertices, transform, {0.0f, 0.53f, -0.03f}, {0.13f, 0.16f, 0.05f}, layer, tint);
    } else if (Named(name, "sledgehammer")) {
        AddBox(vertices, transform, {0.0f, 0.54f, -0.03f}, {0.25f, 0.10f, 0.10f}, layer, tint);
    } else if (Named(name, "sword")) {
        AddBox(vertices, transform, {0.0f, 0.57f, -0.03f}, {0.10f, 0.32f, 0.035f}, layer, tint);
    } else if (Named(name, "hoe")) {
        AddBox(vertices, transform, {0.12f, 0.51f, -0.03f}, {0.20f, 0.05f, 0.05f}, layer, tint);
    } else {
        AddBox(vertices, transform, {0.0f, 0.42f, -0.03f}, {0.17f, 0.17f, 0.17f}, layer, tint);
    }
    return vertices;
}

HeldItemRenderer::~HeldItemRenderer() { Shutdown(); }

bool HeldItemRenderer::EnsureResources() {
    if (m_program != 0) return true;
    if (glCreateShader == nullptr) return false;
    constexpr const char* vertexSource = "#version 330 core\nlayout(location=0)in vec3 p;layout(location=1)in vec3 n;layout(location=2)in vec2 uv;layout(location=3)in vec4 tint;layout(location=4)in float layer;uniform mat4 projection;out vec2 t;out vec4 c;out float l;void main(){gl_Position=projection*vec4(p,1);t=uv;c=tint;l=layer;}";
    constexpr const char* fragmentSource = "#version 330 core\nin vec2 t;in vec4 c;in float l;uniform sampler2DArray atlas;out vec4 outColor;void main(){vec4 sampled=texture(atlas,vec3(t,l))*c;if(sampled.a<0.05)discard;outColor=sampled;}";
    const auto compile = [](GLenum stage, const char* source) {
        const GLuint shader = glCreateShader(stage); glShaderSource(shader, 1, &source, nullptr); glCompileShader(shader);
        GLint valid = GL_FALSE; glGetShaderiv(shader, GL_COMPILE_STATUS, &valid);
        if (valid == GL_FALSE) { glDeleteShader(shader); return GLuint{0}; } return shader;
    };
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) { if (vertex) glDeleteShader(vertex); if (fragment) glDeleteShader(fragment); return false; }
    m_program = glCreateProgram(); glAttachShader(m_program, vertex); glAttachShader(m_program, fragment); glLinkProgram(m_program);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint linked = GL_FALSE; glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) { glDeleteProgram(m_program); m_program = 0; return false; }
    glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_vbo); glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HeldItemVertex), reinterpret_cast<void*>(offsetof(HeldItemVertex, position)));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(HeldItemVertex), reinterpret_cast<void*>(offsetof(HeldItemVertex, normal)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(HeldItemVertex), reinterpret_cast<void*>(offsetof(HeldItemVertex, uv)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(HeldItemVertex), reinterpret_cast<void*>(offsetof(HeldItemVertex, tint)));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(HeldItemVertex), reinterpret_cast<void*>(offsetof(HeldItemVertex, atlasLayer)));
    m_projectionUniform = glGetUniformLocation(m_program, "projection"); m_textureUniform = glGetUniformLocation(m_program, "atlas");
    return true;
}

void HeldItemRenderer::Render(const Camera& camera, const gameplay::ItemStack& held,
                              const BlockRegistry& registry, const TextureAtlas& atlas,
                              bool swinging, float elapsedSeconds) {
    if (!EnsureResources()) return;
    const std::vector<HeldItemVertex> vertices = BuildHeldItemMesh(held, registry, atlas, swinging, elapsedSeconds);
    glUseProgram(m_program); glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(HeldItemVertex)), vertices.data(), GL_DYNAMIC_DRAW);
    const glm::mat4 projection = camera.Projection();
    glUniformMatrix4fv(m_projectionUniform, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1i(m_textureUniform, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, atlas.GetTextureHandle());
    glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glBindVertexArray(0); glUseProgram(0);
}

void HeldItemRenderer::Shutdown() {
    if (glDeleteBuffers != nullptr && m_vbo != 0) glDeleteBuffers(1, &m_vbo);
    if (glDeleteVertexArrays != nullptr && m_vao != 0) glDeleteVertexArrays(1, &m_vao);
    if (glDeleteProgram != nullptr && m_program != 0) glDeleteProgram(m_program);
    m_vbo = m_vao = m_program = 0;
}

} // namespace voxels::graphics