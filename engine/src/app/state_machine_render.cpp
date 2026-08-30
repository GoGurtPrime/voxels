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
    GenerateInitialWorld();
}

void InGameState::OnExit() {
    if (m_jobSystem) {
        // Blocks until every queued/in-flight mesh job (which may capture `this`) has finished,
        // so it is always safe to destroy the chunk renderer immediately afterwards.
        m_jobSystem->Shutdown();
    }
    if (m_chunkRenderer) {
        m_chunkRenderer->Shutdown();
    }
    m_chunkRenderer.reset();
    m_jobSystem.reset();
    m_worldGenerated = false;
}

void InGameState::GenerateInitialWorld() {
    if (m_worldGenerated || m_registry == nullptr || m_atlas == nullptr) {
        return;
    }

    WorldGenerator generator(m_options);
    // Vertical sections 0-2 cover the full terrain height range (worldgen surface tops out
    // around y=46); section 3 is an explicit all-air "cap" chunk so the true topmost surface
    // faces have a known (empty) neighbour above them instead of being suppressed as provisional.
    constexpr int kVerticalSections = 3;
    for (int cz = -kInitialGenerationRadiusChunks; cz <= kInitialGenerationRadiusChunks; ++cz) {
        for (int cx = -kInitialGenerationRadiusChunks; cx <= kInitialGenerationRadiusChunks; ++cx) {
            for (int cy = 0; cy < kVerticalSections; ++cy) {
                const ChunkCoordinate coordinate{cx, cy, cz};
                m_world.GetOrCreateChunk(coordinate) = generator.GenerateChunk(coordinate);
            }
            m_world.GetOrCreateChunk({cx, kVerticalSections, cz}); // all-air cap chunk
        }
    }

    m_jobSystem = std::make_unique<JobSystem>();
    m_chunkRenderer = std::make_unique<graphics::ChunkRenderer>(*m_registry, *m_atlas, *m_jobSystem);
    m_chunkRenderer->SetUploadBudget(4, 2.0);
    for (const auto& [coordinate, chunk] : m_world.GetChunks()) {
        (void)chunk;
        m_chunkRenderer->MarkChunkDirty(coordinate);
    }

    const Chunk* originChunk = &m_world.GetOrCreateChunk({0, 0, 0});
    const Vec3I spawn = FindSafeSpawn(*originChunk, 0, 0);
    m_camera.position = glm::vec3(static_cast<float>(spawn.x) + 8.0f, static_cast<float>(spawn.y) + 42.0f,
                                   static_cast<float>(spawn.z) + 70.0f);
    m_camera.fovY = glm::radians(60.0f);
    m_camera.aspect = 1280.0f / 720.0f;
    m_camera.nearPlane = 0.1f;
    m_camera.farPlane = 400.0f;

    voxels::Logger logger;
    logger.Info("InGameState generated " + std::to_string(m_world.LoadedChunkCount()) +
                " chunks around spawn (" + std::to_string(spawn.x) + ", " + std::to_string(spawn.y) + ", " +
                std::to_string(spawn.z) + ").");

    m_worldGenerated = true;
}

void InGameState::Update(double deltaSeconds) {
    m_elapsedSeconds += static_cast<float>(deltaSeconds);

    // Slow automatic flythrough so the generated world is directly observable without a player
    // controller (work item 06 replaces this with real WASD + mouse-look input).
    const float radius = 60.0f;
    const float speed = 0.08f;
    const float angle = m_elapsedSeconds * speed;
    const glm::vec3 target(8.0f, 40.0f, 8.0f);
    m_camera.position = target + glm::vec3(std::sin(angle) * radius, 30.0f + 8.0f * std::sin(angle * 0.5f),
                                            std::cos(angle) * radius);
    const glm::vec3 dir = glm::normalize(target - m_camera.position);
    m_camera.pitch = std::asin(dir.y);
    m_camera.yaw = std::atan2(-dir.x, -dir.z);

    if (m_chunkRenderer) {
        m_chunkRenderer->EnqueueDirtyMeshJobs(m_world, m_camera.position);
        m_chunkRenderer->UploadCompletedMeshes();
    }
}

void InGameState::Render() {
    if (g_renderer == nullptr) {
        return;
    }
    g_renderer->SetCamera(m_camera);
    g_renderer->BeginFrame({0.58f, 0.72f, 0.88f, 1.0f});
    if (m_chunkRenderer) {
        m_chunkRenderer->Render(m_camera);
    }
    g_renderer->EndFrame();
}

} // namespace voxels
