/**
 * @file state_machine.cpp
 * @brief Implementation of `AppStateMachine` transition bookkeeping.
 *
 * @details Deliberately thin: all behavior beyond `OnEnter`/`OnExit` ordering belongs to the
 *          concrete state classes or their owning app code.
 */

#include "voxels/app/state_machine.hpp"

#include <chrono>
#include <utility>

#include "voxels/world/generation_pipeline.hpp"
#include "voxels/world/spawn_calculator.hpp"

namespace voxels {

namespace {

void FillWorldFromChunk(World& world, const Chunk& chunk) {
    const auto chunkCoord = chunk.GetCoordinate();
    for (std::uint32_t z = 0; z < chunk.GetDepth(); ++z) {
        for (std::uint32_t y = 0; y < chunk.GetHeight(); ++y) {
            for (std::uint32_t x = 0; x < chunk.GetWidth(); ++x) {
                const Vec3I worldPos{static_cast<int>(chunkCoord.x * static_cast<int>(chunk.GetWidth()) + x),
                                     static_cast<int>(chunkCoord.y * static_cast<int>(chunk.GetHeight()) + y),
                                     static_cast<int>(chunkCoord.z * static_cast<int>(chunk.GetDepth()) + z)};
                world.SetBlock(worldPos, chunk.GetBlock(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)));
            }
        }
    }
}

} // namespace

std::string_view ToString(AppStateId id) noexcept {
    switch (id) {
        case AppStateId::Boot: return "Boot";
        case AppStateId::MainMenu: return "MainMenu";
        case AppStateId::WorldSelect: return "WorldSelect";
        case AppStateId::WorldCreation: return "WorldCreation";
        case AppStateId::LoadingScreen: return "LoadingScreen";
        case AppStateId::JoinGame: return "JoinGame";
        case AppStateId::JoinLoading: return "JoinLoading";
        case AppStateId::InGame: return "InGame";
        case AppStateId::PauseMenu: return "PauseMenu";
        case AppStateId::Settings: return "Settings";
        case AppStateId::ControlsCard: return "ControlsCard";
        case AppStateId::Error: return "Error";
    }
    return "Unknown";
}

void AppStateMachine::Start(std::unique_ptr<IAppState> state) {
    m_current = std::move(state);
    if (m_current) {
        m_log.push_back({m_current->GetId(), true});
        m_current->OnEnter();
    }
}

void AppStateMachine::TransitionTo(std::unique_ptr<IAppState> state) {
    while (!m_overlays.empty()) {
        m_overlays.back()->OnExit();
        m_log.push_back({m_overlays.back()->GetId(), false});
        m_overlays.pop_back();
    }
    if (m_current) {
        m_current->OnExit();
        m_log.push_back({m_current->GetId(), false});
    }
    m_current = std::move(state);
    if (m_current) {
        m_log.push_back({m_current->GetId(), true});
        m_current->OnEnter();
    }
}

void AppStateMachine::RequestTransition(std::unique_ptr<IAppState> state) {
    m_pending = std::move(state);
}

void AppStateMachine::PushOverlay(std::unique_ptr<IAppState> state) {
    if (!state) return;
    m_log.push_back({state->GetId(), true});
    state->OnEnter();
    m_overlays.push_back(std::move(state));
}

void AppStateMachine::RequestPushOverlay(std::unique_ptr<IAppState> state) {
    m_pendingOverlay = std::move(state);
}

void AppStateMachine::PopOverlay() {
    if (m_overlays.empty()) return;
    m_overlays.back()->OnExit();
    m_log.push_back({m_overlays.back()->GetId(), false});
    m_overlays.pop_back();
}

void AppStateMachine::RequestPopOverlay() {
    m_popOverlayRequested = true;
}

void AppStateMachine::Update(double deltaSeconds) {
    IAppState* active = m_overlays.empty() ? m_current.get() : m_overlays.back().get();
    if (active) active->Update(deltaSeconds);
    if (m_pending) TransitionTo(std::move(m_pending));
    if (m_popOverlayRequested) {
        m_popOverlayRequested = false;
        PopOverlay();
    }
    if (m_pendingOverlay) PushOverlay(std::move(m_pendingOverlay));
}

void AppStateMachine::Render() {
    if (m_current) m_current->Render();
    for (const auto& overlay : m_overlays) overlay->Render();
}

void AppStateMachine::Shutdown() {
    while (!m_overlays.empty()) PopOverlay();
    if (m_current) {
        m_current->OnExit();
        m_log.push_back({m_current->GetId(), false});
        m_current.reset();
    }
}

void LoadingScreenState::RunGeneration() {
    if (m_saveManager && !m_saveName.empty()) {
        GameSave loadedSave{};
        if (m_saveManager->Load(m_saveName, loadedSave)) {
            m_options.seed = loadedSave.seed;
            m_options.generatorVersion = loadedSave.generatorVersion;
            if (loadedSave.publicVisibility) {
                m_options.isPublic = true;
            }
        }
    }

    if (m_options.generatorVersion > WorldGenerator::kGeneratorVersion) {
        m_generationQueue.clear();
        m_phase = GenerationPhase::Complete;
        return;
    }
    m_world = std::make_unique<World>();
    m_world->Initialize(m_options);
    m_generationQueue.clear();
    constexpr int kInitialLoadRadius = 2;
    constexpr int kTerrainSectionCount = 8;
    const int initialLoadRadius = std::min(kInitialLoadRadius, std::max(1, m_options.renderDistanceChunks));
    for (int ring = 0; ring <= initialLoadRadius; ++ring) {
        for (int z = -ring; z <= ring; ++z) {
            for (int x = -ring; x <= ring; ++x) {
                if (std::max(std::abs(x), std::abs(z)) != ring) continue;
                for (int y = 0; y < kTerrainSectionCount; ++y) m_generationQueue.push_back({x, y, z});
            }
        }
    }
    m_generatedChunks = 0;
    m_generationResults.clear();
    if (m_context != nullptr) {
        m_generationJobs = std::make_unique<JobSystem>();
        m_generationResults.reserve(m_generationQueue.size());
        for (const ChunkCoordinate coordinate : m_generationQueue) {
            const WorldOptions options = m_options;
            m_generationResults.push_back({coordinate, m_generationJobs->EnqueueWithResult([options, coordinate] {
                                              return WorldGenerator(options).GenerateChunk(coordinate);
                                          })});
        }
    }
    m_phase = GenerationPhase::Shape;
}

float LoadingScreenState::GetProgress() const noexcept {
    switch (m_phase) {
        case GenerationPhase::Shape: return 0.0f;
        case GenerationPhase::Caves: return 0.25f;
        case GenerationPhase::Vegetation: return 0.5f;
        case GenerationPhase::SpawnPlacement: return 0.75f;
        case GenerationPhase::Complete: return 1.0f;
    }
    return 0.0f;
}

} // namespace voxels
