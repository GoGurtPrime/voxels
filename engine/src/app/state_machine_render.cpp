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
#include "voxels/ui/imgui_ui_manager.hpp"
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

void InGameState::OnEnter() {
    if (m_preparedWorld != nullptr) m_session.AdoptWorld(std::move(m_preparedWorld));
    m_session.SetWorldOptions(m_options);
    m_session.SetInputManager(m_inputManager);
    m_session.SetBlockRegistry(m_registry);
    if (m_context != nullptr && m_context->preferences != nullptr) m_session.SetPreferences(*m_context->preferences);
    if (m_context != nullptr) m_context->activeGame = this;
    m_session.Initialize();

    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Gameplay);
    else if (m_platform != nullptr) m_platform->SetRelativeMouseMode(true);

    if (m_registry != nullptr && m_atlas != nullptr) {
        if (m_jobSystem == nullptr) m_jobSystem = std::make_unique<JobSystem>();
        if (m_chunkRenderer == nullptr) {
            m_chunkRenderer = std::make_unique<graphics::ChunkRenderer>(*m_registry, *m_atlas, *m_jobSystem);
            m_chunkRenderer->SetUploadBudget(16, 4.0);
            const std::size_t workerCount = m_jobSystem->WorkerCount();
            m_chunkRenderer->SetBackgroundMeshQueueLimit(workerCount > 1 ? workerCount - 1 : 1);
        }
        if (m_hudRenderer == nullptr) m_hudRenderer = std::make_unique<graphics::GameplayHudRenderer>();
    }

    if (m_inputManager != nullptr) {
        m_inputManager->BindAction("MoveForward", InputBinding{"MoveForward", static_cast<int>('w'), static_cast<int>('W'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveBackward", InputBinding{"MoveBackward", static_cast<int>('s'), static_cast<int>('S'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveLeft", InputBinding{"MoveLeft", static_cast<int>('a'), static_cast<int>('A'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveRight", InputBinding{"MoveRight", static_cast<int>('d'), static_cast<int>('D'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("Jump", InputBinding{"Jump", static_cast<int>(' '), 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("Sprint", InputBinding{"Sprint", 1073742049, 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("Pause", InputBinding{"Pause", 27, 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("DestroyBlock", InputBinding{"DestroyBlock", 1, 0, InputDeviceType::Mouse});
        m_inputManager->BindAction("PlaceBlock", InputBinding{"PlaceBlock", 3, 0, InputDeviceType::Mouse});
        for (int slot = 0; slot < 9; ++slot) {
            m_inputManager->BindAction("Hotbar" + std::to_string(slot + 1), InputBinding{"", static_cast<int>('1') + slot, 0, InputDeviceType::Keyboard});
        }
    }
    if (m_cameraOverride != nullptr) {
        *m_cameraOverride = m_session.GetCamera();
    }

    if (m_chunkRenderer) {
        auto& world = m_session.GetWorld();
        const int chunkSize = static_cast<int>(world.GetChunkSize());
        for (const Vec3I& position : m_session.GetEditedBlocks()) {
            const ChunkCoordinate coordinate{static_cast<int>(std::floor(static_cast<float>(position.x) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.y) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.z) / chunkSize))};
            const Vec3I local{((position.x % chunkSize) + chunkSize) % chunkSize, ((position.y % chunkSize) + chunkSize) % chunkSize,
                               ((position.z % chunkSize) + chunkSize) % chunkSize};
            m_chunkRenderer->MarkBlockEdited(coordinate, local, world.GetChunkSize());
        }
        m_session.ClearEditedBlocks();
        m_chunkRenderer->EnqueueDirtyMeshJobs(world, m_session.GetCamera().position);
        m_chunkRenderer->UploadCompletedMeshes();
    }
    m_worldGenerated = true;
}

void InGameState::OnExit() {
        if (m_context != nullptr && m_context->activeGame == this) m_context->activeGame = nullptr;
    if (m_platform != nullptr) {
        m_platform->SetRelativeMouseMode(false);
    }
    if (m_jobSystem) {
        m_jobSystem->Shutdown();
    }
    if (m_chunkRenderer) {
        m_chunkRenderer->Shutdown();
    }
    if (m_hudRenderer) m_hudRenderer->Shutdown();
    m_chunkRenderer.reset();
    m_hudRenderer.reset();
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

    if (m_context != nullptr && m_context->input != nullptr && m_context->input->IsActionActive("Pause") &&
        m_context->requestPushOverlay) {
        m_context->requestPushOverlay(std::make_unique<PauseMenuState>(m_context, m_activeSave));
        return;
    }

    m_session.Update(static_cast<float>(deltaSeconds));
    if (m_cameraOverride != nullptr) {
        *m_cameraOverride = m_session.GetCamera();
    }

    if (m_chunkRenderer) {
        auto& world = m_session.GetWorld();
        const int chunkSize = static_cast<int>(world.GetChunkSize());
        for (const Vec3I& position : m_session.GetEditedBlocks()) {
            const ChunkCoordinate coordinate{static_cast<int>(std::floor(static_cast<float>(position.x) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.y) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.z) / chunkSize))};
            const Vec3I local{((position.x % chunkSize) + chunkSize) % chunkSize,
                               ((position.y % chunkSize) + chunkSize) % chunkSize,
                               ((position.z % chunkSize) + chunkSize) % chunkSize};
            m_chunkRenderer->MarkBlockEdited(coordinate, local, world.GetChunkSize());
        }
        m_session.ClearEditedBlocks();
        for (const auto& [coordinate, chunk] : world.GetChunks()) {
            (void)chunk;
            if (!m_chunkRenderer->HasMesh(coordinate) && !m_chunkRenderer->IsDirty(coordinate) &&
                !m_chunkRenderer->IsInFlight(coordinate)) {
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
    if (m_hudRenderer) {
        m_hudRenderer->Render(camera, m_session.GetTarget(), m_session.GetBreakProgress(), m_session.GetPlayer().state.inventory,
                              m_session.GetSelectedItemLabel(), m_session.GetSelectedItemLabelAge(), m_session.GetParticleBursts());
        m_session.ClearParticleBursts();
    }
}

} // namespace voxels
