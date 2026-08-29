/*
 * Scope: Renderer backend integration scaffolding.
 *
 * This file is the placeholder for Vulkan, DirectX 12, and Metal implementation support.
 * The engine should later select a backend based on platform and project configuration,
 * but this initial version only establishes the interface boundaries and target capability.
 *
 * Relation to the rest of the codebase: all rendering code should be managed through the
 * renderer abstraction instead of hard-wired platform-specific graphics code.
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
