#pragma once

/**
 * @file dreamcast_platform.hpp
 * @brief `IPlatform` stub targeting Sega Dreamcast/KallistiOS hardware output.
 *
 * @details The Dreamcast has a single fixed 640x480 @ 60Hz VGA/NTSC display with no desktop
 *          windowing semantics, so this implementation ignores requested resolution/fullscreen
 *          changes and reports the fixed mode. Real KallistiOS integration (video mode setup,
 *          controller/event polling via KOS) is intentionally left as a follow-up hardware
 *          bring-up task; this stub keeps the engine buildable and testable for this target
 *          without the KOS toolchain present. Only compiled when `VOXELS_ENABLE_DREAMCAST`
 *          is set.
 */

#ifdef VOXELS_ENABLE_DREAMCAST

#include "voxels/platform/platform.hpp"

namespace voxels {

class DreamcastPlatform final : public IPlatform {
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

    static constexpr int kFixedWidth = 640;
    static constexpr int kFixedHeight = 480;
    static constexpr int kFixedRefreshRate = 60;

private:
    bool m_initialized = false;
};

} // namespace voxels

#endif // VOXELS_ENABLE_DREAMCAST
