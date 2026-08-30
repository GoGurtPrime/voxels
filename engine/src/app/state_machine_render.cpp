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
    const float radius = 7.0f;
    const float speed = 0.35f;
    const float angle = m_elapsedSeconds * speed;
    m_camera.position = glm::vec3(std::sin(angle) * radius, 3.5f, std::cos(angle) * radius);
    m_camera.aspect = 1280.0f / 720.0f;
    m_camera.fovY = glm::radians(60.0f);
}

void MainMenuState::Render() {
    if (g_renderer == nullptr) {
        return;
    }
    const glm::vec3 target(0.0f, 0.5f, 0.0f);
    const glm::vec3 dir = glm::normalize(target - m_camera.position);
    m_camera.pitch = std::asin(dir.y);
    m_camera.yaw = std::atan2(-dir.x, -dir.z);

    g_renderer->SetCamera(m_camera);
    g_renderer->BeginFrame({0.58f, 0.72f, 0.88f, 1.0f});
    g_renderer->RenderTestScene(m_elapsedSeconds);
    g_renderer->EndFrame();
}

void InGameState::Update(double deltaSeconds) {
    m_elapsedSeconds += static_cast<float>(deltaSeconds);
    const float radius = 6.0f;
    const float speed = 0.5f;
    const float angle = m_elapsedSeconds * speed;
    m_camera.position = glm::vec3(std::sin(angle) * radius, 2.5f, std::cos(angle) * radius);
    m_camera.aspect = 1280.0f / 720.0f;
    m_camera.fovY = glm::radians(60.0f);
}

void InGameState::Render() {
    if (g_renderer == nullptr) {
        return;
    }
    const glm::vec3 target(0.0f, 0.5f, 0.0f);
    const glm::vec3 dir = glm::normalize(target - m_camera.position);
    m_camera.pitch = std::asin(dir.y);
    m_camera.yaw = std::atan2(-dir.x, -dir.z);

    g_renderer->SetCamera(m_camera);
    g_renderer->BeginFrame({0.58f, 0.72f, 0.88f, 1.0f});
    g_renderer->RenderTestScene(m_elapsedSeconds);
    g_renderer->EndFrame();
}

} // namespace voxels
