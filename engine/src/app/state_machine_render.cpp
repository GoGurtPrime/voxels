/**
 * @file state_machine_render.cpp
 * @brief Implements the real desktop states that render a visible scene in the shipping path.
 */

#include "voxels/app/state_machine.hpp"

#include <cmath>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "voxels/core/logger.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/platform/platform.hpp"
#include "voxels/render/chunk_renderer.hpp"
#include "voxels/world/spawn_calculator.hpp"

namespace voxels {

namespace {
voxels::graphics::GLRenderer* g_renderer = nullptr;

/// Chunk radius generated around the origin for the initial playable area. Chunk streaming
/// around a moving player is work item 06; this keeps the world bounded but large enough to
/// walk around once a player controller lands.
constexpr int kInitialGenerationRadiusChunks = 3;
} // namespace

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
    // Real menu UI (ImGui) lands in work item 08; for now this just proves the render loop and
    // GL context are alive with a clear sky - no test-scene geometry is drawn here anymore
    // (removed per work_items/05, task 8: the temporary render-test scene from work item 03).
    if (g_renderer == nullptr) {
        return;
    }
    g_renderer->BeginFrame({0.58f, 0.72f, 0.88f, 1.0f});
    g_renderer->EndFrame();
}

void InGameState::OnEnter() {
    m_session.SetWorldOptions(m_options);
    m_session.SetInputManager(m_inputManager);
    m_session.Initialize();

    if (m_platform != nullptr) {
        m_platform->SetRelativeMouseMode(true);
    }

    if (m_registry != nullptr && m_atlas != nullptr) {
        if (m_jobSystem == nullptr) {
            m_jobSystem = std::make_unique<JobSystem>(2);
        }
        if (m_chunkRenderer == nullptr) {
            m_chunkRenderer = std::make_unique<graphics::ChunkRenderer>(*m_registry, *m_atlas, *m_jobSystem);
            m_chunkRenderer->SetUploadBudget(16, 4.0);
        }
    }

    if (m_inputManager != nullptr) {
        m_inputManager->BindAction("MoveForward", InputBinding{"MoveForward", static_cast<int>('w'), static_cast<int>('W'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveBackward", InputBinding{"MoveBackward", static_cast<int>('s'), static_cast<int>('S'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveLeft", InputBinding{"MoveLeft", static_cast<int>('a'), static_cast<int>('A'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveRight", InputBinding{"MoveRight", static_cast<int>('d'), static_cast<int>('D'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("Jump", InputBinding{"Jump", static_cast<int>(' '), 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("Sprint", InputBinding{"Sprint", 1073742049, 0, InputDeviceType::Keyboard});
    }
    if (m_cameraOverride != nullptr) {
        *m_cameraOverride = m_session.GetCamera();
    }

    if (m_chunkRenderer) {
        auto& world = m_session.GetWorld();
        for (const auto& [coordinate, chunk] : world.GetChunks()) {
            (void)chunk;
            m_chunkRenderer->MarkChunkDirty(coordinate);
        }
        m_chunkRenderer->EnqueueDirtyMeshJobs(world, m_session.GetCamera().position);
        m_chunkRenderer->UploadCompletedMeshes();
    }
    m_worldGenerated = true;
}

void InGameState::OnExit() {
    if (m_platform != nullptr) {
        m_platform->SetRelativeMouseMode(false);
    }
    if (m_jobSystem) {
        m_jobSystem->Shutdown();
    }
    if (m_chunkRenderer) {
        m_chunkRenderer->Shutdown();
    }
    m_chunkRenderer.reset();
    m_jobSystem.reset();
    m_session.Shutdown();
    m_worldGenerated = false;
}

void InGameState::GenerateInitialWorld() {
    if (m_worldGenerated) {
        return;
    }
    m_session.SetWorldOptions(m_options);
    m_session.Initialize();
    m_worldGenerated = true;
}

void InGameState::Update(double deltaSeconds) {
    m_elapsedSeconds += static_cast<float>(deltaSeconds);

    m_session.Update(static_cast<float>(deltaSeconds));
    if (m_cameraOverride != nullptr) {
        *m_cameraOverride = m_session.GetCamera();
    }

    if (m_chunkRenderer) {
        auto& world = m_session.GetWorld();
        for (const auto& [coordinate, chunk] : world.GetChunks()) {
            (void)chunk;
            if (!m_chunkRenderer->HasMesh(coordinate) && !m_chunkRenderer->IsDirty(coordinate)) {
                m_chunkRenderer->MarkChunkDirty(coordinate);
            }
        }
        m_chunkRenderer->EnqueueDirtyMeshJobs(world, m_session.GetCamera().position);
        m_chunkRenderer->UploadCompletedMeshes();
    }
}

void InGameState::Render() {
    if (g_renderer == nullptr) {
        return;
    }
    const Camera camera = m_cameraOverride != nullptr ? *m_cameraOverride : m_session.GetCamera();
    g_renderer->SetCamera(camera);
    g_renderer->BeginFrame({0.58f, 0.72f, 0.88f, 1.0f});
    if (m_chunkRenderer) {
        m_chunkRenderer->Render(camera);
    }
    g_renderer->EndFrame();
}

} // namespace voxels
