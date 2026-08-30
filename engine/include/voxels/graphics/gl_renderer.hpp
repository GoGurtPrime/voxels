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

namespace voxels::graphics {

class GLRenderer {
public:
    GLRenderer() = default;
    ~GLRenderer();

    [[nodiscard]] bool Initialize();
    void Shutdown();
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    bool BeginFrame(const std::array<float, 4>& clearColor = {0.2f, 0.3f, 0.4f, 1.0f});
    bool EndFrame();

    void SetCamera(const Camera& camera) { m_camera = camera; }
    void RenderTestScene(float elapsedSeconds); 

private:
    bool CompileShader(GLenum type, const char* sourceCode, GLuint& shaderId) const;
    bool LinkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint& programId) const;
    void CreateProceduralTexture();
    void CreateCubeMesh();
    void SetupState();
    void ApplyUniforms(const glm::mat4& modelMatrix) const;

    bool m_initialized = false;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_texture = 0;
    GLint m_uniformViewProj = -1;
    GLint m_uniformModel = -1;
    GLint m_uniformCameraPos = -1;
    GLint m_uniformTexture = -1;
    Camera m_camera{};
};

} // namespace voxels::graphics
