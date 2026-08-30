/**
 * @file gl_renderer.hpp
 * @brief OpenGL 3.3 Core renderer used by the desktop shipping path.
 *
 * @details Provides the real, visible rendering backend for the game window. It owns the GL
 *          shader program, mesh buffers, and a small temporary scene used to prove the window is
 *          drawing depth-tested geometry before chunk rendering is connected in a later work item.
 */

#pragma once

#include <array>
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
    void SetTextureAtlas(TextureAtlas* atlas) { m_atlas = atlas; }
    [[nodiscard]] TextureAtlas* GetTextureAtlas() const noexcept { return m_atlas; }

    void RenderTestScene(float elapsedSeconds); 

private:
    bool CompileShader(GLenum type, const char* sourceCode, GLuint& shaderId) const;
    bool LinkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint& programId) const;
    void CreateDefaultAtlas();
    void CreateCubeMesh();
    void SetupState();
    void ApplyUniforms(const glm::mat4& modelMatrix) const;

    bool m_initialized = false;
    int m_viewportWidth = 1280;
    int m_viewportHeight = 720;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    TextureAtlas* m_atlas = nullptr;
    std::unique_ptr<TextureAtlas> m_ownedAtlas;
    GLint m_uniformViewProj = -1;
    GLint m_uniformModel = -1;
    GLint m_uniformCameraPos = -1;
    GLint m_uniformTexture = -1;
    GLint m_uniformFaceLayers = -1;
    GLint m_uniformTintColor = -1;
    Camera m_camera{};
};

} // namespace voxels::graphics
