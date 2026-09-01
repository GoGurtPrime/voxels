/**
 * @file rhi.hpp
 * @brief Platform-neutral render hardware interface contracts.
 *
 * @details Defines the resource, command, and renderer interfaces shared by all graphics
 *          backends. The contracts deliberately contain no native API types so application,
 *          editor, and headless test code remain portable across supported platforms.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace voxels::graphics {

/// Native API family behind an IRenderer; only Mock is implemented today (ADR-001/ADR-013).
enum class RendererBackend { Mock, Vulkan, DirectX12, Metal, DreamcastPVR };
enum class BufferType { Vertex, Index, Uniform };
enum class TextureType { Texture2D, Texture2DArray };
enum class TextureFormat { Rgba8Unorm, Depth24Stencil8 };
enum class ShaderStage { Vertex, Fragment, Compute };

/// Feature/limit report a backend returns from IRenderer::capabilities().
struct RendererCapabilities {
    RendererBackend backend{RendererBackend::Mock};
    bool supportsPresentation{false}; ///< False for headless backends with no swap chain (Mock).
    bool supportsTextureArrays{true};
    std::uint32_t maxTextureDimension{4096};
};

struct BufferDescriptor {
    BufferType type{BufferType::Vertex};
    std::size_t sizeBytes{0};
    std::string debugName;
};

struct TextureDescriptor {
    TextureType type{TextureType::Texture2D};
    TextureFormat format{TextureFormat::Rgba8Unorm};
    std::uint32_t width{1};
    std::uint32_t height{1};
    std::uint32_t layers{1};
    std::string debugName;
};

struct ShaderDescriptor {
    ShaderStage stage{ShaderStage::Vertex};
    std::string entryPoint{"main"};
    std::vector<std::byte> bytecode; ///< Backend-defined bytecode or source bytes; opaque to the RHI layer.
    std::string debugName;
};

struct RasterizerState { bool cullBackFaces{true}; bool wireframe{false}; };
struct DepthStencilState { bool depthTestEnabled{true}; bool depthWriteEnabled{true}; };
struct BlendState { bool enabled{false}; };

/// One vertex-shader input within a VertexLayout.
struct VertexAttribute {
    std::uint32_t location{0};       ///< Shader attribute location.
    std::uint32_t offset{0};         ///< Bytes from the start of the vertex.
    std::uint32_t componentCount{0}; ///< Scalar components (e.g. 3 for a vec3).
};

struct VertexLayout {
    std::uint32_t stride{0}; ///< Bytes between consecutive vertices.
    std::vector<VertexAttribute> attributes;
};

class IBuffer {
public:
    virtual ~IBuffer() = default;
    [[nodiscard]] virtual const BufferDescriptor& descriptor() const noexcept = 0;
    [[nodiscard]] virtual std::size_t sizeBytes() const noexcept = 0;
    /// Copies `data` into the buffer at `offsetBytes`; returns false (writing nothing) when the
    /// range would overrun the buffer.
    virtual bool write(std::span<const std::byte> data, std::size_t offsetBytes = 0) = 0;
};

class ITexture {
public:
    virtual ~ITexture() = default;
    [[nodiscard]] virtual const TextureDescriptor& descriptor() const noexcept = 0;
};

class IShader {
public:
    virtual ~IShader() = default;
    [[nodiscard]] virtual const ShaderDescriptor& descriptor() const noexcept = 0;
};

struct PipelineDescriptor {
    RasterizerState rasterizer{};
    DepthStencilState depthStencil{};
    BlendState blend{};
    VertexLayout vertexLayout{};
    std::shared_ptr<IShader> vertexShader;
    std::shared_ptr<IShader> fragmentShader;
    std::string debugName;
};

class IPipelineState {
public:
    virtual ~IPipelineState() = default;
    [[nodiscard]] virtual const PipelineDescriptor& descriptor() const noexcept = 0;
};

using BufferPtr = std::shared_ptr<IBuffer>;
using TexturePtr = std::shared_ptr<ITexture>;
using ShaderPtr = std::shared_ptr<IShader>;
using PipelineStatePtr = std::shared_ptr<IPipelineState>;

/// Snapshot of the bindings at record time; exactly one of vertexCount/indexCount is non-zero
/// (non-indexed vs indexed draw).
struct DrawCommand {
    BufferPtr vertexBuffer;
    BufferPtr indexBuffer;
    PipelineStatePtr pipeline;
    std::uint32_t vertexCount{0};
    std::uint32_t indexCount{0};
};

/// CPU-side draw recording against the currently bound vertex/index buffer and pipeline; no
/// native API work happens until an IRenderer consumes commands() at submit time.
class CommandBuffer {
public:
    void bindVertexBuffer(BufferPtr buffer) { m_vertexBuffer = std::move(buffer); }
    void bindIndexBuffer(BufferPtr buffer) { m_indexBuffer = std::move(buffer); }
    void bindPipeline(PipelineStatePtr pipeline) { m_pipeline = std::move(pipeline); }
    /// Records a non-indexed draw capturing the current bindings.
    void draw(std::uint32_t vertexCount);
    /// Records an indexed draw capturing the current bindings (index buffer required at submit).
    void drawIndexed(std::uint32_t indexCount);
    [[nodiscard]] const std::vector<DrawCommand>& commands() const noexcept { return m_commands; }

private:
    BufferPtr m_vertexBuffer;
    BufferPtr m_indexBuffer;
    PipelineStatePtr m_pipeline;
    std::vector<DrawCommand> m_commands;
};

/// Backend contract: resource factory plus frame driver. Per-frame call order is
/// beginFrame -> submitCommandBuffer... -> endFrame -> present.
class IRenderer {
public:
    virtual ~IRenderer() = default;
    /// Acquires backend resources; every other call may legitimately fail until this returns true.
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    /// Opens a frame; false when uninitialized or a frame is already open.
    virtual bool beginFrame() = 0;
    /// Closes the open frame; false when none is open.
    virtual bool endFrame() = 0;
    /// Replays recorded draws into the open frame; false outside a frame or when a draw lacks a
    /// bound pipeline/vertex buffer (or index buffer for indexed draws).
    virtual bool submitCommandBuffer(const CommandBuffer& commandBuffer) = 0;
    /// Presents the last completed frame; valid only between frames (after endFrame).
    virtual bool present() = 0;
    [[nodiscard]] virtual RendererCapabilities capabilities() const noexcept = 0;
    virtual BufferPtr createBuffer(const BufferDescriptor& descriptor) = 0;
    virtual TexturePtr createTexture(const TextureDescriptor& descriptor) = 0;
    virtual ShaderPtr createShader(const ShaderDescriptor& descriptor) = 0;
    virtual PipelineStatePtr createPipelineState(const PipelineDescriptor& descriptor) = 0;
};

} // namespace voxels::graphics