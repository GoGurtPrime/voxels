/**
 * @file engine.cpp
 * @brief Engine-level initialization and top-level runtime lifecycle orchestration.
 * 
 * @details Implements the core Engine class bootstrap logic, dependency wiring,
 *          and lifecycle management. Serves as the foundation for game app and editor targets.
 */

#include "voxels/engine.hpp"
#include "voxels/core/game_types.hpp"
#include "voxels/platform/platform.hpp"

namespace voxels {

Engine::Engine() = default;

Engine::~Engine() {
    if (m_status == EngineStatus::Initialized || m_status == EngineStatus::Running) {
        shutdown();
    }
}

Engine::Engine(Engine&& other) noexcept
    : m_status(other.m_status), m_platform(std::move(other.m_platform)) {
    other.m_status = EngineStatus::Uninitialized;
}

Engine& Engine::operator=(Engine&& other) noexcept {
    if (this != &other) {
        if (m_status == EngineStatus::Initialized || m_status == EngineStatus::Running) {
            shutdown();
        }
        m_status = other.m_status;
        m_platform = std::move(other.m_platform);
        other.m_status = EngineStatus::Uninitialized;
    }
    return *this;
}

bool Engine::initialize() {
    m_platform = CreateDefaultPlatform();
    if (!m_platform->Initialize(WindowConfig{})) {
        m_platform.reset();
        m_status = EngineStatus::Error;
        return false;
    }
    m_status = EngineStatus::Initialized;
    return true;
}

void Engine::shutdown() {
    if (m_platform) {
        m_platform->Shutdown();
        m_platform.reset();
    }
    m_status = EngineStatus::Stopped;
}

bool Engine::isInitialized() const noexcept {
    return m_status == EngineStatus::Initialized || m_status == EngineStatus::Running;
}

EngineStatus Engine::getStatus() const noexcept {
    return m_status;
}

std::string Engine::getVersion() const {
    return std::string(kVersionString);
}

IPlatform* Engine::getPlatform() const noexcept {
    return m_platform.get();
}

int EngineMain() {
    return 0;
}

} // namespace voxels
