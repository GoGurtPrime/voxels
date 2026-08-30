/**
 * @file gl_renderer.hpp
 * @brief OpenGL 3.3 Core renderer used by the desktop shipping path.
 *
 * @details Owns the GL context frame lifecycle (clear/viewport/state) and the block texture
 *          atlas. Actual scene geometry is drawn by dedicated renderers that own their own
 *          shader programs and buffers - see `voxels::graphics::ChunkRenderer` for the voxel
 *          world. This class intentionally has no scene-specific rendering of its own.
 */

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "voxels/render/camera.hpp"
#include "voxels/render/texture_atlas.hpp"

namespace voxels::graphics {

class GLRenderer {
public:
    GLRenderer() = default;
    ~GLRenderer();

    [[nodiscard]] bool Initialize();
    void Shutdown();
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    bool BeginFrame(const std::array<float, 4>& clearColor = {0.58f, 0.72f, 0.88f, 1.0f}, int viewportWidth = 0, int viewportHeight = 0);
    bool EndFrame();

    void SetViewport(int width, int height);
    void SetCamera(const Camera& camera) { m_camera = camera; }
    [[nodiscard]] const Camera& GetCamera() const noexcept { return m_camera; }
    void SetTextureAtlas(TextureAtlas* atlas) { m_atlas = atlas; }
    [[nodiscard]] TextureAtlas* GetTextureAtlas() const noexcept { return m_atlas; }
    [[nodiscard]] bool CaptureScreenshot(const std::filesystem::path& path) const;

private:
    void CreateDefaultAtlas();
    void SetupState();

    bool m_initialized = false;
    int m_viewportWidth = 1280;
    int m_viewportHeight = 720;
    TextureAtlas* m_atlas = nullptr;
    std::unique_ptr<TextureAtlas> m_ownedAtlas;
    Camera m_camera{};
};

} // namespace voxels::graphics

