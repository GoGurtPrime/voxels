/**
 * @file sdl_platform.cpp
 * @brief Implements `SDLPlatform`, translating SDL2 window/quit events into `PlatformEvent`s.
 */

#ifdef VOXELS_HAS_SDL2

#include "voxels/platform/sdl_platform.hpp"

#include <algorithm>
#include <glad/glad.h>

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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        return false;
    }

    m_graphicsApi = config.graphicsApi;
    if (m_graphicsApi == WindowGraphicsApi::OpenGL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
    }

    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (m_graphicsApi == WindowGraphicsApi::OpenGL) flags |= SDL_WINDOW_OPENGL;
    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    m_window = SDL_CreateWindow(config.title.c_str(), SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, config.width, config.height, flags);
    if (m_window == nullptr) {
        SDL_Quit();
        return false;
    }

    if (m_graphicsApi == WindowGraphicsApi::OpenGL) {
        m_context = SDL_GL_CreateContext(m_window);
        if (m_context == nullptr || SDL_GL_MakeCurrent(m_window, m_context) != 0 ||
            !gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            if (m_context != nullptr) SDL_GL_DeleteContext(m_context);
            m_context = nullptr;
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
            SDL_Quit();
            return false;
        }
    }

    SDL_ShowWindow(m_window);

    m_title = config.title;
    m_width = config.width;
    m_height = config.height;
    m_fullscreen = config.fullscreen;
    m_borderless = false;
    m_resizable = config.resizable;
    m_perfFrequency = SDL_GetPerformanceFrequency();
    SetVSync(m_vsync);
    return true;
}

void SDLPlatform::Shutdown() {
    if (m_context != nullptr) {
        SDL_GL_DeleteContext(m_context);
        m_context = nullptr;
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
    m_listeners.clear();
    m_defaultListener = nullptr;
}

void SDLPlatform::PollEvents(IPlatformEventListener* listener) {
    if (listener != nullptr) {
        m_defaultListener = listener;
        m_defaultListenerPriority = 0;
    }

    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent) != 0) {
        PlatformEvent event{};

        switch (sdlEvent.type) {
            case SDL_QUIT:
                event.type = PlatformEventType::QuitRequested;
                break;
            case SDL_WINDOWEVENT:
                switch (sdlEvent.window.event) {
                    case SDL_WINDOWEVENT_CLOSE:
                        event.type = PlatformEventType::WindowClosed;
                        break;
                    case SDL_WINDOWEVENT_RESIZED:
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        event.type = PlatformEventType::WindowResized;
                        event.width = sdlEvent.window.data1;
                        event.height = sdlEvent.window.data2;
                        m_width = event.width;
                        m_height = event.height;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        event.type = PlatformEventType::WindowFocusGained;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        event.type = PlatformEventType::WindowFocusLost;
                        break;
                    case SDL_WINDOWEVENT_MINIMIZED:
                        event.type = PlatformEventType::WindowMinimized;
                        break;
                    case SDL_WINDOWEVENT_RESTORED:
                        event.type = PlatformEventType::WindowRestored;
                        break;
                    default:
                        break;
                }
                break;
            case SDL_KEYDOWN:
                event.type = PlatformEventType::KeyDown;
                event.keyCode = static_cast<std::uint32_t>(sdlEvent.key.keysym.sym);
                event.scancode = static_cast<std::uint32_t>(sdlEvent.key.keysym.scancode);
                event.modifiers = static_cast<std::uint32_t>(SDL_GetModState());
                event.repeat = sdlEvent.key.repeat != 0;
                break;
            case SDL_KEYUP:
                event.type = PlatformEventType::KeyUp;
                event.keyCode = static_cast<std::uint32_t>(sdlEvent.key.keysym.sym);
                event.scancode = static_cast<std::uint32_t>(sdlEvent.key.keysym.scancode);
                event.modifiers = static_cast<std::uint32_t>(SDL_GetModState());
                event.repeat = sdlEvent.key.repeat != 0;
                break;
            case SDL_TEXTINPUT:
                event.type = PlatformEventType::TextInput;
                event.text = sdlEvent.text.text;
                break;
            case SDL_MOUSEMOTION:
                event.type = PlatformEventType::MouseMotion;
                event.x = sdlEvent.motion.x;
                event.y = sdlEvent.motion.y;
                event.relativeX = sdlEvent.motion.xrel;
                event.relativeY = sdlEvent.motion.yrel;
                break;
            case SDL_MOUSEBUTTONDOWN:
                event.type = PlatformEventType::MouseButtonDown;
                event.button = sdlEvent.button.button;
                event.x = sdlEvent.button.x;
                event.y = sdlEvent.button.y;
                break;
            case SDL_MOUSEBUTTONUP:
                event.type = PlatformEventType::MouseButtonUp;
                event.button = sdlEvent.button.button;
                event.x = sdlEvent.button.x;
                event.y = sdlEvent.button.y;
                break;
            case SDL_MOUSEWHEEL:
                event.type = PlatformEventType::MouseWheel;
                event.wheelX = sdlEvent.wheel.x;
                event.wheelY = sdlEvent.wheel.y;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                event.type = PlatformEventType::ControllerAdded;
                event.controllerIndex = sdlEvent.cdevice.which;
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                event.type = PlatformEventType::ControllerRemoved;
                event.controllerIndex = sdlEvent.cdevice.which;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                event.type = PlatformEventType::ControllerButton;
                event.button = sdlEvent.cbutton.button;
                event.controllerIndex = sdlEvent.cbutton.which;
                break;
            case SDL_CONTROLLERAXISMOTION:
                event.type = PlatformEventType::ControllerAxis;
                event.controllerIndex = sdlEvent.caxis.which;
                event.axisValue = static_cast<float>(sdlEvent.caxis.value) / 32768.0f;
                event.x = sdlEvent.caxis.axis;
                break;
            default:
                continue;
        }

        if (event.type == PlatformEventType::None) {
            continue;
        }

        std::vector<ListenerEntry> ordered = m_listeners;
        if (m_defaultListener != nullptr) {
            ordered.push_back({m_defaultListener, m_defaultListenerPriority});
        }
        std::sort(ordered.begin(), ordered.end(), [](const ListenerEntry& lhs, const ListenerEntry& rhs) {
            return lhs.priority > rhs.priority;
        });

        for (const auto& entry : ordered) {
            if (entry.listener != nullptr) {
                entry.listener->OnPlatformEvent(event);
            }
        }
    }
}

void SDLPlatform::SwapBuffers() {
    if (m_window != nullptr && m_graphicsApi == WindowGraphicsApi::OpenGL) {
        SDL_GL_SwapWindow(m_window);
    }
}

void SDLPlatform::SetWindowFullscreen(bool fullscreen) {
    m_fullscreen = fullscreen;
    if (m_window != nullptr) {
        SDL_SetWindowFullscreen(m_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
}

void SDLPlatform::SetWindowBorderless(bool borderless) {
    m_borderless = borderless;
    if (m_window != nullptr) {
        SDL_SetWindowBordered(m_window, borderless ? SDL_FALSE : SDL_TRUE);
    }
}

void SDLPlatform::SetWindowResizable(bool resizable) {
    m_resizable = resizable;
    if (m_window != nullptr) {
        SDL_SetWindowResizable(m_window, resizable ? SDL_TRUE : SDL_FALSE);
    }
}

void SDLPlatform::SetWindowResolution(int width, int height) {
    if (m_window != nullptr) {
        m_width = width;
        m_height = height;
        SDL_SetWindowSize(m_window, width, height);
    }
}

void SDLPlatform::SetWindowTitle(const std::string& title) {
    m_title = title;
    if (m_window != nullptr) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

void SDLPlatform::SetRelativeMouseMode(bool enabled) {
    if (m_window != nullptr) {
        SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
    }
}

void SDLPlatform::SetCursorVisible(bool visible) {
    if (m_window != nullptr) {
        SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
    }
}

void SDLPlatform::SetVSync(bool enabled) {
    m_vsync = enabled;
    if (m_window != nullptr && m_graphicsApi == WindowGraphicsApi::OpenGL) {
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
    }
}

std::pair<int, int> SDLPlatform::GetDrawableSize() const {
    int width = m_width;
    int height = m_height;
    if (m_window != nullptr && m_graphicsApi == WindowGraphicsApi::OpenGL) {
        SDL_GL_GetDrawableSize(m_window, &width, &height);
    } else if (m_window != nullptr) {
        SDL_GetWindowSize(m_window, &width, &height);
    }
    return {width, height};
}

double SDLPlatform::GetHighResTimeSeconds() const {
    if (m_perfFrequency == 0) {
        return 0.0;
    }
    return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(m_perfFrequency);
}

void SDLPlatform::RegisterEventListener(IPlatformEventListener* listener, int priority) {
    if (listener == nullptr) {
        return;
    }
    auto it = std::find_if(m_listeners.begin(), m_listeners.end(), [listener](const ListenerEntry& entry) {
        return entry.listener == listener;
    });
    if (it == m_listeners.end()) {
        m_listeners.push_back({listener, priority});
    } else {
        it->priority = priority;
    }
}

void SDLPlatform::UnregisterEventListener(IPlatformEventListener* listener) {
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(), [listener](const ListenerEntry& entry) {
        return entry.listener == listener;
    }), m_listeners.end());
    if (m_defaultListener == listener) {
        m_defaultListener = nullptr;
    }
}

} // namespace voxels

#endif // VOXELS_HAS_SDL2
