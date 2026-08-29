/**
 * @file metal_renderer.hpp
 * @brief Metal renderer backend scaffold.
 *
 * @details Establishes the Metal backend integration point for MTLDevice and CAMetalLayer on
 *          Apple platforms while preserving the API-independent RHI contract for all consumers.
 */

#pragma once

#include "voxels/graphics/mock/mock_renderer.hpp"

namespace voxels::graphics {

class MetalRenderer final : public MockRenderer {
protected:
    [[nodiscard]] RendererBackend backendType() const noexcept override { return RendererBackend::Metal; }
};

} // namespace voxels::graphics