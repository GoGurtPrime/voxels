/**
 * @file dx11_renderer.hpp
 * @brief Windows Direct3D 11 renderer with flip-model presentation and resilient resizing.
 *
 * @details Owns the D3D11 device, immediate context, DXGI swap chain, render target, and depth
 *          target used by the Windows shipping path. Native interfaces are exposed only as
 *          opaque pointers for backend adapters such as Dear ImGui and the chunk GPU uploader.
 */

#pragma once

#if defined(_WIN32)

#include <array>
#include <filesystem>
#include <memory>

#include <glm/glm.hpp>

#include "voxels/graphics/renderer.hpp"
#include "voxels/render/camera.hpp"

namespace voxels::graphics {

class DX11Renderer final : public IGraphicsRenderer {
public:
    DX11Renderer();
    ~DX11Renderer() override;

    DX11Renderer(const DX11Renderer&) = delete;
    DX11Renderer& operator=(const DX11Renderer&) = delete;

    [[nodiscard]] bool Initialize(IPlatform& platform, TextureAtlas& atlas, bool vSync) override;
    void Shutdown() override;
    [[nodiscard]] bool BeginFrame(const std::array<float, 4>& clearColor) override;
    [[nodiscard]] bool EndFrame() override;
    [[nodiscard]] bool Present() override;
    void SetViewport(int width, int height) override;
    void SetCamera(const Camera& camera) override { m_camera = camera; }
    [[nodiscard]] const Camera& GetCamera() const noexcept override { return m_camera; }
    [[nodiscard]] RendererBackend GetBackend() const noexcept override { return RendererBackend::Direct3D11; }
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Direct3D 11"; }
    [[nodiscard]] bool CaptureScreenshot(const std::filesystem::path& path) const override;
    [[nodiscard]] void* GetNativeDevice() const noexcept override;
    [[nodiscard]] void* GetNativeContext() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    Camera m_camera{};
};

} // namespace voxels::graphics

#endif
