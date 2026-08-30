/**
 * @file gl_renderer.cpp
 * @brief Real OpenGL 3.3 renderer implementation: frame lifecycle, viewport, and atlas.
 */

#include "voxels/graphics/gl_renderer.hpp"

#include "voxels/core/logger.hpp"
#include "voxels/render/texture_forge.hpp"

namespace voxels::graphics {

GLRenderer::~GLRenderer() {
    Shutdown();
}

bool GLRenderer::Initialize() {
    if (m_initialized) {
        return true;
    }

    if (glGetString == nullptr || glGetString(GL_VERSION) == nullptr) {
        return false;
    }

    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    voxels::Logger logger;
    logger.Info(std::string("OpenGL vendor: ") + reinterpret_cast<const char*>(vendor));
    logger.Info(std::string("OpenGL renderer: ") + reinterpret_cast<const char*>(renderer));
    logger.Info(std::string("OpenGL version: ") + reinterpret_cast<const char*>(version));

    if (m_atlas == nullptr) {
        CreateDefaultAtlas();
    }
    SetupState();

    m_initialized = true;
    return true;
}

void GLRenderer::Shutdown() {
    if (!m_initialized) {
        return;
    }
    if (m_ownedAtlas) {
        m_ownedAtlas->Shutdown();
        m_ownedAtlas.reset();
    }
    m_initialized = false;
}

void GLRenderer::SetViewport(int width, int height) {
    if (width > 0 && height > 0) {
        m_viewportWidth = width;
        m_viewportHeight = height;
        glViewport(0, 0, width, height);
    }
}

bool GLRenderer::BeginFrame(const std::array<float, 4>& clearColor, int viewportWidth, int viewportHeight) {
    if (!m_initialized) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (viewportWidth > 0 && viewportHeight > 0) {
        m_viewportWidth = viewportWidth;
        m_viewportHeight = viewportHeight;
    }
    if (m_viewportWidth > 0 && m_viewportHeight > 0) {
        glViewport(0, 0, m_viewportWidth, m_viewportHeight);
    }
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

bool GLRenderer::EndFrame() {
    if (!m_initialized) {
        return false;
    }
    glFlush();
    return true;
}

void GLRenderer::CreateDefaultAtlas() {
    m_ownedAtlas = std::make_unique<TextureAtlas>(16, 16);
    for (const auto& name : TextureForge::GetLaunchTextureNames()) {
        m_ownedAtlas->RegisterTexture("blocks/" + name, TextureForge::GenerateTexture("blocks/" + name));
    }
    m_ownedAtlas->BuildGLTexture();
    m_atlas = m_ownedAtlas.get();
}

void GLRenderer::SetupState() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

} // namespace voxels::graphics
