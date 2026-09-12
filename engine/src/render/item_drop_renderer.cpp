/**
 * @file item_drop_renderer.cpp
 * @brief Implements atlas-backed dropped-item geometry and OpenGL batching.
 *
 * @details Item transforms are derived from entity age each frame and never write back to the
 *          authoritative simulation. OpenGL 3.3 is the reference implementation per ADR-001.
 */

#include "voxels/render/item_drop_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include "voxels/core/logger.hpp"
#include "voxels/core/paths.hpp"

namespace voxels::graphics {
namespace {

constexpr float kDropHalfExtent = 0.16f;
constexpr float kIconHalfWidth = 0.22f;
constexpr float kIconHalfHeight = 0.22f;

glm::vec3 RotateAroundY(const glm::vec3& point, float yaw) {
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    return {point.x * cosine - point.z * sine, point.y, point.x * sine + point.z * cosine};
}

glm::vec3 FaceNormal(Face face) {
    switch (face) {
        case Face::PosX: return {1.0f, 0.0f, 0.0f};
        case Face::NegX: return {-1.0f, 0.0f, 0.0f};
        case Face::PosY: return {0.0f, 1.0f, 0.0f};
        case Face::NegY: return {0.0f, -1.0f, 0.0f};
        case Face::PosZ: return {0.0f, 0.0f, 1.0f};
        case Face::NegZ: return {0.0f, 0.0f, -1.0f};
    }
    return {0.0f, 1.0f, 0.0f};
}

void AddQuad(std::vector<ItemDropVertex>& vertices, const std::array<glm::vec3, 4>& positions,
             const glm::vec3& normal, int layer, const glm::vec4& tint, bool reverse = false) {
    constexpr std::array<glm::vec2, 4> kUvs = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    const std::array<std::size_t, 6> order = reverse
        ? std::array<std::size_t, 6>{0, 3, 2, 0, 2, 1}
        : std::array<std::size_t, 6>{0, 1, 2, 0, 2, 3};
    for (const std::size_t index : order) {
        vertices.push_back({positions[index], reverse ? -normal : normal, kUvs[index], tint,
                            static_cast<float>(layer)});
    }
}

glm::vec3 TransformPoint(const glm::vec3& local, const glm::vec3& center, float yaw, float scale) {
    return center + RotateAroundY(local * scale, yaw);
}

void AddCube(std::vector<ItemDropVertex>& vertices, const BlockDefinition& definition,
             const TextureAtlas& atlas, const glm::vec3& center, float yaw) {
    constexpr std::array<Face, 6> kFaces = {
        Face::PosX, Face::NegX, Face::PosY, Face::NegY, Face::PosZ, Face::NegZ};
    constexpr std::array<std::array<glm::vec3, 4>, 6> kPositions = {{
        {{{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}},
        {{{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}},
        {{{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}},
        {{{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}},
        {{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}},
        {{{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}},
    }};
    const glm::vec4 tint{definition.tintColor[0], definition.tintColor[1],
                         definition.tintColor[2], definition.tintColor[3]};
    for (std::size_t faceIndex = 0; faceIndex < kFaces.size(); ++faceIndex) {
        std::array<glm::vec3, 4> transformed{};
        for (std::size_t index = 0; index < transformed.size(); ++index) {
            transformed[index] = TransformPoint(kPositions[faceIndex][index], center, yaw, kDropHalfExtent);
        }
        AddQuad(vertices, transformed, RotateAroundY(FaceNormal(kFaces[faceIndex]), yaw),
                atlas.LayerFor(definition.GetFaceTexture(kFaces[faceIndex])), tint);
    }
}

void AddCrossedIcon(std::vector<ItemDropVertex>& vertices, const BlockDefinition& definition,
                    const TextureAtlas& atlas, const glm::vec3& center, float yaw) {
    const int layer = atlas.LayerFor(definition.GetFaceTexture(Face::PosY));
    const glm::vec4 tint{definition.tintColor[0], definition.tintColor[1],
                         definition.tintColor[2], definition.tintColor[3]};
    for (const float angleOffset : {-0.785398163f, 0.785398163f}) {
        const float angle = yaw + angleOffset;
        const glm::vec3 horizontal = RotateAroundY({kIconHalfWidth, 0.0f, 0.0f}, angle);
        const glm::vec3 vertical{0.0f, kIconHalfHeight, 0.0f};
        const std::array<glm::vec3, 4> points = {
            center - horizontal - vertical, center + horizontal - vertical,
            center + horizontal + vertical, center - horizontal + vertical};
        const glm::vec3 normal = glm::normalize(glm::cross(points[1] - points[0], points[2] - points[0]));
        AddQuad(vertices, points, normal, layer, tint);
        AddQuad(vertices, points, normal, layer, tint, true);
    }
}

void AddModel(std::vector<ItemDropVertex>& vertices, const BakedModelMesh& model,
              const BlockDefinition& definition, const TextureAtlas& atlas,
              const glm::vec3& center, float yaw) {
    const glm::vec4 tint{definition.tintColor[0], definition.tintColor[1],
                         definition.tintColor[2], definition.tintColor[3]};
    constexpr float kModelScale = kDropHalfExtent * 2.0f;
    for (std::size_t triangle = 0; triangle < model.indices.size(); triangle += 3U) {
        for (const std::size_t corner : {0U, 2U, 1U}) {
            const ChunkVertex& source = model.vertices[model.indices[triangle + corner]];
            const glm::vec3 local{
                static_cast<float>(source.x) / kChunkVertexPositionScale - 0.5f,
                static_cast<float>(source.y) / kChunkVertexPositionScale - 0.5f,
                static_cast<float>(source.z) / kChunkVertexPositionScale - 0.5f};
            vertices.push_back({TransformPoint(local, center, yaw, kModelScale),
                                RotateAroundY(FaceNormal(static_cast<Face>(source.faceIndex)), yaw),
                                {static_cast<float>(source.u), static_cast<float>(source.v)}, tint,
                                static_cast<float>(atlas.LayerFor(definition.GetFaceTexture(
                                    static_cast<Face>(source.faceIndex))))});
        }
    }
}

bool CompileShader(GLenum type, const char* source, GLuint& shader) {
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return true;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    Logger logger;
    logger.Error(std::string("Item drop shader compile failed: ") + log.data());
    glDeleteShader(shader);
    shader = 0;
    return false;
}

} // namespace

ItemDropRenderBatch BuildItemDropRenderBatch(const std::vector<gameplay::ItemDrop>& drops,
                                              const BlockRegistry& registry,
                                              const TextureAtlas& atlas,
                                              const ModelRegistry* models) {
    ItemDropRenderBatch batch;
    batch.opaque.reserve(drops.size() * 36U);
    for (const gameplay::ItemDrop& drop : drops) {
        const BlockDefinition* definition = registry.GetDefinition(drop.stack.blockId);
        if (definition == nullptr) continue;
        const float yaw = drop.age * 1.1f;
        const float bob = std::sin(drop.age * 2.4f) * 0.05f;
        const glm::vec3 center{drop.position.x, drop.position.y + bob, drop.position.z};
        std::vector<ItemDropVertex>* destination = &batch.opaque;
        if (!definition->isPlaceable) destination = &batch.cutout;
        else if (definition->isTransparent || !definition->isOpaque) destination = &batch.transparent;

        if (!definition->isPlaceable) {
            AddCrossedIcon(*destination, *definition, atlas, center, yaw);
        } else if (definition->renderType == "model" && definition->modelId.has_value() && models != nullptr) {
            if (const BakedModelMesh* model = models->FindMesh(*definition->modelId); model != nullptr) {
                AddModel(*destination, *model, *definition, atlas, center, yaw);
            } else {
                AddCube(*destination, *definition, atlas, center, yaw);
            }
        } else {
            AddCube(*destination, *definition, atlas, center, yaw);
        }
    }
    return batch;
}

ItemDropRenderer::ItemDropRenderer(BlockRegistry& registry, TextureAtlas& atlas)
    : m_registry(registry), m_atlas(atlas) {
    m_models.LoadReferencedModels(m_registry, Paths::AssetsDir());
}

ItemDropRenderer::~ItemDropRenderer() {
    Shutdown();
}

bool ItemDropRenderer::EnsureResources() {
    if (m_program != 0) return true;
    if (glCreateShader == nullptr) return false;
    constexpr const char* vertexSource = R"(
#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aTint;
layout(location=4) in float aLayer;
uniform mat4 uViewProjection;
uniform vec3 uSunDirection;
out vec2 vUv;
out vec4 vTint;
out float vLayer;
out float vSun;
void main() {
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
    vUv = aUv;
    vTint = aTint;
    vLayer = aLayer;
    vSun = max(dot(normalize(aNormal), normalize(uSunDirection)), 0.0);
})";
    constexpr const char* fragmentSource = R"(
#version 330 core
in vec2 vUv;
in vec4 vTint;
in float vLayer;
in float vSun;
uniform sampler2DArray uTextureAtlas;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
out vec4 outColor;
void main() {
    vec4 sampled = texture(uTextureAtlas, vec3(vUv, vLayer)) * vTint;
    if (sampled.a < 0.05) discard;
    vec3 lighting = max(uAmbientColor + uSunColor * vSun, vec3(0.08));
    outColor = vec4(sampled.rgb * lighting, sampled.a);
})";
    GLuint vertex = 0;
    GLuint fragment = 0;
    if (!CompileShader(GL_VERTEX_SHADER, vertexSource, vertex) ||
        !CompileShader(GL_FRAGMENT_SHADER, fragmentSource, fragment)) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ItemDropVertex),
                          reinterpret_cast<void*>(offsetof(ItemDropVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ItemDropVertex),
                          reinterpret_cast<void*>(offsetof(ItemDropVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ItemDropVertex),
                          reinterpret_cast<void*>(offsetof(ItemDropVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropVertex),
                          reinterpret_cast<void*>(offsetof(ItemDropVertex, tint)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ItemDropVertex),
                          reinterpret_cast<void*>(offsetof(ItemDropVertex, atlasLayer)));
    m_viewProjectionUniform = glGetUniformLocation(m_program, "uViewProjection");
    m_sunDirectionUniform = glGetUniformLocation(m_program, "uSunDirection");
    m_sunColorUniform = glGetUniformLocation(m_program, "uSunColor");
    m_ambientColorUniform = glGetUniformLocation(m_program, "uAmbientColor");
    m_textureUniform = glGetUniformLocation(m_program, "uTextureAtlas");
    return true;
}

void ItemDropRenderer::DrawBatch(const std::vector<ItemDropVertex>& vertices, bool blend, bool cull) {
    if (vertices.empty()) return;
    if (blend) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(ItemDropVertex)),
                 vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    ++m_metrics.drawCalls;
    m_metrics.triangles += vertices.size() / 3U;
}

void ItemDropRenderer::Render(const Camera& camera, const std::vector<gameplay::ItemDrop>& drops) {
    m_metrics = {};
    if (drops.empty() || !EnsureResources()) return;
    for (const gameplay::ItemDrop& drop : drops) {
        if (m_registry.GetDefinition(drop.stack.blockId) == nullptr &&
            m_reportedMissingItems.insert(drop.stack.blockId).second) {
            Logger logger;
            logger.Warn("Dropped item id " + std::to_string(drop.stack.blockId) +
                        " has no catalogue material and will not be rendered.");
        }
    }
    std::vector<gameplay::ItemDrop> sortedDrops = drops;
    std::stable_sort(sortedDrops.begin(), sortedDrops.end(), [&](const auto& left, const auto& right) {
        const glm::vec3 leftPosition{left.position.x, left.position.y, left.position.z};
        const glm::vec3 rightPosition{right.position.x, right.position.y, right.position.z};
        const glm::vec3 leftDelta = leftPosition - camera.position;
        const glm::vec3 rightDelta = rightPosition - camera.position;
        return glm::dot(leftDelta, leftDelta) > glm::dot(rightDelta, rightDelta);
    });
    const auto started = std::chrono::steady_clock::now();
    const ItemDropRenderBatch batch = BuildItemDropRenderBatch(sortedDrops, m_registry, m_atlas, &m_models);
    m_metrics.cpuBuildMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    m_metrics.visibleDrops = drops.size();

    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const glm::mat4 viewProjection = camera.ViewProjection();
    glUniformMatrix4fv(m_viewProjectionUniform, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform3fv(m_sunDirectionUniform, 1, &m_lighting.sunDirection.x);
    glUniform3fv(m_sunColorUniform, 1, &m_lighting.sunColor.x);
    glUniform3fv(m_ambientColorUniform, 1, &m_lighting.ambientColor.x);
    glUniform1i(m_textureUniform, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas.GetTextureHandle());

    DrawBatch(batch.opaque, false, true);
    DrawBatch(batch.cutout, false, false);
    DrawBatch(batch.transparent, true, false);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

void ItemDropRenderer::Shutdown() {
    if (glDeleteBuffers != nullptr && m_vbo != 0) glDeleteBuffers(1, &m_vbo);
    if (glDeleteVertexArrays != nullptr && m_vao != 0) glDeleteVertexArrays(1, &m_vao);
    if (glDeleteProgram != nullptr && m_program != 0) glDeleteProgram(m_program);
    m_vbo = 0;
    m_vao = 0;
    m_program = 0;
}

} // namespace voxels::graphics