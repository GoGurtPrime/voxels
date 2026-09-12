/**
 * @file chunk_renderer.cpp
 * @brief GPU chunk mesh cache implementation: job-scheduled meshing, budgeted upload, culling.
 */

#include "voxels/render/chunk_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "voxels/core/logger.hpp"
#include "voxels/render/chunk_vertex.hpp"

#if defined(_WIN32)
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif

namespace voxels::graphics {

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;

struct DX11ChunkState {
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Texture2D> atlasTexture;
    ComPtr<ID3D11ShaderResourceView> atlasView;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> opaqueRasterizer;
    ComPtr<ID3D11RasterizerState> transparentRasterizer;
    ComPtr<ID3D11DepthStencilState> opaqueDepth;
    ComPtr<ID3D11DepthStencilState> transparentDepth;
    ComPtr<ID3D11BlendState> opaqueBlend;
    ComPtr<ID3D11BlendState> alphaBlend;
};
#else
struct DX11ChunkState {};
#endif

namespace {

constexpr char kVertexShaderSource[] = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aFaceAoLight;
layout(location = 2) in float aAtlasLayer;
layout(location = 3) in vec4 aUvTint;

uniform mat4 uViewProj;
uniform vec3 uChunkOrigin;
uniform vec3 uSunDirection;

out vec2 vUV;
out float vLayer;
out float vAO;
out float vSkyLight;
out float vBlockLight;
out float vSunTerm;
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
    vSunTerm = max(dot(kFaceNormal[faceIdx], normalize(uSunDirection)), 0.0) * kFaceAmbient[faceIdx];

    gl_Position = uViewProj * vec4(worldPos, 1.0);
})";

constexpr char kFragmentShaderSource[] = R"(
#version 330 core
in vec2 vUV;
in float vLayer;
in float vAO;
in float vSkyLight;
in float vBlockLight;
in float vSunTerm;
in float vTint;
in vec3 vWorldPos;

uniform sampler2DArray uTextureAtlas;
uniform vec3 uCameraPos;
uniform vec3 uFoliageTint;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uSkyColor;
uniform float uFogStart;
uniform float uFogEnd;

out vec4 FragColor;

void main() {
    // Greedy quads use whole-tile UVs. Explicit wrapping keeps per-tile sampling stable even
    // if external GL code changes the atlas sampler state.
    vec4 texColor = texture(uTextureAtlas, vec3(fract(vUV), vLayer));
    if (texColor.a < 0.05) {
        discard;
    }

    vec3 baseColor = texColor.rgb;
    if (vTint > 0.5) {
        baseColor *= uFoliageTint;
    }

    float aoFactor = 0.35 + 0.65 * (vAO / 3.0);
    float skyFactor = clamp(vSkyLight / 15.0, 0.0, 1.0);
    float blockFactor = clamp(vBlockLight / 15.0, 0.0, 1.0);
    vec3 skyLighting = skyFactor * (uAmbientColor + uSunColor * vSunTerm);
    vec3 blockLighting = blockFactor * vec3(1.0, 0.58, 0.28);
    vec3 lit = baseColor * aoFactor * max(skyLighting + blockLighting, vec3(0.015));

    float fog = smoothstep(uFogStart, uFogEnd, length(vWorldPos.xz - uCameraPos.xz));
    FragColor = vec4(mix(lit, uSkyColor, fog), texColor.a);
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

ChunkRenderer::ChunkRenderer(voxels::BlockRegistry& registry, voxels::TextureAtlas& atlas, voxels::JobSystem& jobSystem,
                             IGraphicsRenderer* renderer)
    : m_registry(registry), m_atlas(atlas), m_jobSystem(jobSystem), m_renderer(renderer) {
    m_models.LoadReferencedModels(m_registry, "assets");
}

ChunkRenderer::~ChunkRenderer() {
    Shutdown();
}

void ChunkRenderer::SetUploadBudget(std::uint32_t maxChunksPerFrame, double maxMilliseconds) noexcept {
    m_uploadBudgetChunksPerFrame = maxChunksPerFrame;
    m_uploadBudgetMilliseconds = maxMilliseconds;
}

void ChunkRenderer::SetBackgroundMeshQueueLimit(std::size_t maxJobs) noexcept {
    m_backgroundMeshQueueLimit = maxJobs;
}

void ChunkRenderer::MarkChunkDirty(const voxels::ChunkCoordinate& coordinate) {
    ++m_revisions[coordinate];
    m_dirty.insert(coordinate);
}

void ChunkRenderer::MarkChunkDirtyForEdit(const voxels::ChunkCoordinate& coordinate) {
    MarkChunkDirty(coordinate);
    if (m_editPriority.insert(coordinate).second) {
        m_editRequestedAt.emplace(coordinate, std::chrono::steady_clock::now());
    }
}

void ChunkRenderer::MarkBlockEdited(const voxels::ChunkCoordinate& coordinate, const voxels::Vec3I& localEditPos,
                                    std::uint32_t chunkSize) {
    MarkChunkDirtyForEdit(coordinate);
    const int size = static_cast<int>(chunkSize);
    if (localEditPos.x == 0) MarkChunkDirtyForEdit({coordinate.x - 1, coordinate.y, coordinate.z});
    if (localEditPos.x == size - 1) MarkChunkDirtyForEdit({coordinate.x + 1, coordinate.y, coordinate.z});
    if (localEditPos.y == 0) MarkChunkDirtyForEdit({coordinate.x, coordinate.y - 1, coordinate.z});
    if (localEditPos.y == size - 1) MarkChunkDirtyForEdit({coordinate.x, coordinate.y + 1, coordinate.z});
    if (localEditPos.z == 0) MarkChunkDirtyForEdit({coordinate.x, coordinate.y, coordinate.z - 1});
    if (localEditPos.z == size - 1) MarkChunkDirtyForEdit({coordinate.x, coordinate.y, coordinate.z + 1});
}

void ChunkRenderer::OnChunkArrived(const voxels::ChunkCoordinate& coordinate, const voxels::World& world) {
    static constexpr std::array<voxels::ChunkCoordinate, 6> kOffsets = {
        voxels::ChunkCoordinate{1, 0, 0}, voxels::ChunkCoordinate{-1, 0, 0},
        voxels::ChunkCoordinate{0, 1, 0}, voxels::ChunkCoordinate{0, -1, 0},
        voxels::ChunkCoordinate{0, 0, 1}, voxels::ChunkCoordinate{0, 0, -1}};
    m_knownResidentChunks.insert(coordinate);
    MarkChunkDirty(coordinate);
    for (const ChunkCoordinate& offset : kOffsets) {
        const ChunkCoordinate neighbor{coordinate.x + offset.x, coordinate.y + offset.y, coordinate.z + offset.z};
        if (world.HasChunk(neighbor)) MarkChunkDirty(neighbor);
    }
}

void ChunkRenderer::OnChunkRemoved(const voxels::ChunkCoordinate& coordinate, const voxels::World& world) {
    static constexpr std::array<voxels::ChunkCoordinate, 6> kOffsets = {
        voxels::ChunkCoordinate{1, 0, 0}, voxels::ChunkCoordinate{-1, 0, 0},
        voxels::ChunkCoordinate{0, 1, 0}, voxels::ChunkCoordinate{0, -1, 0},
        voxels::ChunkCoordinate{0, 0, 1}, voxels::ChunkCoordinate{0, 0, -1}};
    m_knownResidentChunks.erase(coordinate);
    if (const auto mesh = m_meshes.find(coordinate); mesh != m_meshes.end()) {
        ReleaseMesh(mesh->second);
        m_meshes.erase(mesh);
    }
    m_dirty.erase(coordinate);
    m_editPriority.erase(coordinate);
    m_editRequestedAt.erase(coordinate);
    for (const ChunkCoordinate& offset : kOffsets) {
        const ChunkCoordinate neighbor{coordinate.x + offset.x, coordinate.y + offset.y, coordinate.z + offset.z};
        if (world.HasChunk(neighbor)) MarkChunkDirty(neighbor);
    }
}

void ChunkRenderer::EnqueueDirtyMeshJobs(const voxels::World& world, const glm::vec3& cameraPosition) {
    m_chunkSize = world.GetChunkSize();
    static constexpr std::array<voxels::ChunkCoordinate, 6> kOffsets = {
        voxels::ChunkCoordinate{1, 0, 0}, voxels::ChunkCoordinate{-1, 0, 0},
        voxels::ChunkCoordinate{0, 1, 0}, voxels::ChunkCoordinate{0, -1, 0},
        voxels::ChunkCoordinate{0, 0, 1}, voxels::ChunkCoordinate{0, 0, -1}};
    const auto& chunks = world.GetChunks();
    if (m_knownResidentChunks.empty()) {
        for (const auto& [coordinate, chunk] : chunks) {
            (void)chunk;
            OnChunkArrived(coordinate, world);
        }
    }
    while (true) {
        if (m_backgroundInFlight.size() >= m_backgroundMeshQueueLimit && m_editPriority.empty()) {
            break;
        }
        auto selected = m_dirty.end();
        for (auto candidate = m_dirty.begin(); candidate != m_dirty.end(); ++candidate) {
            if (!chunks.contains(*candidate) || m_inFlight.contains(*candidate)) continue;
            if (selected == m_dirty.end() ||
                (m_editPriority.contains(*candidate) && !m_editPriority.contains(*selected)) ||
                (m_editPriority.contains(*candidate) == m_editPriority.contains(*selected) &&
                 DistanceSq(ChunkOrigin(*candidate, m_chunkSize), cameraPosition) <
                     DistanceSq(ChunkOrigin(*selected, m_chunkSize), cameraPosition))) {
                selected = candidate;
            }
        }
        if (selected == m_dirty.end()) break;
        const voxels::ChunkCoordinate coordinate = *selected;
        if (m_inFlight.contains(coordinate)) {
            continue;
        }
        const bool isEditPriority = m_editPriority.contains(coordinate);
        if (!isEditPriority && m_backgroundInFlight.size() >= m_backgroundMeshQueueLimit) {
            break;
        }
        const auto ownerIt = chunks.find(coordinate);
        if (ownerIt == chunks.end()) {
            continue;
        }

        ChunkNeighborhood neighborhood;
        std::array<std::shared_ptr<voxels::Chunk>, 6> neighborSnapshots{};
        for (std::size_t i = 0; i < kOffsets.size(); ++i) {
            const voxels::ChunkCoordinate neighborCoord{coordinate.x + kOffsets[i].x, coordinate.y + kOffsets[i].y,
                                                         coordinate.z + kOffsets[i].z};
            const auto neighborIt = chunks.find(neighborCoord);
            if (neighborIt != chunks.end()) {
                neighborSnapshots[i] = std::make_shared<voxels::Chunk>(*neighborIt->second);
                neighborhood.neighbors[i] = neighborSnapshots[i].get();
            }
        }

        const std::shared_ptr<voxels::Chunk> ownerSnapshot = std::make_shared<voxels::Chunk>(*ownerIt->second);
        const std::uint64_t revision = m_revisions[coordinate];
        m_dirty.erase(coordinate);
        m_inFlight.insert(coordinate);
        if (!isEditPriority) {
            m_backgroundInFlight.insert(coordinate);
        }

        voxels::BlockRegistry& registry = m_registry;
        voxels::TextureAtlas& atlas = m_atlas;
        const ModelRegistry& models = m_models;
        const JobPriority priority = isEditPriority ? JobPriority::High : JobPriority::Normal;
        m_jobSystem.Enqueue([this, coordinate, revision, ownerSnapshot, neighborSnapshots, neighborhood, isEditPriority,
                             &registry, &atlas, &models]() mutable {
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < neighborSnapshots.size(); ++i) {
                neighborhood.neighbors[i] = neighborSnapshots[i].get();
            }
            ChunkMeshData data = BuildChunkMesh(*ownerSnapshot, neighborhood, registry, atlas, &models);
            const double meshingMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::lock_guard<std::mutex> lock(m_completedMutex);
            m_completed.push_back(PendingMeshResult{coordinate, std::move(data), revision, isEditPriority,
                                                    meshingMilliseconds});
        }, priority);
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
    m_uniformSunDirection = glGetUniformLocation(m_program, "uSunDirection");
    m_uniformSunColor = glGetUniformLocation(m_program, "uSunColor");
    m_uniformAmbientColor = glGetUniformLocation(m_program, "uAmbientColor");
    m_uniformSkyColor = glGetUniformLocation(m_program, "uSkyColor");
    m_uniformFogStart = glGetUniformLocation(m_program, "uFogStart");
    m_uniformFogEnd = glGetUniformLocation(m_program, "uFogEnd");
    return true;
}

bool ChunkRenderer::EnsureDX11Resources() {
#if defined(_WIN32)
    if (m_dx11 != nullptr) return true;
    if (m_renderer == nullptr || m_renderer->GetBackend() != RendererBackend::Direct3D11) return false;
    auto* device = static_cast<ID3D11Device*>(m_renderer->GetNativeDevice());
    if (device == nullptr) return false;

    constexpr char vertexSource[] = R"(
cbuffer FrameConstants : register(b0) {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
    float4 cameraPosition;
    float4 foliageTint;
    float4 sunDirection;
    float4 sunColor;
    float4 ambientColor;
    float4 skyColor;
    float4 fogRange;
};
struct VSInput {
    uint4 packedPosition : POSITION;
    uint2 light : LIGHT;
    uint atlasLayer : ATLAS;
    uint4 uvTint : UVTINT;
};
struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation uint layer : TEXCOORD2;
    float ao : TEXCOORD3;
    float skyLight : TEXCOORD4;
    float blockLight : TEXCOORD5;
    float sunTerm : TEXCOORD6;
    float tint : TEXCOORD7;
};
VSOutput main(VSInput input) {
    const float3 normals[6] = {
        float3(1,0,0), float3(-1,0,0), float3(0,1,0),
        float3(0,-1,0), float3(0,0,1), float3(0,0,-1)
    };
    const float ambient[6] = {0.8, 0.8, 1.0, 0.5, 0.8, 0.8};
    VSOutput output;
    uint face = input.packedPosition.w & 255;
    uint ao = input.packedPosition.w >> 8;
    output.worldPosition = chunkOrigin.xyz + float3(input.packedPosition.xyz) / 16.0;
    output.position = mul(viewProjection, float4(output.worldPosition, 1.0));
    output.position.z = output.position.z * 0.5 + output.position.w * 0.5;
    output.uv = float2(input.uvTint.xy);
    output.layer = input.atlasLayer;
    output.ao = float(ao);
    output.skyLight = float(input.light.x);
    output.blockLight = float(input.light.y);
    float sun = max(dot(normals[min(face, 5)], normalize(sunDirection.xyz)), 0.0);
    output.sunTerm = ambient[min(face, 5)] * sun;
    output.tint = float(input.uvTint.z);
    return output;
})";
    constexpr char pixelSource[] = R"(
Texture2DArray atlas : register(t0);
SamplerState atlasSampler : register(s0);
cbuffer FrameConstants : register(b0) {
    column_major float4x4 viewProjection;
    float4 chunkOrigin;
    float4 cameraPosition;
    float4 foliageTint;
    float4 sunDirection;
    float4 sunColor;
    float4 ambientColor;
    float4 skyColor;
    float4 fogRange;
};
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation uint layer : TEXCOORD2;
    float ao : TEXCOORD3;
    float skyLight : TEXCOORD4;
    float blockLight : TEXCOORD5;
    float sunTerm : TEXCOORD6;
    float tint : TEXCOORD7;
};
float4 main(PSInput input) : SV_TARGET {
    float4 texel = atlas.Sample(atlasSampler, float3(frac(input.uv), input.layer));
    clip(texel.a - 0.05);
    float3 base = input.tint > 0.5 ? texel.rgb * foliageTint.rgb : texel.rgb;
    float aoFactor = 0.35 + 0.65 * (input.ao / 3.0);
    float skyFactor = clamp(input.skyLight / 15.0, 0.0, 1.0);
    float blockFactor = clamp(input.blockLight / 15.0, 0.0, 1.0);
    float3 skyLighting = skyFactor * (ambientColor.rgb + sunColor.rgb * input.sunTerm);
    float3 blockLighting = blockFactor * float3(1.0, 0.58, 0.28);
    float3 lit = base * aoFactor * max(skyLighting + blockLighting, 0.015);
    float fog = smoothstep(fogRange.x, fogRange.y,
                           distance(input.worldPosition.xz, cameraPosition.xz));
    return float4(lerp(lit, skyColor.rgb, fog), texel.a);
})";

    ComPtr<ID3DBlob> vertexBytecode;
    ComPtr<ID3DBlob> pixelBytecode;
    ComPtr<ID3DBlob> errors;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    HRESULT result = D3DCompile(vertexSource, sizeof(vertexSource), "chunk_vs", nullptr, nullptr, "main",
                                "vs_5_0", compileFlags, 0, &vertexBytecode, &errors);
    if (FAILED(result)) {
        Logger logger;
        logger.Error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "DX11 chunk vertex shader failed.");
        return false;
    }
    errors.Reset();
    result = D3DCompile(pixelSource, sizeof(pixelSource), "chunk_ps", nullptr, nullptr, "main",
                        "ps_5_0", compileFlags, 0, &pixelBytecode, &errors);
    if (FAILED(result)) {
        Logger logger;
        logger.Error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "DX11 chunk pixel shader failed.");
        return false;
    }

    auto state = std::make_unique<DX11ChunkState>();
    if (FAILED(device->CreateVertexShader(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
                                          nullptr, &state->vertexShader)) ||
        FAILED(device->CreatePixelShader(pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
                                         nullptr, &state->pixelShader))) return false;
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"LIGHT", 0, DXGI_FORMAT_R8G8_UINT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"ATLAS", 0, DXGI_FORMAT_R16_UINT, 0, 10, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"UVTINT", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)),
                                         vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
                                         &state->inputLayout))) return false;

    D3D11_BUFFER_DESC constantDesc{};
    constantDesc.ByteWidth = 192;
    constantDesc.Usage = D3D11_USAGE_DYNAMIC;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&constantDesc, nullptr, &state->constants))) return false;

    const int layerCount = m_atlas.GetLayerCount();
    if (layerCount <= 0) return false;
    std::vector<D3D11_SUBRESOURCE_DATA> initialData(static_cast<std::size_t>(layerCount));
    for (int layer = 0; layer < layerCount; ++layer) {
        const ImageData* image = m_atlas.GetLayerImage(layer);
        if (image == nullptr || image->pixels.empty()) return false;
        initialData[static_cast<std::size_t>(layer)].pSysMem = image->pixels.data();
        initialData[static_cast<std::size_t>(layer)].SysMemPitch = static_cast<UINT>(m_atlas.GetTileWidth() * 4);
    }
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = static_cast<UINT>(m_atlas.GetTileWidth());
    textureDesc.Height = static_cast<UINT>(m_atlas.GetTileHeight());
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = static_cast<UINT>(layerCount);
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&textureDesc, initialData.data(), &state->atlasTexture))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = textureDesc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    viewDesc.Texture2DArray.MipLevels = 1;
    viewDesc.Texture2DArray.ArraySize = textureDesc.ArraySize;
    if (FAILED(device->CreateShaderResourceView(state->atlasTexture.Get(), &viewDesc, &state->atlasView))) return false;

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->sampler))) return false;

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_BACK;
    rasterDesc.FrontCounterClockwise = TRUE;
    rasterDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &state->opaqueRasterizer))) return false;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &state->transparentRasterizer))) return false;

    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(device->CreateDepthStencilState(&depthDesc, &state->opaqueDepth))) return false;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device->CreateDepthStencilState(&depthDesc, &state->transparentDepth))) return false;

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blendDesc, &state->opaqueBlend))) return false;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    if (FAILED(device->CreateBlendState(&blendDesc, &state->alphaBlend))) return false;
    m_dx11 = std::move(state);
    return true;
#else
    return false;
#endif
}

void ChunkRenderer::UploadDX11Mesh(GpuChunkMesh& mesh, const ChunkMeshData& data) {
#if defined(_WIN32)
    if (!EnsureDX11Resources()) return;
    auto* device = static_cast<ID3D11Device*>(m_renderer->GetNativeDevice());
    const auto makeBuffer = [device](const void* source, std::size_t bytes, UINT bindFlags) -> std::shared_ptr<void> {
        if (bytes == 0) return {};
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(bytes);
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = bindFlags;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = source;
        ID3D11Buffer* buffer = nullptr;
        if (FAILED(device->CreateBuffer(&description, &initial, &buffer))) return {};
        return {buffer, [](void* object) { static_cast<ID3D11Buffer*>(object)->Release(); }};
    };
    mesh.nativeVertexBuffer = makeBuffer(data.vertices.data(), data.vertices.size() * sizeof(ChunkVertex),
                                         D3D11_BIND_VERTEX_BUFFER);
    mesh.nativeIndexBuffer = makeBuffer(data.indices.data(), data.indices.size() * sizeof(std::uint32_t),
                                        D3D11_BIND_INDEX_BUFFER);
#else
    (void)mesh;
    (void)data;
#endif
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

    if (m_renderer != nullptr && m_renderer->GetBackend() == RendererBackend::Direct3D11) {
        UploadDX11Mesh(mesh, data);
        return;
    }

    if (glGenVertexArrays == nullptr) {
        // No live GL context (headless unit test): cache mesh metadata without touching the driver.
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

}

void ChunkRenderer::UploadCompletedMeshes() {
    const auto start = std::chrono::steady_clock::now();

    std::vector<PendingMeshResult> batch;
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        batch.swap(m_completed);
    }
    batch.insert(batch.end(), std::make_move_iterator(m_deferredUploads.begin()), std::make_move_iterator(m_deferredUploads.end()));
    m_deferredUploads.clear();

    std::uint32_t uploaded = 0;
    for (std::size_t i = 0; i < batch.size(); ++i) {
        m_inFlight.erase(batch[i].coordinate);
        m_backgroundInFlight.erase(batch[i].coordinate);

        if (!m_knownResidentChunks.contains(batch[i].coordinate)) {
            continue;
        }

        if (batch[i].revision != m_revisions[batch[i].coordinate]) {
            // A block edit happened while this job ran; preserve its dirty state and discard stale geometry.
            m_dirty.insert(batch[i].coordinate);
            continue;
        }

        const auto elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool isEditPriority = m_editPriority.contains(batch[i].coordinate);
        const bool overBudget = !isEditPriority &&
                    (uploaded >= m_uploadBudgetChunksPerFrame || elapsedMs >= m_uploadBudgetMilliseconds);
        if (overBudget) {
            m_deferredUploads.push_back(std::move(batch[i]));
            continue;
        }

        if (m_renderer != nullptr && m_renderer->GetBackend() == RendererBackend::Direct3D11) {
            static_cast<void>(EnsureDX11Resources());
        } else {
            static_cast<void>(EnsureProgram());
        }
        UploadMesh(batch[i].coordinate, std::move(batch[i].data));
        if (batch[i].editPriority) {
            m_metrics.lastEditMeshingMilliseconds = batch[i].meshingMilliseconds;
            const auto requestedAt = m_editRequestedAt.find(batch[i].coordinate);
            if (requestedAt != m_editRequestedAt.end()) {
                m_metrics.lastEditLatencyMilliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requestedAt->second).count();
                m_editRequestedAt.erase(requestedAt);
            }
            ++m_metrics.completedEditMeshes;
            m_editPriority.erase(batch[i].coordinate);
        }
        ++uploaded;
    }

    m_metrics.lastUploadMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_metrics.meshedChunks = m_meshes.size();
    m_metrics.meshQueueDepth = m_dirty.size() + m_inFlight.size() + m_deferredUploads.size();
}

void ChunkRenderer::Render(const voxels::Camera& camera) {
    if (m_renderer != nullptr && m_renderer->GetBackend() == RendererBackend::Direct3D11) {
        RenderDX11(camera);
        return;
    }
    RenderOpenGLPass(camera, true, true, true);
}

void ChunkRenderer::RenderOpaque(const voxels::Camera& camera) {
    if (m_renderer != nullptr && m_renderer->GetBackend() == RendererBackend::Direct3D11) {
        RenderDX11(camera);
        return;
    }
    RenderOpenGLPass(camera, true, false, true);
}

void ChunkRenderer::RenderTransparent(const voxels::Camera& camera) {
    if (m_renderer != nullptr && m_renderer->GetBackend() == RendererBackend::Direct3D11) return;
    RenderOpenGLPass(camera, false, true, false);
}

void ChunkRenderer::RenderOpenGLPass(const voxels::Camera& camera, bool drawOpaque,
                                     bool drawTransparent, bool resetMetrics) {
    if (m_program == 0 || m_meshes.empty()) {
        if (resetMetrics) {
            m_metrics.visibleChunks = 0;
            m_metrics.drawCalls = 0;
            m_metrics.triangles = 0;
        }
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
        if (drawOpaque && mesh.opaqueIndexCount > 0) {
            visibleOpaque.emplace_back(&coordinate, &mesh);
        }
        if (drawTransparent && mesh.transparentIndexCount > 0) {
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
    glUniform3fv(m_uniformSunDirection, 1, &m_celestialLighting.sunDirection[0]);
    glUniform3fv(m_uniformSunColor, 1, &m_celestialLighting.sunColor[0]);
    glUniform3fv(m_uniformAmbientColor, 1, &m_celestialLighting.ambientColor[0]);
    const glm::vec3 fogColor = SkyHorizonColor(m_celestialLighting);
    glUniform3fv(m_uniformSkyColor, 1, &fogColor[0]);
    glUniform1f(m_uniformFogStart, m_fogRange.startBlocks);
    glUniform1f(m_uniformFogEnd, m_fogRange.endBlocks);
    glUniform1i(m_uniformTexture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas.GetTextureHandle());

    std::size_t drawCalls = resetMetrics ? 0 : m_metrics.drawCalls;
    std::size_t triangles = resetMetrics ? 0 : m_metrics.triangles;

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
    glDisable(GL_CULL_FACE);
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
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);

    m_metrics.visibleChunks = (resetMetrics ? 0 : m_metrics.visibleChunks) +
                              visibleOpaque.size() + visibleTransparent.size();
    m_metrics.drawCalls = drawCalls;
    m_metrics.triangles = triangles;
    m_metrics.loadedChunks = m_meshes.size();
}

void ChunkRenderer::RenderDX11(const voxels::Camera& camera) {
#if defined(_WIN32)
    if (!EnsureDX11Resources() || m_meshes.empty()) {
        m_metrics.visibleChunks = 0;
        m_metrics.drawCalls = 0;
        m_metrics.triangles = 0;
        return;
    }
    auto* context = static_cast<ID3D11DeviceContext*>(m_renderer->GetNativeContext());
    if (context == nullptr) return;

    voxels::Frustum frustum;
    frustum.Update(camera.ViewProjection());
    std::vector<std::pair<const voxels::ChunkCoordinate*, GpuChunkMesh*>> visibleOpaque;
    std::vector<std::pair<const voxels::ChunkCoordinate*, GpuChunkMesh*>> visibleTransparent;
    for (auto& [coordinate, mesh] : m_meshes) {
        if (!frustum.Intersects(mesh.aabb) || !mesh.nativeVertexBuffer || !mesh.nativeIndexBuffer) continue;
        if (mesh.opaqueIndexCount > 0) visibleOpaque.emplace_back(&coordinate, &mesh);
        if (mesh.transparentIndexCount > 0) visibleTransparent.emplace_back(&coordinate, &mesh);
    }
    const auto distanceTo = [&](const voxels::ChunkCoordinate& coordinate) {
        return DistanceSq(ChunkOrigin(coordinate, m_chunkSize) +
                              glm::vec3(static_cast<float>(m_chunkSize) * 0.5f),
                          camera.position);
    };
    std::sort(visibleOpaque.begin(), visibleOpaque.end(), [&](const auto& lhs, const auto& rhs) {
        return distanceTo(*lhs.first) < distanceTo(*rhs.first);
    });
    std::sort(visibleTransparent.begin(), visibleTransparent.end(), [&](const auto& lhs, const auto& rhs) {
        return distanceTo(*lhs.first) > distanceTo(*rhs.first);
    });

    struct alignas(16) FrameConstants {
        glm::mat4 viewProjection;
        glm::vec4 chunkOrigin;
        glm::vec4 cameraPosition;
        glm::vec4 foliageTint;
        glm::vec4 sunDirection;
        glm::vec4 sunColor;
        glm::vec4 ambientColor;
        glm::vec4 skyColor;
        glm::vec4 fogRange;
    };
    static_assert(sizeof(FrameConstants) == 192);
    const glm::mat4 viewProjection = camera.ViewProjection();
    context->IASetInputLayout(m_dx11->inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_dx11->vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_dx11->pixelShader.Get(), nullptr, 0);
    ID3D11Buffer* constantBuffer = m_dx11->constants.Get();
    context->VSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    ID3D11ShaderResourceView* atlasView = m_dx11->atlasView.Get();
    ID3D11SamplerState* sampler = m_dx11->sampler.Get();
    context->PSSetShaderResources(0, 1, &atlasView);
    context->PSSetSamplers(0, 1, &sampler);

    std::size_t drawCalls = 0;
    std::size_t triangles = 0;
    const auto drawMesh = [&](const voxels::ChunkCoordinate& coordinate, GpuChunkMesh& mesh,
                              std::uint32_t count, std::uint32_t startIndex) {
        auto* vertexBuffer = static_cast<ID3D11Buffer*>(mesh.nativeVertexBuffer.get());
        auto* indexBuffer = static_cast<ID3D11Buffer*>(mesh.nativeIndexBuffer.get());
        constexpr UINT stride = sizeof(ChunkVertex);
        constexpr UINT offset = 0;
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(m_dx11->constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const glm::vec3 origin = ChunkOrigin(coordinate, m_chunkSize);
            const glm::vec3 fogColor = SkyHorizonColor(m_celestialLighting);
            const FrameConstants constants{viewProjection, glm::vec4(origin, 0.0f),
                                           glm::vec4(camera.position, 0.0f),
                                           glm::vec4(0.45f, 0.75f, 0.35f, 0.0f),
                                           glm::vec4(m_celestialLighting.sunDirection, 0.0f),
                                           glm::vec4(m_celestialLighting.sunColor, 0.0f),
                                           glm::vec4(m_celestialLighting.ambientColor, 0.0f),
                                           glm::vec4(fogColor, 0.0f),
                                           glm::vec4(m_fogRange.startBlocks, m_fogRange.endBlocks, 0.0f, 0.0f)};
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            context->Unmap(m_dx11->constants.Get(), 0);
        }
        context->DrawIndexed(count, startIndex, 0);
    };

    constexpr float blendFactor[4]{};
    context->RSSetState(m_dx11->opaqueRasterizer.Get());
    context->OMSetDepthStencilState(m_dx11->opaqueDepth.Get(), 0);
    context->OMSetBlendState(m_dx11->opaqueBlend.Get(), blendFactor, 0xffffffffU);
    for (auto& [coordinate, mesh] : visibleOpaque) {
        drawMesh(*coordinate, *mesh, mesh->opaqueIndexCount, 0);
        ++drawCalls;
        triangles += mesh->opaqueIndexCount / 3;
    }

    context->RSSetState(m_dx11->transparentRasterizer.Get());
    context->OMSetDepthStencilState(m_dx11->transparentDepth.Get(), 0);
    context->OMSetBlendState(m_dx11->alphaBlend.Get(), blendFactor, 0xffffffffU);
    for (auto& [coordinate, mesh] : visibleTransparent) {
        drawMesh(*coordinate, *mesh, mesh->transparentIndexCount, mesh->opaqueIndexCount);
        ++drawCalls;
        triangles += mesh->transparentIndexCount / 3;
    }
    ID3D11ShaderResourceView* emptyView = nullptr;
    context->PSSetShaderResources(0, 1, &emptyView);

    m_metrics.visibleChunks = visibleOpaque.size() + visibleTransparent.size();
    m_metrics.drawCalls = drawCalls;
    m_metrics.triangles = triangles;
    m_metrics.loadedChunks = m_meshes.size();
#else
    (void)camera;
#endif
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
    m_dx11.reset();
}

} // namespace voxels::graphics
