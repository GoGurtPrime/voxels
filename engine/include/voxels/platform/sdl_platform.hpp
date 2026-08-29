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
    double GetHighResTimeSeconds() const override;

private:
    SDL_Window* m_window = nullptr;
    Uint64 m_perfFrequency = 0;
};

} // namespace voxels

#endif // VOXELS_HAS_SDL2
