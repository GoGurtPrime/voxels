/**
 * @file state_machine.cpp
 * @brief Implementation of `AppStateMachine` transition bookkeeping.
 *
 * @details Deliberately thin: all behavior beyond `OnEnter`/`OnExit` ordering belongs to the
 *          concrete state classes or their owning app code.
 */

#include "voxels/app/state_machine.hpp"

#include <utility>

namespace voxels {

std::string_view ToString(AppStateId id) noexcept {
    switch (id) {
        case AppStateId::Boot: return "Boot";
        case AppStateId::MainMenu: return "MainMenu";
        case AppStateId::WorldSelect: return "WorldSelect";
        case AppStateId::WorldCreation: return "WorldCreation";
        case AppStateId::LoadingScreen: return "LoadingScreen";
        case AppStateId::InGame: return "InGame";
        case AppStateId::PauseMenu: return "PauseMenu";
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

void AppStateMachine::Update(double deltaSeconds) {
    if (m_current) {
        m_current->Update(deltaSeconds);
    }
}

void AppStateMachine::Render() {
    if (m_current) {
        m_current->Render();
    }
}

} // namespace voxels
