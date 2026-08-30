/**
 * @file chunk_renderer.cpp
 * @brief GPU chunk mesh cache implementation: job-scheduled meshing, budgeted upload, culling.
 */

#include "voxels/render/chunk_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>

#include "voxels/core/logger.hpp"
#include "voxels/render/chunk_vertex.hpp"

namespace voxels::graphics {

namespace {

constexpr char kVertexShaderSource[] = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aFaceAoLight;
layout(location = 2) in float aAtlasLayer;
layout(location = 3) in vec4 aUvTint;

uniform mat4 uViewProj;
uniform vec3 uChunkOrigin;

out vec2 vUV;
out float vLayer;
out float vAO;
out float vSkyLight;
out float vBlockLight;
out float vBrightness;
out float vTint;
out vec3 vWorldPos;

const vec3 kFaceNormal[6] = vec3[6](
    vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0));
const float kFaceAmbient[6] = float[6](0.8, 0.8, 1.0, 0.5, 0.8, 0.8);

void main() {
    vec3 localPos = aPosition / 16.0;
    vec3 worldPos = uChunkOrigin + localPos;
    vWorldPos = worldPos;

    vUV = aUvTint.xy;
    vLayer = aAtlasLayer;
    vAO = aFaceAoLight.y;
    vSkyLight = aFaceAoLight.z;
    vBlockLight = aFaceAoLight.w;
    vTint = aUvTint.z;

    int faceIdx = int(aFaceAoLight.x + 0.5);
    vec3 sunDir = normalize(vec3(-0.6, 1.0, -0.5));
    float sunTerm = max(dot(kFaceNormal[faceIdx], sunDir), 0.0);
    vBrightness = kFaceAmbient[faceIdx] * (0.55 + 0.45 * sunTerm);

    gl_Position = uViewProj * vec4(worldPos, 1.0);
})";

constexpr char kFragmentShaderSource[] = R"(
#version 330 core
in vec2 vUV;
in float vLayer;
in float vAO;
in float vSkyLight;
in float vBlockLight;
in float vBrightness;
in float vTint;
in vec3 vWorldPos;

uniform sampler2DArray uTextureAtlas;
uniform vec3 uCameraPos;
uniform vec3 uFoliageTint;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTextureAtlas, vec3(vUV, vLayer));
    if (texColor.a < 0.05) {
        discard;
    }

    vec3 baseColor = texColor.rgb;
    if (vTint > 0.5) {
        baseColor *= uFoliageTint;
    }

    float aoFactor = 0.35 + 0.65 * (vAO / 3.0);
    float lightFactor = clamp(max(vSkyLight, vBlockLight) / 15.0, 0.15, 1.0);
    vec3 lit = baseColor * (aoFactor * lightFactor * vBrightness);

    float fog = clamp((length(vWorldPos - uCameraPos) - 24.0) / 48.0, 0.0, 1.0);
    vec3 skyTint = vec3(0.55, 0.70, 0.92);
    FragColor = vec4(mix(lit, skyTint, fog), texColor.a);
})";

bool CompileShader(GLenum type, const char* source, GLuint& outShader) {
    outShader = glCreateShader(type);
    glShaderSource(outShader, 1, &source, nullptr);
    glCompileShader(outShader);
    GLint success = 0;
    glGetShaderiv(outShader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(outShader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> infoLog(static_cast<std::size_t>(logLength) + 1U, '\0');
        glGetShaderInfoLog(outShader, logLength, nullptr, infoLog.data());
        voxels::Logger logger;
        logger.Error(std::string("Chunk shader compile failed: ") + infoLog.data());
        glDeleteShader(outShader);
        outShader = 0;
        return false;
    }
    return true;
}

bool LinkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint& outProgram) {
    outProgram = glCreateProgram();
    glAttachShader(outProgram, vertexShader);
    glAttachShader(outProgram, fragmentShader);
    glLinkProgram(outProgram);
    GLint status = 0;
    glGetProgramiv(outProgram, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength = 0;
        glGetProgramiv(outProgram, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> infoLog(static_cast<std::size_t>(logLength) + 1U, '\0');
        glGetProgramInfoLog(outProgram, logLength, nullptr, infoLog.data());
        voxels::Logger logger;
        logger.Error(std::string("Chunk shader link failed: ") + infoLog.data());
        glDeleteProgram(outProgram);
        outProgram = 0;
        return false;
    }
    return true;
}

float DistanceSq(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = a - b;
    return glm::dot(d, d);
}

glm::vec3 ChunkOrigin(const voxels::ChunkCoordinate& coordinate, std::uint32_t chunkSize) {
    return {static_cast<float>(coordinate.x * static_cast<int>(chunkSize)),
            static_cast<float>(coordinate.y * static_cast<int>(chunkSize)),
            static_cast<float>(coordinate.z * static_cast<int>(chunkSize))};
}

} // namespace

ChunkRenderer::ChunkRenderer(voxels::BlockRegistry& registry, voxels::TextureAtlas& atlas, voxels::JobSystem& jobSystem)
    : m_registry(registry), m_atlas(atlas), m_jobSystem(jobSystem) {}

ChunkRenderer::~ChunkRenderer() {
    Shutdown();
}

void ChunkRenderer::SetUploadBudget(std::uint32_t maxChunksPerFrame, double maxMilliseconds) noexcept {
    m_uploadBudgetChunksPerFrame = maxChunksPerFrame;
    m_uploadBudgetMilliseconds = maxMilliseconds;
}

void ChunkRenderer::MarkChunkDirty(const voxels::ChunkCoordinate& coordinate) {
    m_dirty.insert(coordinate);
}

void ChunkRenderer::MarkBlockEdited(const voxels::ChunkCoordinate& coordinate, const voxels::Vec3I& localEditPos,
                                    std::uint32_t chunkSize) {
    MarkChunkDirty(coordinate);
    const int size = static_cast<int>(chunkSize);
    if (localEditPos.x == 0) MarkChunkDirty({coordinate.x - 1, coordinate.y, coordinate.z});
    if (localEditPos.x == size - 1) MarkChunkDirty({coordinate.x + 1, coordinate.y, coordinate.z});
    if (localEditPos.y == 0) MarkChunkDirty({coordinate.x, coordinate.y - 1, coordinate.z});
    if (localEditPos.y == size - 1) MarkChunkDirty({coordinate.x, coordinate.y + 1, coordinate.z});
    if (localEditPos.z == 0) MarkChunkDirty({coordinate.x, coordinate.y, coordinate.z - 1});
    if (localEditPos.z == size - 1) MarkChunkDirty({coordinate.x, coordinate.y, coordinate.z + 1});
}

void ChunkRenderer::EnqueueDirtyMeshJobs(const voxels::World& world, const glm::vec3& cameraPosition) {
    m_chunkSize = world.GetChunkSize();

    std::vector<voxels::ChunkCoordinate> candidates(m_dirty.begin(), m_dirty.end());
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        return DistanceSq(ChunkOrigin(a, m_chunkSize), cameraPosition) <
               DistanceSq(ChunkOrigin(b, m_chunkSize), cameraPosition);
    });

    const auto& chunks = world.GetChunks();
    for (const auto& coordinate : candidates) {
        if (m_inFlight.contains(coordinate)) {
            continue;
        }
        const auto ownerIt = chunks.find(coordinate);
        if (ownerIt == chunks.end()) {
            continue;
        }

        ChunkNeighborhood neighborhood;
        static constexpr std::array<voxels::ChunkCoordinate, 6> kOffsets = {
            voxels::ChunkCoordinate{1, 0, 0}, voxels::ChunkCoordinate{-1, 0, 0},
            voxels::ChunkCoordinate{0, 1, 0}, voxels::ChunkCoordinate{0, -1, 0},
            voxels::ChunkCoordinate{0, 0, 1}, voxels::ChunkCoordinate{0, 0, -1}};
        std::array<std::shared_ptr<voxels::Chunk>, 6> neighborOwners{};
        for (std::size_t i = 0; i < kOffsets.size(); ++i) {
            const voxels::ChunkCoordinate neighborCoord{coordinate.x + kOffsets[i].x, coordinate.y + kOffsets[i].y,
                                                         coordinate.z + kOffsets[i].z};
            const auto neighborIt = chunks.find(neighborCoord);
            if (neighborIt != chunks.end()) {
                neighborOwners[i] = neighborIt->second;
                neighborhood.neighbors[i] = neighborOwners[i].get();
            }
        }

        std::shared_ptr<voxels::Chunk> owner = ownerIt->second;
        m_dirty.erase(coordinate);
        m_inFlight.insert(coordinate);

        voxels::BlockRegistry& registry = m_registry;
        voxels::TextureAtlas& atlas = m_atlas;
        m_jobSystem.Enqueue([this, coordinate, owner, neighborOwners, neighborhood, &registry, &atlas]() mutable {
            for (std::size_t i = 0; i < neighborOwners.size(); ++i) {
                neighborhood.neighbors[i] = neighborOwners[i].get();
            }
            ChunkMeshData data = BuildChunkMesh(*owner, neighborhood, registry, atlas);
            std::lock_guard<std::mutex> lock(m_completedMutex);
            m_completed.push_back(PendingMeshResult{coordinate, std::move(data)});
        });
    }
}

bool ChunkRenderer::EnsureProgram() {
    if (m_program != 0) {
        return true;
    }
    if (glCreateShader == nullptr) {
        return false; // no live GL context (headless unit test); callers degrade gracefully
    }
    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    if (!CompileShader(GL_VERTEX_SHADER, kVertexShaderSource, vertexShader)) {
        return false;
    }
    if (!CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource, fragmentShader)) {
        glDeleteShader(vertexShader);
        return false;
    }
    const bool linked = LinkProgram(vertexShader, fragmentShader, m_program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (!linked) {
        return false;
    }
    m_uniformViewProj = glGetUniformLocation(m_program, "uViewProj");
    m_uniformChunkOrigin = glGetUniformLocation(m_program, "uChunkOrigin");
    m_uniformCameraPos = glGetUniformLocation(m_program, "uCameraPos");
    m_uniformTexture = glGetUniformLocation(m_program, "uTextureAtlas");
    m_uniformFoliageTint = glGetUniformLocation(m_program, "uFoliageTint");
    return true;
}

void ChunkRenderer::ConfigureVertexAttributes() {
    if (glEnableVertexAttribArray == nullptr) {
        return;
    }
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(ChunkVertex),
                           reinterpret_cast<void*>(offsetof(ChunkVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(ChunkVertex),
                           reinterpret_cast<void*>(offsetof(ChunkVertex, faceIndex)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(ChunkVertex),
                           reinterpret_cast<void*>(offsetof(ChunkVertex, atlasLayer)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(ChunkVertex),
                           reinterpret_cast<void*>(offsetof(ChunkVertex, u)));
}

void ChunkRenderer::UploadMesh(const voxels::ChunkCoordinate& coordinate, ChunkMeshData&& data) {
    GpuChunkMesh& mesh = m_meshes[coordinate];

    const glm::vec3 origin = ChunkOrigin(coordinate, m_chunkSize);
    mesh.aabb.min = {static_cast<int>(origin.x), static_cast<int>(origin.y), static_cast<int>(origin.z)};
    mesh.aabb.max = {static_cast<int>(origin.x) + static_cast<int>(m_chunkSize),
                      static_cast<int>(origin.y) + static_cast<int>(m_chunkSize),
                      static_cast<int>(origin.z) + static_cast<int>(m_chunkSize)};
    mesh.opaqueIndexCount = data.opaqueIndexCount;
    mesh.transparentIndexCount = data.transparentIndexCount;
    mesh.provisional = data.provisional;

    if (glGenVertexArrays == nullptr) {
        // No live GL context (headless unit test): the mesh cache entry above still lets budget
        // and dirty-tracking logic be exercised without touching the driver.
        if (mesh.provisional) {
            m_dirty.insert(coordinate);
        }
        return;
    }

    if (mesh.vao == 0) {
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ibo);
    }

    glBindVertexArray(mesh.vao);

    const std::size_t vertexBytes = data.vertices.size() * sizeof(ChunkVertex);
    const std::size_t indexBytes = data.indices.size() * sizeof(std::uint32_t);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    if (vertexBytes > mesh.vboCapacityBytes) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexBytes), data.vertices.data(), GL_DYNAMIC_DRAW);
        mesh.vboCapacityBytes = vertexBytes;
    } else if (vertexBytes > 0) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertexBytes), data.vertices.data());
    }
    ConfigureVertexAttributes();

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    if (indexBytes > mesh.iboCapacityBytes) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexBytes), data.indices.data(), GL_DYNAMIC_DRAW);
        mesh.iboCapacityBytes = indexBytes;
    } else if (indexBytes > 0) {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indexBytes), data.indices.data());
    }

    glBindVertexArray(0);

    if (mesh.provisional) {
        // Neighbours were missing when this mesh was built; queue a re-mesh for when they load.
        m_dirty.insert(coordinate);
    }
}

void ChunkRenderer::UploadCompletedMeshes() {
    const auto start = std::chrono::steady_clock::now();

    std::vector<PendingMeshResult> batch;
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        batch.swap(m_completed);
    }

    std::uint32_t uploaded = 0;
    for (std::size_t i = 0; i < batch.size(); ++i) {
        m_inFlight.erase(batch[i].coordinate);

        const auto elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool overBudget = uploaded >= m_uploadBudgetChunksPerFrame || elapsedMs >= m_uploadBudgetMilliseconds;
        if (overBudget) {
            // Skipped this frame due to budget: its mesh data is discarded rather than cached,
            // and the coordinate goes back on the dirty queue to be re-meshed next pass (keeps
            // this simple and bounded - see Known Gaps in the work item 05 completion report).
            m_dirty.insert(batch[i].coordinate);
            continue;
        }

        EnsureProgram(); // best-effort; UploadMesh() degrades gracefully without a GL context
        UploadMesh(batch[i].coordinate, std::move(batch[i].data));
        ++uploaded;
    }

    m_metrics.lastUploadMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_metrics.meshedChunks = m_meshes.size();
    m_metrics.meshQueueDepth = m_dirty.size() + m_inFlight.size();
}

void ChunkRenderer::Render(const voxels::Camera& camera) {
    if (m_program == 0 || m_meshes.empty()) {
        m_metrics.visibleChunks = 0;
        m_metrics.drawCalls = 0;
        m_metrics.triangles = 0;
        return;
    }

    voxels::Frustum frustum;
    frustum.Update(camera.ViewProjection());

    std::vector<std::pair<const voxels::ChunkCoordinate*, GpuChunkMesh*>> visibleOpaque;
    std::vector<std::pair<const voxels::ChunkCoordinate*, GpuChunkMesh*>> visibleTransparent;
    for (auto& [coordinate, mesh] : m_meshes) {
        if (!frustum.Intersects(mesh.aabb)) {
            continue;
        }
        if (mesh.opaqueIndexCount > 0) {
            visibleOpaque.emplace_back(&coordinate, &mesh);
        }
        if (mesh.transparentIndexCount > 0) {
            visibleTransparent.emplace_back(&coordinate, &mesh);
        }
    }

    const auto distanceTo = [&](const voxels::ChunkCoordinate& c) {
        return DistanceSq(ChunkOrigin(c, m_chunkSize) + glm::vec3(static_cast<float>(m_chunkSize) * 0.5f), camera.position);
    };
    std::sort(visibleOpaque.begin(), visibleOpaque.end(), [&](const auto& a, const auto& b) {
        return distanceTo(*a.first) < distanceTo(*b.first);
    });
    std::sort(visibleTransparent.begin(), visibleTransparent.end(), [&](const auto& a, const auto& b) {
        return distanceTo(*a.first) > distanceTo(*b.first);
    });

    glUseProgram(m_program);
    const glm::mat4 viewProj = camera.ViewProjection();
    glUniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, &viewProj[0][0]);
    glUniform3fv(m_uniformCameraPos, 1, &camera.position[0]);
    glUniform3f(m_uniformFoliageTint, 0.45f, 0.75f, 0.35f);
    glUniform1i(m_uniformTexture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas.GetTextureHandle());

    std::size_t drawCalls = 0;
    std::size_t triangles = 0;

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    for (auto& [coordinate, mesh] : visibleOpaque) {
        const glm::vec3 origin = ChunkOrigin(*coordinate, m_chunkSize);
        glUniform3fv(m_uniformChunkOrigin, 1, &origin[0]);
        glBindVertexArray(mesh->vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->opaqueIndexCount), GL_UNSIGNED_INT, nullptr);
        ++drawCalls;
        triangles += mesh->opaqueIndexCount / 3;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (auto& [coordinate, mesh] : visibleTransparent) {
        const glm::vec3 origin = ChunkOrigin(*coordinate, m_chunkSize);
        glUniform3fv(m_uniformChunkOrigin, 1, &origin[0]);
        glBindVertexArray(mesh->vao);
        const void* offset = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(mesh->opaqueIndexCount) * sizeof(std::uint32_t));
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->transparentIndexCount), GL_UNSIGNED_INT, offset);
        ++drawCalls;
        triangles += mesh->transparentIndexCount / 3;
    }
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);

    m_metrics.visibleChunks = visibleOpaque.size() + visibleTransparent.size();
    m_metrics.drawCalls = drawCalls;
    m_metrics.triangles = triangles;
    m_metrics.loadedChunks = m_meshes.size();
}

void ChunkRenderer::ReleaseMesh(GpuChunkMesh& mesh) {
    if (mesh.vao != 0) glDeleteVertexArrays(1, &mesh.vao);
    if (mesh.vbo != 0) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.ibo != 0) glDeleteBuffers(1, &mesh.ibo);
    mesh = GpuChunkMesh{};
}

void ChunkRenderer::Shutdown() {
    for (auto& [coordinate, mesh] : m_meshes) {
        ReleaseMesh(mesh);
    }
    m_meshes.clear();
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

} // namespace voxels::graphics
