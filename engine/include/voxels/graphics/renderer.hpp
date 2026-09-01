#pragma once

/**
 * @file renderer.hpp
 * @brief Backend-agnostic renderer interface (`IRenderer`) and backend selection types.
 *
 * @details The minimal frame-lifecycle contract every graphics backend implements. OpenGL 3.3
 *          Core (`GLRenderer`) is the shipping implementation; Vulkan/DX12/Metal/Dreamcast are
 *          declared-but-unimplemented placeholders per ADR-001/ADR-013, and MockRenderer is a
 *          test fixture that must never appear on the shipping path (AGENT_RULES.md §2).
 *          See voxels/graphics/rhi.hpp for the richer resource-level RHI used by chunk
 *          rendering.
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

/// Frame-lifecycle contract for a graphics backend. All calls are main-thread only (ADR-008).
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// Acquires API resources for the given configuration. Returns false on failure, which is
    /// fatal at startup.
    virtual bool Initialize(const RendererConfig& config) = 0;
    virtual void Shutdown() = 0;

    /// Begins a frame: clears targets and prepares per-frame state. Pair with EndFrame().
    virtual void BeginFrame() = 0;

    /// Finishes the frame's command submission; presentation happens via the platform's
    /// SwapBuffers, not here.
    virtual void EndFrame() = 0;
    virtual RendererBackendType GetBackendType() const = 0;
};

} // namespace voxels
