#pragma once

/**
 * @file headless_platform.hpp
 * @brief In-memory `IPlatform` fallback with no dependency on a native windowing library.
 *
 * @details Used whenever SDL2 is not linked into the build (e.g. minimal CI/test environments)
 *          and as the default platform for headless automated testing. Simulates window state
 *          and lets callers (typically tests) inject `PlatformEvent`s to exercise the same
 *          `PollEvents` dispatch contract that real backends (SDL2, Dreamcast) fulfill.
 */

#include <deque>
#include <vector>

#include "voxels/platform/platform.hpp"

namespace voxels {

class HeadlessPlatform final : public IPlatform {
public:
    PlatformContext GetContext() const override;
    bool Initialize(const WindowConfig& config) override;
    void Shutdown() override;
    void PollEvents(IPlatformEventListener* listener) override;
    void SwapBuffers() override;
    void SetWindowFullscreen(bool fullscreen) override;
    void SetWindowBorderless(bool borderless) override;
    void SetWindowResizable(bool resizable) override;
    void SetWindowResolution(int width, int height) override;
    void SetWindowTitle(const std::string& title) override;
    void SetRelativeMouseMode(bool enabled) override;
    void SetCursorVisible(bool visible) override;
    void SetVSync(bool enabled) override;
    std::pair<int, int> GetDrawableSize() const override;
    double GetHighResTimeSeconds() const override;
    void RegisterEventListener(IPlatformEventListener* listener, int priority = 0) override;
    void UnregisterEventListener(IPlatformEventListener* listener) override;

    /// Test/tooling hook: enqueues an event to be delivered on the next `PollEvents` call.
    void SimulateEvent(const PlatformEvent& event);

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }
    [[nodiscard]] bool IsFullscreen() const noexcept { return m_fullscreen; }

private:
    struct ListenerEntry {
        IPlatformEventListener* listener = nullptr;
        int priority = 0;
    };

    bool m_initialized = false;
    int m_width = 1280;
    int m_height = 720;
    bool m_fullscreen = false;
    bool m_borderless = false;
    bool m_resizable = true;
    bool m_cursorVisible = true;
    bool m_relativeMouseMode = false;
    std::deque<PlatformEvent> m_eventQueue;
    std::vector<ListenerEntry> m_listeners;
    int m_defaultListenerPriority = 0;
    IPlatformEventListener* m_defaultListener = nullptr;
};

} // namespace voxels
