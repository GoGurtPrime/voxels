#pragma once

/**
 * @file imgui_ui_manager.hpp
 * @brief SDL2/OpenGL Dear ImGui UI pass and input arbitration.
 *
 * @details Implements the final UI pass from DIAGRAMS.md section 6.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "voxels/ui/player_ui.hpp"

namespace voxels {

struct UIDebugMetrics {
    float frameMilliseconds = 0.0f;
    float framesPerSecond = 0.0f;
    float playerX = 0.0f;
    float playerY = 0.0f;
    float playerZ = 0.0f;
    int chunkX = 0;
    int chunkY = 0;
    int chunkZ = 0;
    std::size_t loadedChunks = 0;
    std::size_t meshedChunks = 0;
    std::size_t visibleChunks = 0;
    std::size_t drawCalls = 0;
    std::size_t triangles = 0;
    std::size_t meshQueueDepth = 0;
    std::uint64_t worldTick = 0;
    std::uint64_t worldDay = 0;
    float worldDayFraction = 0.0f;
    std::string glVendor;
    std::string glRenderer;
    std::string glVersion;
};

class ImGuiUIManager final : public IPlayerUI {
public:
    bool Initialize(IPlatform* platform, graphics::IGraphicsRenderer* renderer) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    [[nodiscard]] float GetUIScale() const noexcept override { return m_scale; }
    [[nodiscard]] bool IsFrameActive() const noexcept override { return m_frameActive; }
    [[nodiscard]] bool CapturesMouse() const noexcept override;
    [[nodiscard]] bool CapturesKeyboard() const noexcept override;
    void SetInputPolicy(PlayerUIInputPolicy policy) override;
    [[nodiscard]] PlayerUIInputPolicy GetInputPolicy() const noexcept override { return m_policy; }
    [[nodiscard]] bool ConsumeTransitionMouseDelta() noexcept override;
    void Publish(PlayerUIViewModel model) override;
    [[nodiscard]] std::optional<PlayerUIAction> ConsumeAction() override;
    void SetDebugMetrics(UIDebugMetrics metrics);
    [[nodiscard]] const UIDebugMetrics& GetDebugMetrics() const noexcept { return m_debugMetrics; }
    void ToggleDebugOverlay() noexcept override { m_debugOverlayVisible = !m_debugOverlayVisible; }
    void ShowError(std::string title, std::string detail);
    void ShowToast(std::string message, float durationSeconds = 3.0f) override;
    void OnPlatformEvent(const PlatformEvent& event) override;

private:
    struct ToastMessage { std::string text; float remainingSeconds = 0.0f; };
    void RebuildFonts();
    void RenderDebugOverlay();
    void RenderErrorModal();
    void RenderToasts();
    IPlatform* m_platform = nullptr;
    UIDisplayMetrics m_metrics{};
    UIDebugMetrics m_debugMetrics{};
    std::vector<float> m_frameHistory;
    std::vector<ToastMessage> m_toasts;
    std::string m_errorTitle;
    std::string m_errorDetail;
    float m_scale = 1.0f;
    PlayerUIInputPolicy m_policy = PlayerUIInputPolicy::Gameplay;
    std::optional<PlayerUIViewModel> m_lastModel;
    std::vector<PlayerUIAction> m_actions;
    bool m_initialized = false;
    bool m_frameActive = false;
    bool m_discardNextMouseDelta = false;
    bool m_debugOverlayVisible = false;
    bool m_fontWarningReported = false;
    RendererBackend m_rendererBackend = RendererBackend::OpenGL;
};

void ApplyVoxelsTheme();

namespace ui {
bool MenuButton(const char* label, bool enabled = true);
void MenuTitle(const char* title);
bool SettingSlider(const char* label, float* value, float minimum, float maximum, const char* format = "%.0f");
bool SettingPercentSlider(const char* label, float* normalizedValue);
bool SettingToggle(const char* label, bool* value);
bool SettingDropdown(const char* label, int* currentItem, const char* const items[], int itemCount);
bool KeyBindRow(const char* label, int* key);
bool TextField(const char* label, std::string& value, std::size_t capacity = 256);
bool ConfirmDialog(const char* title, const char* detail, bool* open);
void ProgressBar(float fraction, const char* overlay = nullptr);
bool SaveListEntry(const char* label, bool selected);
void Toast(const char* message);
} // namespace ui

} // namespace voxels