#pragma once

/**
 * @file player_ui.hpp
 * @brief Backend-neutral player UI snapshots, actions, protocol codec, and input ownership.
 *
 * @details Defines the main-thread contract between application states and UI backends described
 *          in work_items/02_player_ui_contract_and_protocol.md. Web backends can consume this
 *          interface without including SDL, OpenGL, or Dear ImGui headers.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "voxels/ui/ui_manager.hpp"

namespace voxels {

enum class PlayerUIRoute : std::uint8_t { MainMenu, SaveSelection, WorldCreation, Loading, Join, Error, Pause, Settings, ControlsCard, Hud, FatalError, Splash };
enum class PlayerUIInputPolicy : std::uint8_t { Gameplay, Overlay, TextEntry };

/// Immutable value snapshot submitted to the active UI route on the main thread.
struct PlayerUIViewModel {
    PlayerUIRoute route = PlayerUIRoute::MainMenu;
    std::uint64_t revision = 0;
    std::string title;
    std::string message;
    std::vector<std::string> items;
    /// Optional JSON object string carrying route-specific state.
    std::string payload;
    float progress = 0.0f;
    bool blocking = false;
};

enum class PlayerUIActionKind : std::uint8_t {
    Play,
    CreateWorld,
    LoadWorld,
    DeleteWorld,
    ConfirmDelete,
    Join,
    Resume,
    OpenSettings,
    OpenControls,
    ToggleWorldVisibility,
    ReturnToMainMenu,
    ExitToDesktop,
    Back,
    Quit,
    ApplySettings,
    DismissControls,
    HudHotbar,
    HudOpenChat,
    HudCloseChat,
    HudSendChat,
    HudOpenCrafting,
    HudCloseCrafting,
    HudCraftRecipe,
    AcknowledgeError
};

struct PlayerUIAction {
    std::uint32_t requestId = 0;
    PlayerUIActionKind kind = PlayerUIActionKind::Back;
    std::string primary;
    std::string secondary;
    float value = 0.0f;
};

/// JSON envelope accepted across the native/web boundary. Only this four-field envelope is serialized.
struct PlayerUIProtocolMessage {
    static constexpr std::uint32_t kVersion = 1;
    std::uint32_t version = kVersion;
    std::string kind;
    std::uint32_t requestId = 0;
    std::string payload;
};

[[nodiscard]] std::optional<std::string> EncodePlayerUIProtocolMessage(const PlayerUIProtocolMessage& message);
[[nodiscard]] std::optional<PlayerUIProtocolMessage> DecodePlayerUIProtocolMessage(std::string_view encoded);

/// UI service extension consumed by application state and platform routing, independent of backend.
class IPlayerUI : public IUIManager {
public:
    ~IPlayerUI() override = default;
    virtual void SetInputPolicy(PlayerUIInputPolicy policy) = 0;
    [[nodiscard]] virtual PlayerUIInputPolicy GetInputPolicy() const noexcept = 0;
    [[nodiscard]] virtual bool CapturesMouse() const noexcept = 0;
    [[nodiscard]] virtual bool CapturesKeyboard() const noexcept = 0;
    [[nodiscard]] virtual bool ConsumeTransitionMouseDelta() noexcept = 0;
    [[nodiscard]] virtual bool UsesNativeRoutePresentation(PlayerUIRoute route) const noexcept {
        (void)route;
        return true;
    }
    virtual void Publish(PlayerUIViewModel model) = 0;
    [[nodiscard]] virtual std::optional<PlayerUIAction> ConsumeAction() = 0;
    virtual void ShowToast(std::string message, float durationSeconds = 3.0f) = 0;
    virtual void ToggleDebugOverlay() noexcept = 0;
};

/// Headless player-UI double retaining snapshots/actions for protocol and app-state tests.
class NullPlayerUI final : public IPlayerUI {
public:
    bool Initialize(IPlatform* platform, graphics::IGraphicsRenderer* renderer) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    [[nodiscard]] float GetUIScale() const noexcept override;
    [[nodiscard]] bool IsFrameActive() const noexcept override;
    void OnPlatformEvent(const PlatformEvent& event) override;
    void SetInputPolicy(PlayerUIInputPolicy policy) override;
    [[nodiscard]] PlayerUIInputPolicy GetInputPolicy() const noexcept override { return m_policy; }
    [[nodiscard]] bool CapturesMouse() const noexcept override { return m_policy != PlayerUIInputPolicy::Gameplay; }
    [[nodiscard]] bool CapturesKeyboard() const noexcept override { return m_policy != PlayerUIInputPolicy::Gameplay; }
    [[nodiscard]] bool ConsumeTransitionMouseDelta() noexcept override;
    void Publish(PlayerUIViewModel model) override;
    [[nodiscard]] std::optional<PlayerUIAction> ConsumeAction() override;
    void ShowToast(std::string message, float durationSeconds) override;
    void ToggleDebugOverlay() noexcept override {}
    void SubmitAction(PlayerUIAction action);
    [[nodiscard]] const std::optional<PlayerUIViewModel>& LastModel() const noexcept { return m_lastModel; }

private:
    NullUIManager m_base;
    PlayerUIInputPolicy m_policy = PlayerUIInputPolicy::Gameplay;
    bool m_discardNextMouseDelta = false;
    std::optional<PlayerUIViewModel> m_lastModel;
    std::vector<PlayerUIAction> m_actions;
};

} // namespace voxels