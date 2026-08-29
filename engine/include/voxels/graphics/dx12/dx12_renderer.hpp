/**
 * @file dx12_renderer.hpp
 * @brief DirectX 12 renderer backend scaffold.
 *
 * @details Reserves the DirectX 12 backend boundary for DXGI factory, device, command queue, and
 *          swapchain ownership while retaining portable headless RHI behavior in this scaffold.
 */

#pragma once

#include "voxels/graphics/mock/mock_renderer.hpp"

namespace voxels::graphics {

class DirectX12Renderer final : public MockRenderer {
protected:
    [[nodiscard]] RendererBackend backendType() const noexcept override { return RendererBackend::DirectX12; }
};

} // namespace voxels::graphics