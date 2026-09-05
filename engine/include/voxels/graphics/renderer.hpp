/**
 * @file renderer.hpp
 * @brief Backend-agnostic interface used by the desktop runtime and render passes.
 *
 * @details Defines the process-lifetime graphics backend used by application state, Dear ImGui,
 *          and backend-specific render adapters. The interface contains no native API headers;
 *          optional native handles are opaque and remain confined to platform adapters.
 */

#pragma once

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

#include "voxels/core/game_types.hpp"
#include "voxels/render/camera.hpp"

namespace voxels { class IPlatform; class TextureAtlas; enum class PlatformType; }

namespace voxels::graphics {

/// Human-readable backend label used by settings and diagnostics.
[[nodiscard]] std::string_view RendererBackendName(RendererBackend backend) noexcept;
/// Backends compiled for and supported by the target platform, in preferred order.
[[nodiscard]] std::vector<RendererBackend> AvailableRendererBackends(PlatformType platform);

/// Process-lifetime graphics device and presentation contract. All calls are main-thread only.
class IGraphicsRenderer {
public:
    virtual ~IGraphicsRenderer() = default;
    [[nodiscard]] virtual bool Initialize(IPlatform& platform, TextureAtlas& atlas, bool vSync) = 0;
    virtual void Shutdown() = 0;
    [[nodiscard]] virtual bool BeginFrame(const std::array<float, 4>& clearColor) = 0;
    [[nodiscard]] virtual bool EndFrame() = 0;
    [[nodiscard]] virtual bool Present() = 0;
    virtual void SetViewport(int width, int height) = 0;
    virtual void SetCamera(const Camera& camera) = 0;
    [[nodiscard]] virtual const Camera& GetCamera() const noexcept = 0;
    [[nodiscard]] virtual RendererBackend GetBackend() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
    [[nodiscard]] virtual bool CaptureScreenshot(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual void* GetNativeDevice() const noexcept { return nullptr; }
    [[nodiscard]] virtual void* GetNativeContext() const noexcept { return nullptr; }
};

} // namespace voxels::graphics
