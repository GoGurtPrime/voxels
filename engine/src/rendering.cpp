/**
 * @file rendering.cpp
 * @brief Placeholder no-op renderer behind the IRenderer abstraction.
 *
 * @details Defines an inert Renderer that accepts any config and reports a Vulkan backend
 *          type; no factory exposes it, so nothing instantiates it yet. Actual drawing lives
 *          in the OpenGL 3.3 render layer (engine/src/render/, engine/src/graphics/). Kept as
 *          the seam where selectable Vulkan/D3D12/Metal backends would plug in later.
 */

#include "voxels/graphics/renderer.hpp"

namespace voxels {

class Renderer : public IRenderer {
public:
    bool Initialize(const RendererConfig& config) override {
        (void)config;
        return true;
    }

    void Shutdown() override {
    }

    void BeginFrame() override {
    }

    void EndFrame() override {
    }

    RendererBackendType GetBackendType() const override {
        return RendererBackendType::Vulkan;
    }
};

} // namespace voxels
