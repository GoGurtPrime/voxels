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

enum class RendererBackend { Mock, Vulkan, DirectX12, Metal, DreamcastPVR };
enum class BufferType { Vertex, Index, Uniform };
enum class TextureType { Texture2D, Texture2DArray };
enum class TextureFormat { Rgba8Unorm, Depth24Stencil8 };
enum class ShaderStage { Vertex, Fragment, Compute };

struct RendererCapabilities {
    RendererBackend backend{RendererBackend::Mock};
    bool supportsPresentation{false};
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
    std::vector<std::byte> bytecode;
    std::string debugName;
};

struct RasterizerState { bool cullBackFaces{true}; bool wireframe{false}; };
struct DepthStencilState { bool depthTestEnabled{true}; bool depthWriteEnabled{true}; };
struct BlendState { bool enabled{false}; };

struct VertexAttribute {
    std::uint32_t location{0};
    std::uint32_t offset{0};
    std::uint32_t componentCount{0};
};

struct VertexLayout {
    std::uint32_t stride{0};
    std::vector<VertexAttribute> attributes;
};

class IBuffer {
public:
    virtual ~IBuffer() = default;
    [[nodiscard]] virtual const BufferDescriptor& descriptor() const noexcept = 0;
    [[nodiscard]] virtual std::size_t sizeBytes() const noexcept = 0;
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

struct DrawCommand {
    BufferPtr vertexBuffer;
    BufferPtr indexBuffer;
    PipelineStatePtr pipeline;
    std::uint32_t vertexCount{0};
    std::uint32_t indexCount{0};
};

class CommandBuffer {
public:
    void bindVertexBuffer(BufferPtr buffer) { m_vertexBuffer = std::move(buffer); }
    void bindIndexBuffer(BufferPtr buffer) { m_indexBuffer = std::move(buffer); }
    void bindPipeline(PipelineStatePtr pipeline) { m_pipeline = std::move(pipeline); }
    void draw(std::uint32_t vertexCount);
    void drawIndexed(std::uint32_t indexCount);
    [[nodiscard]] const std::vector<DrawCommand>& commands() const noexcept { return m_commands; }

private:
    BufferPtr m_vertexBuffer;
    BufferPtr m_indexBuffer;
    PipelineStatePtr m_pipeline;
    std::vector<DrawCommand> m_commands;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool beginFrame() = 0;
    virtual bool endFrame() = 0;
    virtual bool submitCommandBuffer(const CommandBuffer& commandBuffer) = 0;
    virtual bool present() = 0;
    [[nodiscard]] virtual RendererCapabilities capabilities() const noexcept = 0;
    virtual BufferPtr createBuffer(const BufferDescriptor& descriptor) = 0;
    virtual TexturePtr createTexture(const TextureDescriptor& descriptor) = 0;
    virtual ShaderPtr createShader(const ShaderDescriptor& descriptor) = 0;
    virtual PipelineStatePtr createPipelineState(const PipelineDescriptor& descriptor) = 0;
};

} // namespace voxels::graphics