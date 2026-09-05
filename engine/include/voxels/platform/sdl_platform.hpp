#pragma once

/**
 * @file sdl_platform.hpp
 * @brief SDL2-backed `IPlatform` implementation for Windows/Mac/Linux desktop targets.
 *
 * @details Wraps `SDL_CreateWindow`, the SDL event pump, and `SDL_GetPerformanceCounter` to
 *          fulfill the `IPlatform` contract. Graphics context/surface creation for a specific
 *          rendering backend (Vulkan/DirectX/Metal) is handled by the renderer abstraction
 *          (see Work Item 04) rather than this class. Only compiled when SDL2 is linked in
 *          (`VOXELS_HAS_SDL2`), so non-desktop builds (e.g. Dreamcast) never require it.
 */

#ifdef VOXELS_HAS_SDL2

#include <SDL.h>

#include "voxels/platform/platform.hpp"

namespace voxels {

class SDLPlatform final : public IPlatform {
public:
    ~SDLPlatform() override;

    PlatformContext GetContext() const override;
    bool Initialize(const WindowConfig& config) override;
    void Shutdown() override;
    void PollEvents(IPlatformEventListener* listener) override;
    void SwapBuffers() override;
    void SetWindowFullscreen(bool fullscreen) override;
    void SetWindowResolution(int width, int height) override;
    void SetWindowTitle(const std::string& title) override;
    void SetRelativeMouseMode(bool enabled) override;
    void SetCursorVisible(bool visible) override;
    void SetVSync(bool enabled) override;
    std::pair<int, int> GetDrawableSize() const override;
    [[nodiscard]] void* GetNativeWindowHandle() const noexcept override { return m_window; }
    double GetHighResTimeSeconds() const override;
    void RegisterEventListener(IPlatformEventListener* listener, int priority = 0) override;
    void UnregisterEventListener(IPlatformEventListener* listener) override;

private:
    struct ListenerEntry {
        IPlatformEventListener* listener = nullptr;
        int priority = 0;
    };

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    Uint64 m_perfFrequency = 0;
    std::string m_title;
    int m_width = 1280;
    int m_height = 720;
    bool m_fullscreen = false;
    bool m_vsync = true;
    WindowGraphicsApi m_graphicsApi = WindowGraphicsApi::OpenGL;
    std::vector<ListenerEntry> m_listeners;
    IPlatformEventListener* m_defaultListener = nullptr;
    int m_defaultListenerPriority = 0;
};

} // namespace voxels

#endif // VOXELS_HAS_SDL2
