/**
 * @file gpu_model_preview.hpp
 * @brief Offscreen OpenGL preview of authored textured VMDL geometry.
 *
 * @details Renders the runtime model baker's exposed faces into an FBO consumed by ImGui.
 */

#pragma once

#include <glad/glad.h>

#include "voxels/assets/asset_manager.hpp"
#include "voxels/assets/vmdl_codec.hpp"

namespace voxels::editor {

class GpuModelPreview {
public:
    ~GpuModelPreview();
    GpuModelPreview() = default;
    GpuModelPreview(const GpuModelPreview&) = delete;
    GpuModelPreview& operator=(const GpuModelPreview&) = delete;

    [[nodiscard]] bool Render(const VoxelModel& model, const ImageData& texture, int width, int height);
    [[nodiscard]] GLuint Texture() const noexcept { return m_colorTexture; }
    void Shutdown();

private:
    [[nodiscard]] bool Initialize();
    void Resize(int width, int height);

    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ibo = 0;
    GLuint m_framebuffer = 0;
    GLuint m_colorTexture = 0;
    GLuint m_depthBuffer = 0;
    GLuint m_sourceTexture = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace voxels::editor