#pragma once

/*
 * Scope: UI engine abstraction wired to the platform event loop and renderer pipeline.
 *
 * The desktop runtime uses `ImGuiUIManager`; this header retains `NullUIManager` exclusively
 * for headless tests that need an `IUIManager` without a window or OpenGL context.
 *
 * Relation to the rest of the codebase: `AppStateMachine` states submit UI through an
 * `IUIManager`; `ImGuiUIManager` is the shipping implementation.
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
/// high-DPI desktop displays and small fixed console framebuffers alike.
[[nodiscard]] float ComputeUIScale(const UIDisplayMetrics& metrics, int baseWidth = 640,
                                    int baseHeight = 480) noexcept;

class IUIManager : public IPlatformEventListener {
public:
    ~IUIManager() override = default;
    virtual bool Initialize(IPlatform* platform, IRenderer* renderer) = 0;
    virtual void Shutdown() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    [[nodiscard]] virtual float GetUIScale() const noexcept = 0;
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
