#pragma once

/*
 * Scope: Renderer abstraction for graphics backend independence.
 *
 * The engine must support multiple rendering backends and platform-specific graphics APIs,
 * with Vulkan, DirectX 12, and Metal provisioned as initial targets. This interface is
 * intentionally generic so the app can render without depending on platform-specific code.
 *
 * Relation to the rest of the codebase: the app and editor depend on the renderer layer for
 * frame updates, scene presentation, and backend selection.
 */

#include <cstdint>
#include <string>

namespace voxels {

enum class RendererBackendType {
    Vulkan,
    DirectX12,
    Metal,
    Dreamcast,
    Unknown
};

struct RendererConfig {
    RendererBackendType backend = RendererBackendType::Vulkan;
    int renderScale = 100;
    bool vSync = true;
    std::string shaderPath;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool Initialize(const RendererConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual RendererBackendType GetBackendType() const = 0;
};

} // namespace voxels
