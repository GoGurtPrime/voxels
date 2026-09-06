#pragma once

/**
 * @file player_ui_dispatcher.hpp
 * @brief Route-aware bridge from backend-neutral UI actions to deferred app-state callbacks.
 */

#include "voxels/ui/player_ui.hpp"

namespace voxels {

struct AppContext;

/// Validates an action against the visible route before requesting the existing state transition.
class PlayerUIActionDispatcher {
public:
    [[nodiscard]] bool Dispatch(PlayerUIRoute activeRoute, const PlayerUIAction& action, AppContext& context) const;
};

} // namespace voxels