/**
 * @file vulkan_renderer.hpp
 * @brief Vulkan renderer backend scaffold.
 *
 * @details Provides the Vulkan backend identity while native Vulkan instance, device, swapchain,
 *          and command-pool integration remains isolated from portable RHI consumers. Until the
 *          Vulkan SDK is integrated, it uses the headless validation resource path.
 */

#pragma once

#include "voxels/graphics/mock/mock_renderer.hpp"

namespace voxels::graphics {

class VulkanRenderer final : public MockRenderer {
protected:
    [[nodiscard]] RendererBackend backendType() const noexcept override { return RendererBackend::Vulkan; }
};

} // namespace voxels::graphics