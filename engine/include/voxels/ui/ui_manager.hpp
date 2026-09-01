#pragma once

/**
 * @file ui_manager.hpp
 * @brief UI engine abstraction wired to the platform event loop and renderer pipeline.
 *
 * @details `AppStateMachine` states submit UI through an `IUIManager`. The desktop runtime
 *          uses `ImGuiUIManager` (imgui_ui_manager.hpp); this header retains
 *          `NullUIManager` exclusively for headless tests that need an `IUIManager`
 *          without a window or OpenGL context, plus the shared `ComputeUIScale` helper.
 */

#include "voxels/graphics/renderer.hpp"
#include "voxels/platform/platform.hpp"

namespace voxels {

/// Display parameters that drive UI layout scaling.
struct UIDisplayMetrics {
    int windowWidth = 1280;
    int windowHeight = 720;
    float dpiScale = 1.0f;
};

/// Computes a uniform UI scale factor relative to a reference resolution (defaults to the
/// fixed 640x480 Dreamcast output) so a single set of layouts responsively scales across
/// high-DPI desktop displays and small fixed console framebuffers alike. Result is
/// min(w/baseW, h/baseH) * dpiScale; non-positive dimensions yield dpiScale unchanged.
[[nodiscard]] float ComputeUIScale(const UIDisplayMetrics& metrics, int baseWidth = 640,
                                    int baseHeight = 480) noexcept;

/// Frame-scoped UI service. Listens to platform events (e.g. resize) to keep its scale
/// current; all UI submission must happen between `BeginFrame` and `EndFrame`.
class IUIManager : public IPlatformEventListener {
public:
    ~IUIManager() override = default;
    /// Binds the manager to a platform and renderer (both borrowed); false on failure.
    /// Must be called before any frame methods.
    virtual bool Initialize(IPlatform* platform, IRenderer* renderer) = 0;
    /// Releases backend resources; safe to call more than once.
    virtual void Shutdown() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    /// Current uniform layout scale (see `ComputeUIScale`); updated on window resize.
    [[nodiscard]] virtual float GetUIScale() const noexcept = 0;
    /// True between `BeginFrame` and `EndFrame`, when UI submission is legal.
    [[nodiscard]] virtual bool IsFrameActive() const noexcept = 0;
};

/// Headless test implementation. It never renders and must not be constructed by the desktop
/// application, which always uses `ImGuiUIManager`.
class NullUIManager final : public IUIManager {
public:
    NullUIManager() = default;

    bool Initialize(IPlatform* platform, IRenderer* renderer) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    [[nodiscard]] float GetUIScale() const noexcept override { return m_scale; }
    [[nodiscard]] bool IsFrameActive() const noexcept override { return m_frameActive; }
    [[nodiscard]] const UIDisplayMetrics& GetMetrics() const noexcept { return m_metrics; }

    void OnPlatformEvent(const PlatformEvent& event) override;

private:
    IPlatform* m_platform = nullptr;
    IRenderer* m_renderer = nullptr;
    UIDisplayMetrics m_metrics;
    float m_scale = 1.0f;
    bool m_initialized = false;
    bool m_frameActive = false;
};

} // namespace voxels
