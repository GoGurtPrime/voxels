/**
 * @file mock_renderer.hpp
 * @brief Headless CPU renderer for RHI contract testing.
 *
 * @details Provides in-memory resources and records submitted draw commands without using a
 *          GPU, native window, or graphics SDK. It is the deterministic validation backend for
 *          automated tests and tools running in headless environments.
 */

#pragma once

#include <vector>

#include "voxels/graphics/rhi.hpp"

namespace voxels::graphics {

struct RecordedDrawCall {
    BufferPtr vertexBuffer;
    BufferPtr indexBuffer;
    PipelineStatePtr pipeline;
    std::uint32_t vertexCount{0};
    std::uint32_t indexCount{0};
};

class MockRenderer : public IRenderer {
public:
    bool initialize() override;
    void shutdown() override;
    bool beginFrame() override;
    bool endFrame() override;
    bool submitCommandBuffer(const CommandBuffer& commandBuffer) override;
    bool present() override;
    [[nodiscard]] RendererCapabilities capabilities() const noexcept override;
    BufferPtr createBuffer(const BufferDescriptor& descriptor) override;
    TexturePtr createTexture(const TextureDescriptor& descriptor) override;
    ShaderPtr createShader(const ShaderDescriptor& descriptor) override;
    PipelineStatePtr createPipelineState(const PipelineDescriptor& descriptor) override;

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isFrameActive() const noexcept { return m_frameActive; }
    [[nodiscard]] const std::vector<RecordedDrawCall>& drawCalls() const noexcept { return m_drawCalls; }
    void clearDrawCalls() { m_drawCalls.clear(); }

protected:
    [[nodiscard]] virtual RendererBackend backendType() const noexcept { return RendererBackend::Mock; }

private:
    bool m_initialized{false};
    bool m_frameActive{false};
    std::vector<RecordedDrawCall> m_drawCalls;
};

} // namespace voxels::graphics