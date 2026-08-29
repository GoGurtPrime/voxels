/**
 * @file mock_renderer.cpp
 * @brief Implementation of the headless mock RHI backend.
 *
 * @details Stores graphics resources and draw submissions entirely in CPU memory, allowing RHI
 *          behavior to be tested without a platform graphics driver or presentation surface.
 */

#include "voxels/graphics/mock/mock_renderer.hpp"

#include <algorithm>
#include <utility>

namespace voxels::graphics {
namespace {

class MockBuffer final : public IBuffer {
public:
    explicit MockBuffer(BufferDescriptor descriptor)
        : m_descriptor(std::move(descriptor)), m_data(m_descriptor.sizeBytes) {}

    [[nodiscard]] const BufferDescriptor& descriptor() const noexcept override { return m_descriptor; }
    [[nodiscard]] std::size_t sizeBytes() const noexcept override { return m_data.size(); }

    bool write(std::span<const std::byte> data, std::size_t offsetBytes) override {
        if (offsetBytes > m_data.size() || data.size() > m_data.size() - offsetBytes) {
            return false;
        }
        std::copy(data.begin(), data.end(), m_data.begin() + static_cast<std::ptrdiff_t>(offsetBytes));
        return true;
    }

private:
    BufferDescriptor m_descriptor;
    std::vector<std::byte> m_data;
};

class MockTexture final : public ITexture {
public:
    explicit MockTexture(TextureDescriptor descriptor) : m_descriptor(std::move(descriptor)) {}
    [[nodiscard]] const TextureDescriptor& descriptor() const noexcept override { return m_descriptor; }

private:
    TextureDescriptor m_descriptor;
};

class MockShader final : public IShader {
public:
    explicit MockShader(ShaderDescriptor descriptor) : m_descriptor(std::move(descriptor)) {}
    [[nodiscard]] const ShaderDescriptor& descriptor() const noexcept override { return m_descriptor; }

private:
    ShaderDescriptor m_descriptor;
};

class MockPipelineState final : public IPipelineState {
public:
    explicit MockPipelineState(PipelineDescriptor descriptor) : m_descriptor(std::move(descriptor)) {}
    [[nodiscard]] const PipelineDescriptor& descriptor() const noexcept override { return m_descriptor; }

private:
    PipelineDescriptor m_descriptor;
};

} // namespace

void CommandBuffer::draw(std::uint32_t vertexCount) {
    m_commands.push_back(DrawCommand{m_vertexBuffer, nullptr, m_pipeline, vertexCount, 0});
}

void CommandBuffer::drawIndexed(std::uint32_t indexCount) {
    m_commands.push_back(DrawCommand{m_vertexBuffer, m_indexBuffer, m_pipeline, 0, indexCount});
}

bool MockRenderer::initialize() {
    m_initialized = true;
    return true;
}

void MockRenderer::shutdown() {
    m_frameActive = false;
    m_initialized = false;
    m_drawCalls.clear();
}

bool MockRenderer::beginFrame() {
    if (!m_initialized || m_frameActive) {
        return false;
    }
    m_frameActive = true;
    return true;
}

bool MockRenderer::endFrame() {
    if (!m_initialized || !m_frameActive) {
        return false;
    }
    m_frameActive = false;
    return true;
}

bool MockRenderer::submitCommandBuffer(const CommandBuffer& commandBuffer) {
    if (!m_initialized || !m_frameActive) {
        return false;
    }
    for (const DrawCommand& command : commandBuffer.commands()) {
        if (command.vertexBuffer == nullptr || command.pipeline == nullptr ||
            (command.indexCount > 0 && command.indexBuffer == nullptr)) {
            return false;
        }
        m_drawCalls.push_back(RecordedDrawCall{
            command.vertexBuffer, command.indexBuffer, command.pipeline, command.vertexCount, command.indexCount});
    }
    return true;
}

bool MockRenderer::present() {
    return m_initialized && !m_frameActive;
}

RendererCapabilities MockRenderer::capabilities() const noexcept {
    return RendererCapabilities{backendType(), false, true, 4096};
}

BufferPtr MockRenderer::createBuffer(const BufferDescriptor& descriptor) {
    return descriptor.sizeBytes == 0 ? nullptr : std::make_shared<MockBuffer>(descriptor);
}

TexturePtr MockRenderer::createTexture(const TextureDescriptor& descriptor) {
    if (descriptor.width == 0 || descriptor.height == 0 || descriptor.layers == 0) {
        return nullptr;
    }
    return std::make_shared<MockTexture>(descriptor);
}

ShaderPtr MockRenderer::createShader(const ShaderDescriptor& descriptor) {
    return std::make_shared<MockShader>(descriptor);
}

PipelineStatePtr MockRenderer::createPipelineState(const PipelineDescriptor& descriptor) {
    if (descriptor.vertexLayout.stride == 0 || descriptor.vertexShader == nullptr || descriptor.fragmentShader == nullptr) {
        return nullptr;
    }
    return std::make_shared<MockPipelineState>(descriptor);
}

} // namespace voxels::graphics