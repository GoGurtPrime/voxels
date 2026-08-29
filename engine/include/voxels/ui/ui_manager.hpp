#pragma once

/*
 * Scope: UI engine abstraction wired to the platform event loop and renderer pipeline.
 *
 * A real Dear ImGui backend depends on a concrete windowing/graphics implementation (SDL2 +
 * a GPU-backed `IRenderer`), neither of which is linked in every build configuration used by
 * this repository's headless CI/test environment. This header therefore defines the engine
 * side of the integration (`IUIManager`) plus a procedural `UIManager` implementation that
 * tracks display metrics and DPI/output scaling without depending on any ImGui headers,
 * mirroring the `IPlatform`/`HeadlessPlatform` and `IRenderer`/`MockRenderer` pattern already
 * used elsewhere in the engine. A real ImGui-backed manager can implement the same `IUIManager`
 * contract once a concrete graphics backend is wired in.
 *
 * Relation to the rest of the codebase: `AppStateMachine` states call into an `IUIManager` to
 * begin/end UI frames each tick; `IPlatform::PollEvents` notifies it of resizes so it can
 * recompute layout scale for high-DPI displays and the fixed 640x480 Dreamcast output.
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

/// Procedural (non-ImGui) UI manager. Tracks the platform's current window size via
/// `OnPlatformEvent` and derives `GetUIScale()` from it; `BeginFrame`/`EndFrame` bracket a
/// UI pass around the renderer's own frame so a real ImGui draw pass can be slotted in later
/// without changing this class's public contract.
class UIManager final : public IUIManager {
public:
    UIManager() = default;

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
