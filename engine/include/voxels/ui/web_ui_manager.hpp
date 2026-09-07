/**
 * @file web_ui_manager.hpp
 * @brief CEF off-screen implementation of the player UI service.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "include/internal/cef_ptr.h"
#include "voxels/ui/player_ui.hpp"
#include "voxels/ui/web_ui_compositor.hpp"

namespace voxels {

/// Hosts the hash-verified local React bundle in a transparent CEF browser.
class WebUIManager final : public IPlayerUI {
public:
    WebUIManager();
    ~WebUIManager() override;
    WebUIManager(const WebUIManager&) = delete;
    WebUIManager& operator=(const WebUIManager&) = delete;

    /// Must run before normal engine startup; returns a child-process exit code or -1 in the browser process.
    [[nodiscard]] static int ExecuteSubprocess(int argc, char** argv);
    bool Initialize(IPlatform* platform, graphics::IGraphicsRenderer* renderer) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    [[nodiscard]] float GetUIScale() const noexcept override { return m_scale; }
    [[nodiscard]] bool IsFrameActive() const noexcept override { return m_frameActive; }
    void OnPlatformEvent(const PlatformEvent& event) override;
    void SetInputPolicy(PlayerUIInputPolicy policy) override;
    [[nodiscard]] PlayerUIInputPolicy GetInputPolicy() const noexcept override { return m_policy; }
    [[nodiscard]] bool CapturesMouse() const noexcept override { return m_policy != PlayerUIInputPolicy::Gameplay; }
    [[nodiscard]] bool CapturesKeyboard() const noexcept override { return m_policy != PlayerUIInputPolicy::Gameplay; }
    [[nodiscard]] bool ConsumeTransitionMouseDelta() noexcept override;
    [[nodiscard]] bool UsesNativeRoutePresentation(PlayerUIRoute route) const noexcept override {
        return route == PlayerUIRoute::Hud || route == PlayerUIRoute::FatalError;
    }
    [[nodiscard]] bool UsesNativeRoutePresentation() const noexcept { return false; }
    void Publish(PlayerUIViewModel model) override;
    [[nodiscard]] std::optional<PlayerUIAction> ConsumeAction() override;
    void ShowToast(std::string message, float durationSeconds = 3.0f) override;
    void ToggleDebugOverlay() noexcept override {}

private:
    class BrowserClient;
    void SubmitAction(PlayerUIAction action);
    void RepublishLatestModel();
    void SyncSurfaceSizeFromPlatform();
    // CEF-managed: CefBrowserHost holds its own reference to the client for the lifetime of the
    // browser, so this must be released via refcounting (CefRefPtr), never destroyed directly
    // while a close is in flight (that segfaults CEF's internal browser teardown).
    CefRefPtr<BrowserClient> m_client;
    WebUiFrameQueue m_frames;
    WebUiOpenGLCompositor m_compositor;
    IPlatform* m_platform = nullptr;
    float m_scale = 1.0f;
    PlayerUIInputPolicy m_policy = PlayerUIInputPolicy::Gameplay;
    std::vector<PlayerUIAction> m_actions;
    std::optional<PlayerUIViewModel> m_lastModel;
    std::string m_entryHtmlPath;
    WindowMetrics m_lastWindowMetrics{};
    bool m_initialized = false;
    bool m_frameActive = false;
    bool m_discardNextMouseDelta = false;
};

} // namespace voxels