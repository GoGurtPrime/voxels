/**
 * @file player_ui_dispatcher.cpp
 * @brief Deferred state transition mapping for validated player UI actions.
 */

#include "voxels/app/player_ui_dispatcher.hpp"

#include "voxels/app/state_machine.hpp"

namespace voxels {

bool PlayerUIActionDispatcher::Dispatch(PlayerUIRoute activeRoute, const PlayerUIAction& action,
                                        AppContext& context) const {
    if (action.requestId == 0) return false;
    switch (action.kind) {
        case PlayerUIActionKind::Play:
            if (activeRoute != PlayerUIRoute::MainMenu || !context.requestTransition) return false;
            context.requestTransition(std::make_unique<WorldSelectState>(&context));
            return true;
        case PlayerUIActionKind::OpenSettings:
            if ((activeRoute != PlayerUIRoute::MainMenu && activeRoute != PlayerUIRoute::Pause) ||
                !context.requestPushOverlay) return false;
            context.requestPushOverlay(std::make_unique<SettingsState>(&context));
            return true;
        case PlayerUIActionKind::Resume:
            if (activeRoute != PlayerUIRoute::Pause || !context.requestPopOverlay) return false;
            context.requestPopOverlay();
            return true;
        case PlayerUIActionKind::DismissControls:
            if (activeRoute != PlayerUIRoute::ControlsCard || !context.requestPopOverlay) return false;
            context.requestPopOverlay();
            return true;
        case PlayerUIActionKind::AcknowledgeError:
        case PlayerUIActionKind::Back:
            if ((activeRoute != PlayerUIRoute::Error && activeRoute != PlayerUIRoute::FatalError) ||
                !context.requestTransition) return false;
            context.requestTransition(std::make_unique<MainMenuState>(&context));
            return true;
        case PlayerUIActionKind::Quit:
            if (activeRoute != PlayerUIRoute::MainMenu || !context.requestQuit) return false;
            context.requestQuit();
            return true;
        default: return false;
    }
}

} // namespace voxels