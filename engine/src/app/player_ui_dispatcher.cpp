/**
 * @file player_ui_dispatcher.cpp
 * @brief Deferred state transition mapping for validated player UI actions.
 */

#include "voxels/app/player_ui_dispatcher.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <nlohmann/json.hpp>

#include "voxels/app/display_settings.hpp"
#include "voxels/app/menus.hpp"
#include "voxels/app/save_manager.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/core/preferences.hpp"

namespace voxels {
namespace {

WindowMode WindowModeFromSettingsString(std::string_view value, WindowMode fallback) {
    if (value == "Windowed") return WindowMode::Windowed;
    if (value == "Borderless") return WindowMode::Borderless;
    if (value == "Fullscreen") return WindowMode::Fullscreen;
    return fallback;
}

void RequestError(AppContext& context, std::string detail) {
    if (context.requestTransition) context.requestTransition(std::make_unique<ErrorState>(&context, "Menu Action Failed", std::move(detail)));
}

std::optional<WorldOptions> WorldOptionsFromAction(const PlayerUIAction& action) {
    try {
        const nlohmann::json payload = nlohmann::json::parse(action.secondary);
        const std::string seed = payload.value("seed", "");
        if (seed.size() > 20 || !std::all_of(seed.begin(), seed.end(), [](unsigned char character) {
                return std::isalnum(character) != 0;
            })) {
            return std::nullopt;
        }
        WorldOptions options{};
        options.seed = static_cast<WorldSeed>(SeedFromText(seed));
        options.sandboxMode = payload.value("sandbox", false);
        options.peaceful = payload.value("peaceful", false);
        options.permadeath = payload.value("permadeath", false);
        options.alwaysSunny = payload.value("sunny", false);
        options.isPublic = payload.value("public", false);
        options.renderDistanceChunks = std::clamp(payload.value("distance", 8), 2, 16);
        return options;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace

bool PlayerUIActionDispatcher::Dispatch(PlayerUIRoute activeRoute, const PlayerUIAction& action,
                                        AppContext& context) const {
    if (action.requestId == 0) return false;
    switch (action.kind) {
        case PlayerUIActionKind::Play:
            if (activeRoute != PlayerUIRoute::MainMenu || !context.requestTransition) return false;
            if (context.saveManager != nullptr && context.saveManager->ListSaves().empty()) {
                context.requestTransition(std::make_unique<WorldCreationState>(&context));
            } else {
                context.requestTransition(std::make_unique<WorldSelectState>(&context));
            }
            return true;
        case PlayerUIActionKind::CreateWorld: {
            if (activeRoute == PlayerUIRoute::SaveSelection && context.requestTransition) {
                context.requestTransition(std::make_unique<WorldCreationState>(&context));
                return true;
            }
            if (activeRoute != PlayerUIRoute::WorldCreation || context.saveManager == nullptr || !context.requestTransition) return false;
            const auto options = WorldOptionsFromAction(action);
            if (!options || action.primary.size() > 48 || !IsFilesystemSafeWorldName(action.primary)) {
                RequestError(context, "Use a world name up to 48 characters and a seed up to 20 letters or numbers.");
                return true;
            }
            if (std::filesystem::exists(context.saveManager->GetSaveDirectory(action.primary))) {
                RequestError(context, "A world with that name already exists.");
                return true;
            }
            WorldCreationController controller;
            controller.SetWorldName(action.primary);
            controller.SetSeed(static_cast<WorldSeed>(options->seed));
            controller.SetSandboxMode(options->sandboxMode);
            controller.SetPeaceful(options->peaceful);
            controller.SetPermadeath(options->permadeath);
            controller.SetAlwaysSunny(options->alwaysSunny);
            controller.SetPublic(options->isPublic);
            controller.SetRenderDistance(options->renderDistanceChunks);
            GameSave save = controller.BuildGameSave("Player");
            save.publicVisibility = options->isPublic;
            if (!context.saveManager->Save(save)) {
                RequestError(context, "Could not create the world directory.");
                return true;
            }
            if (context.platformServices != nullptr) context.platformServices->UnlockAchievement(Achievement::FirstWorldCreated);
            auto loading = std::make_unique<LoadingScreenState>(&context);
            loading->SetSaveManager(*context.saveManager);
            loading->SetSaveName(save.saveName);
            loading->SetWorldOptions(*options);
            context.requestTransition(std::move(loading));
            return true;
        }
        case PlayerUIActionKind::LoadWorld: {
            if (activeRoute != PlayerUIRoute::SaveSelection || context.saveManager == nullptr || !context.requestTransition) return false;
            GameSave save{};
            if (!context.saveManager->Load(action.primary, save)) {
                RequestError(context, "The selected world metadata could not be read.");
                return true;
            }
            auto loading = std::make_unique<LoadingScreenState>(&context);
            loading->SetSaveManager(*context.saveManager);
            loading->SetSaveName(action.primary);
            context.requestTransition(std::move(loading));
            return true;
        }
        case PlayerUIActionKind::ConfirmDelete: {
            if (activeRoute != PlayerUIRoute::SaveSelection || context.saveManager == nullptr || !context.requestTransition) return false;
            GameSave save{};
            if (!context.saveManager->Load(action.primary, save) || save.worldName != action.secondary || !context.saveManager->DeleteSave(action.primary)) {
                RequestError(context, "The world was not deleted. Confirm its exact name and try again.");
                return true;
            }
            context.requestTransition(std::make_unique<WorldSelectState>(&context));
            return true;
        }
        case PlayerUIActionKind::Join: {
            if (activeRoute == PlayerUIRoute::MainMenu && context.requestTransition) {
                context.requestTransition(std::make_unique<JoinGameState>(&context));
                return true;
            }
            if (activeRoute != PlayerUIRoute::Join || !context.connectRemote || !context.requestTransition) return false;
            unsigned int port = 0;
            const auto parsed = std::from_chars(action.secondary.data(), action.secondary.data() + action.secondary.size(), port);
            if (action.primary.empty() || parsed.ec != std::errc{} || parsed.ptr != action.secondary.data() + action.secondary.size() || port == 0 || port > 65535 ||
                !context.connectRemote(action.primary, static_cast<std::uint16_t>(port))) {
                RequestError(context, "Enter a reachable host address and a UDP port between 1 and 65535.");
                return true;
            }
            context.requestTransition(std::make_unique<JoinLoadingState>(&context, action.primary + ":" + action.secondary));
            return true;
        }
        case PlayerUIActionKind::OpenSettings:
            if ((activeRoute != PlayerUIRoute::MainMenu && activeRoute != PlayerUIRoute::Pause) ||
                !context.requestPushOverlay) return false;
            context.requestPushOverlay(std::make_unique<SettingsState>(&context));
            return true;
        case PlayerUIActionKind::Resume:
            if (activeRoute != PlayerUIRoute::Pause || !context.requestPopOverlay) return false;
            context.requestPopOverlay();
            return true;
        case PlayerUIActionKind::OpenControls:
            if (activeRoute != PlayerUIRoute::Pause || !context.requestPushOverlay) return false;
            context.requestPushOverlay(std::make_unique<ControlsCardState>(&context));
            return true;
        case PlayerUIActionKind::ToggleWorldVisibility:
            if (activeRoute != PlayerUIRoute::Pause || context.activeGame == nullptr ||
                context.activeGame->IsRemoteSession()) return false;
            return context.activeGame->SetPublicVisibility(action.value >= 0.5f);
        case PlayerUIActionKind::ReturnToMainMenu:
            if (activeRoute != PlayerUIRoute::Pause || context.activeGame == nullptr ||
                !context.requestTransition) return false;
            if (context.activeGame->IsRemoteSession()) {
                if (context.resetNetworkToLocal) context.resetNetworkToLocal();
                context.requestTransition(std::make_unique<MainMenuState>(&context));
            } else {
                context.activeGame->RequestSaveAndReturnToMenu();
            }
            return true;
        case PlayerUIActionKind::ExitToDesktop:
            if (activeRoute != PlayerUIRoute::Pause || context.activeGame == nullptr ||
                !context.requestQuit) return false;
            if (context.activeGame->IsRemoteSession()) {
                if (context.resetNetworkToLocal) context.resetNetworkToLocal();
                context.requestQuit();
            } else {
                context.activeGame->RequestSaveAndExitToDesktop();
            }
            return true;
        case PlayerUIActionKind::DismissControls:
            if (activeRoute != PlayerUIRoute::ControlsCard || !context.requestPopOverlay) return false;
            if (context.preferences != nullptr && context.platform != nullptr) {
                context.preferences->controlsCardSeen = true;
                PreferencesManager(Paths::UserDataDir() / "settings.json", context.platform->GetContext().type).Save(*context.preferences);
            }
            context.requestPopOverlay();
            return true;
        case PlayerUIActionKind::ApplySettings:
            if (activeRoute != PlayerUIRoute::Settings || context.preferences == nullptr) return false;
            {
                const GamePreferences previous = *context.preferences;
                bool hasDisplaySettings = false;
            try {
                nlohmann::json payload = nlohmann::json::object();
                if (!action.secondary.empty()) payload = nlohmann::json::parse(action.secondary);
                if (payload.contains("settings") && payload.at("settings").is_object()) payload = payload.at("settings");
                if (!payload.is_object() || payload.empty()) return false;
                if (const auto windowMode = payload.find("windowMode");
                    windowMode != payload.end() && windowMode->is_string()) {
                    hasDisplaySettings = true;
                    context.preferences->windowMode =
                        WindowModeFromSettingsString(windowMode->get<std::string>(), context.preferences->windowMode);
                }
                if (const auto resolution = payload.find("resolution");
                    resolution != payload.end() && resolution->is_object()) {
                    hasDisplaySettings = true;
                    context.preferences->resolution.width =
                        std::clamp(resolution->value("width", context.preferences->resolution.width), 640, 7680);
                    context.preferences->resolution.height =
                        std::clamp(resolution->value("height", context.preferences->resolution.height), 360, 4320);
                }
                if (payload.contains("resolutionWidth") || payload.contains("resolutionHeight")) {
                    hasDisplaySettings = true;
                }
                context.preferences->resolution.width =
                    std::clamp(payload.value("resolutionWidth", context.preferences->resolution.width), 640, 7680);
                context.preferences->resolution.height =
                    std::clamp(payload.value("resolutionHeight", context.preferences->resolution.height), 360, 4320);
                context.preferences->fieldOfView = std::clamp(payload.value("fov", context.preferences->fieldOfView), 60.0f, 110.0f);
                context.preferences->renderDistance = std::clamp(payload.value("renderDistance", context.preferences->renderDistance), 2, 16);
                context.preferences->simulationDistance = std::clamp(payload.value("simulationDistance", context.preferences->simulationDistance), 2, 12);
                context.preferences->masterVolume = std::clamp(payload.value("master", context.preferences->masterVolume), 0.0f, 1.0f);
                context.preferences->musicVolume = std::clamp(payload.value("music", context.preferences->musicVolume), 0.0f, 1.0f);
                context.preferences->sfxVolume = std::clamp(payload.value("effects", context.preferences->sfxVolume), 0.0f, 1.0f);
                context.preferences->mouseSensitivity = std::clamp(payload.value("sensitivity", context.preferences->mouseSensitivity), 0.1f, 4.0f);
                context.preferences->invertY = payload.value("invertY", context.preferences->invertY);
                context.preferences->particles = payload.value("particles", context.preferences->particles);
                context.preferences->crosshairSize = std::clamp(payload.value("crosshairSize", context.preferences->crosshairSize), 0.5f, 2.0f);
                context.preferences->highContrastCrosshair = payload.value("crosshairHighContrast", context.preferences->highContrastCrosshair);
                context.preferences->reducedMotion = payload.value("reducedMotion", context.preferences->reducedMotion);
                const std::string heartColor = payload.value("heartColor", context.preferences->heartColor);
                if (heartColor.size() == 7 && heartColor.front() == '#' &&
                    std::all_of(heartColor.begin() + 1, heartColor.end(), [](unsigned char character) {
                        return std::isxdigit(character) != 0;
                    })) {
                    context.preferences->heartColor = heartColor;
                }
            } catch (const nlohmann::json::exception&) {
                RequestError(context, "Settings data was malformed.");
                return true;
            }
            if (context.platform != nullptr && hasDisplaySettings &&
                (context.preferences->windowMode != previous.windowMode ||
                 context.preferences->resolution.width != previous.resolution.width ||
                 context.preferences->resolution.height != previous.resolution.height)) {
                if (!ApplyWindowPreferences(*context.platform, *context.preferences)) {
                    *context.preferences = previous;
                    RequestError(context, "The requested display mode is unavailable on this monitor.");
                    return true;
                }
            }
            if (context.activeGame != nullptr) context.activeGame->ApplyPreferences(*context.preferences);
            if (context.audio != nullptr) context.audio->ApplyVolumes(context.preferences->masterVolume, context.preferences->musicVolume, context.preferences->sfxVolume, 0.7f);
            if (context.platform != nullptr) PreferencesManager(Paths::UserDataDir() / "settings.json", context.platform->GetContext().type).Save(*context.preferences);
            return true;
            }
        case PlayerUIActionKind::AcknowledgeError:
            if ((activeRoute != PlayerUIRoute::Error && activeRoute != PlayerUIRoute::FatalError) ||
                !context.requestTransition) return false;
            context.requestTransition(std::make_unique<MainMenuState>(&context));
            return true;
        case PlayerUIActionKind::Back:
            if (!context.requestTransition && !context.requestPopOverlay) return false;
            if (activeRoute == PlayerUIRoute::Settings || activeRoute == PlayerUIRoute::ControlsCard) {
                if (context.requestPopOverlay) context.requestPopOverlay();
                return true;
            }
            if (activeRoute == PlayerUIRoute::SaveSelection || activeRoute == PlayerUIRoute::Join) {
                context.requestTransition(std::make_unique<MainMenuState>(&context));
                return true;
            }
            if (activeRoute == PlayerUIRoute::WorldCreation) {
                if (context.saveManager != nullptr && context.saveManager->ListSaves().empty()) {
                    context.requestTransition(std::make_unique<MainMenuState>(&context));
                } else {
                    context.requestTransition(std::make_unique<WorldSelectState>(&context));
                }
                return true;
            }
            return false;
        case PlayerUIActionKind::Quit:
            if (activeRoute != PlayerUIRoute::MainMenu || !context.requestQuit) return false;
            context.requestQuit();
            return true;
        case PlayerUIActionKind::HudHotbar:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            return context.activeGame->SelectHotbarSlot(static_cast<int>(std::round(action.value)));
        case PlayerUIActionKind::HudOpenChat:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            context.activeGame->SetChatOpen(true);
            return true;
        case PlayerUIActionKind::HudCloseChat:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            context.activeGame->SetChatOpen(false);
            return true;
        case PlayerUIActionKind::HudSendChat:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            return context.activeGame->SubmitChatMessage(action.primary);
        case PlayerUIActionKind::HudOpenCrafting:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            context.activeGame->SetCraftingOpen(true);
            return true;
        case PlayerUIActionKind::HudCloseCrafting:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            context.activeGame->SetCraftingOpen(false);
            return true;
        case PlayerUIActionKind::HudCraftRecipe:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            // A missing ingredient or full inventory is a valid gameplay outcome that the state
            // reports through the HUD; it is not an invalid web-route action.
            static_cast<void>(context.activeGame->CraftRecipe(action.primary));
            return true;
        case PlayerUIActionKind::HudDropItem:
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            return context.activeGame->DropSlot(static_cast<int>(std::round(action.value)));
        case PlayerUIActionKind::HudMoveItem: {
            if (activeRoute != PlayerUIRoute::Hud || context.activeGame == nullptr) return false;
            int destination = 0;
            const auto parsed = std::from_chars(action.primary.data(), action.primary.data() + action.primary.size(), destination);
            if (parsed.ec != std::errc{}) return false;
            return context.activeGame->MoveInventoryItem(static_cast<int>(std::round(action.value)), destination);
        }
        default: return false;
    }
}

} // namespace voxels