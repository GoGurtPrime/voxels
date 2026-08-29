/**
 * @file sdl_platform.cpp
 * @brief Implements `SDLPlatform`, translating SDL2 window/quit events into `PlatformEvent`s.
 */

#ifdef VOXELS_HAS_SDL2

#include "voxels/platform/sdl_platform.hpp"

namespace voxels {

SDLPlatform::~SDLPlatform() {
    Shutdown();
}

PlatformContext SDLPlatform::GetContext() const {
    PlatformContext context;
#if defined(_WIN32)
    context.type = PlatformType::Windows;
#elif defined(__APPLE__)
    context.type = PlatformType::MacOS;
#elif defined(__linux__)
    context.type = PlatformType::Linux;
#else
    context.type = PlatformType::Unknown;
#endif
    context.name = "SDL2";
    return context;
}

bool SDLPlatform::Initialize(const WindowConfig& config) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    Uint32 flags = SDL_WINDOW_OPENGL;
    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    m_window = SDL_CreateWindow(config.title.c_str(), SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, config.width, config.height, flags);
    if (m_window == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    m_perfFrequency = SDL_GetPerformanceFrequency();
    return true;
}

void SDLPlatform::Shutdown() {
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

void SDLPlatform::PollEvents(IPlatformEventListener* listener) {
    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent) != 0) {
        if (listener == nullptr) {
            continue;
        }

        if (sdlEvent.type == SDL_QUIT) {
            listener->OnPlatformEvent(PlatformEvent{PlatformEventType::WindowClosed, 0, 0});
        } else if (sdlEvent.type == SDL_WINDOWEVENT) {
            switch (sdlEvent.window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                    listener->OnPlatformEvent(PlatformEvent{PlatformEventType::WindowClosed, 0, 0});
                    break;
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    listener->OnPlatformEvent(PlatformEvent{PlatformEventType::WindowResized,
                                                             sdlEvent.window.data1,
                                                             sdlEvent.window.data2});
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED:
                    listener->OnPlatformEvent(
                        PlatformEvent{PlatformEventType::WindowFocusGained, 0, 0});
                    break;
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    listener->OnPlatformEvent(
                        PlatformEvent{PlatformEventType::WindowFocusLost, 0, 0});
                    break;
                default:
                    break;
            }
        }
    }
}

void SDLPlatform::SwapBuffers() {
    if (m_window != nullptr) {
        SDL_GL_SwapWindow(m_window);
    }
}

void SDLPlatform::SetWindowFullscreen(bool fullscreen) {
    if (m_window != nullptr) {
        SDL_SetWindowFullscreen(m_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
}

void SDLPlatform::SetWindowResolution(int width, int height) {
    if (m_window != nullptr) {
        SDL_SetWindowSize(m_window, width, height);
    }
}

double SDLPlatform::GetHighResTimeSeconds() const {
    if (m_perfFrequency == 0) {
        return 0.0;
    }
    return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(m_perfFrequency);
}

} // namespace voxels

#endif // VOXELS_HAS_SDL2
