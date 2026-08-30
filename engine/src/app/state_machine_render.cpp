/**
 * @file state_machine_render.cpp
 * @brief Implements the real desktop states that render a visible scene in the shipping path.
 */

#include "voxels/app/state_machine.hpp"

#include <cmath>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "voxels/graphics/gl_renderer.hpp"

namespace voxels {

namespace {
voxels::graphics::GLRenderer* g_renderer = nullptr;
}

void SetGlobalRenderer(voxels::graphics::GLRenderer* renderer) noexcept {
    g_renderer = renderer;
}

void MainMenuState::Update(double deltaSeconds) {
    m_elapsedSeconds += static_cast<float>(deltaSeconds);
}

void MainMenuState::Render() {
    if (g_renderer == nullptr) {
        return;
    }
    g_renderer->BeginFrame({0.58f, 0.72f, 0.88f, 1.0f});
    g_renderer->RenderTestScene(m_elapsedSeconds);
    g_renderer->EndFrame();
}

void InGameState::Update(double deltaSeconds) {
    m_elapsedSeconds += static_cast<float>(deltaSeconds);
    m_camera.yaw += static_cast<float>(deltaSeconds) * 20.0f;
    m_camera.position = {std::sin(m_elapsedSeconds) * 3.0f, 1.5f, 4.0f};
    m_camera.aspect = 1.0f;
}

void InGameState::Render() {
    if (g_renderer == nullptr) {
        return;
    }
    g_renderer->SetCamera(m_camera);
    g_renderer->BeginFrame({0.58f, 0.72f, 0.88f, 1.0f});
    g_renderer->RenderTestScene(m_elapsedSeconds);
    g_renderer->EndFrame();
}

} // namespace voxels
