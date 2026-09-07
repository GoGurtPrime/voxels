/**
 * @file sdl_platform.cpp
 * @brief Implements `SDLPlatform`, translating SDL2 window/quit events into `PlatformEvent`s.
 */

#ifdef VOXELS_HAS_SDL2

#include "voxels/platform/sdl_platform.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <glad/glad.h>

#include "voxels/core/logger.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <SDL_syswm.h>
#endif

namespace {

std::optional<SDL_DisplayMode> ExactDisplayMode(SDL_Window* window, int width, int height) {
    if (window == nullptr) return std::nullopt;
    const int displayIndex = SDL_GetWindowDisplayIndex(window);
    if (displayIndex < 0) return std::nullopt;

    SDL_DisplayMode desktop{};
    static_cast<void>(SDL_GetDesktopDisplayMode(displayIndex, &desktop));
    std::optional<SDL_DisplayMode> best;
    int bestScore = std::numeric_limits<int>::min();
    const int modeCount = SDL_GetNumDisplayModes(displayIndex);
    for (int index = 0; index < modeCount; ++index) {
        SDL_DisplayMode candidate{};
        if (SDL_GetDisplayMode(displayIndex, index, &candidate) != 0 ||
            candidate.w != width || candidate.h != height) {
            continue;
        }
        const int refreshDifference = desktop.refresh_rate > 0 && candidate.refresh_rate > 0
                                          ? std::abs(candidate.refresh_rate - desktop.refresh_rate)
                                          : 0;
        const int score = (candidate.format == desktop.format ? 100000 : 0) - refreshDifference;
        if (!best.has_value() || score > bestScore) {
            best = candidate;
            bestScore = score;
        }
    }
    return best;
}

bool IsWindowsAdvancedColorEnabled(SDL_Window* window) noexcept {
#if defined(_WIN32)
    if (window == nullptr) return false;

    SDL_SysWMinfo windowInfo{};
    SDL_VERSION(&windowInfo.version);
    if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE) return false;

    const HMONITOR monitor = MonitorFromWindow(windowInfo.info.win.window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor == nullptr || GetMonitorInfoW(monitor, &monitorInfo) == FALSE) return false;

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return false;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(),
                           nullptr) != ERROR_SUCCESS) {
        return false;
    }

    for (UINT32 index = 0; index < pathCount; ++index) {
        const DISPLAYCONFIG_PATH_INFO& path = paths[index];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = path.sourceInfo.adapterId;
        sourceName.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS ||
            _wcsicmp(sourceName.viewGdiDeviceName, monitorInfo.szDevice) != 0) {
            continue;
        }

        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
        colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        colorInfo.header.size = sizeof(colorInfo);
        colorInfo.header.adapterId = path.targetInfo.adapterId;
        colorInfo.header.id = path.targetInfo.id;
        return DisplayConfigGetDeviceInfo(&colorInfo.header) == ERROR_SUCCESS &&
               colorInfo.advancedColorSupported != 0U && colorInfo.advancedColorEnabled != 0U;
    }
#else
    static_cast<void>(window);
#endif
    return false;
}

std::uint32_t NormalizeModifiers(const SDL_Keymod keyModifiers, const Uint32 mouseButtons) noexcept {
    std::uint32_t modifiers = voxels::PlatformModifierNone;
    if ((keyModifiers & KMOD_SHIFT) != 0) modifiers |= voxels::PlatformModifierShift;
    if ((keyModifiers & KMOD_CTRL) != 0) modifiers |= voxels::PlatformModifierControl;
    if ((keyModifiers & KMOD_ALT) != 0) modifiers |= voxels::PlatformModifierAlt;
    if ((keyModifiers & KMOD_GUI) != 0) modifiers |= voxels::PlatformModifierSuper;
    if ((keyModifiers & KMOD_CAPS) != 0) modifiers |= voxels::PlatformModifierCapsLock;
    if ((keyModifiers & KMOD_NUM) != 0) modifiers |= voxels::PlatformModifierNumLock;
    if ((mouseButtons & SDL_BUTTON_LMASK) != 0U) modifiers |= voxels::PlatformModifierLeftMouse;
    if ((mouseButtons & SDL_BUTTON_MMASK) != 0U) modifiers |= voxels::PlatformModifierMiddleMouse;
    if ((mouseButtons & SDL_BUTTON_RMASK) != 0U) modifiers |= voxels::PlatformModifierRightMouse;
    return modifiers;
}

} // namespace

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
        int framebufferSrgbCapable = 0;
        if (SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &framebufferSrgbCapable) != 0 ||
            framebufferSrgbCapable == 0) {
            SDL_GL_DeleteContext(m_context);
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
    m_requestedWidth = config.width;
    m_requestedHeight = config.height;
    m_fullscreen = false;
    m_borderless = false;
    m_resizable = config.resizable;
    m_perfFrequency = SDL_GetPerformanceFrequency();
    if (!ApplyWindowDisplayConfig({config.width, config.height,
                                   config.fullscreen ? WindowPresentationMode::Fullscreen
                                                     : WindowPresentationMode::Windowed,
                                   config.resizable})) {
        Shutdown();
        return false;
    }
    SetVSync(m_vsync);
    return true;
}

void SDLPlatform::Shutdown() {
    SDL_StopTextInput();
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
#if SDL_VERSION_ATLEAST(2, 0, 18)
                    case SDL_WINDOWEVENT_DISPLAY_CHANGED:
#endif
                        event.type = PlatformEventType::WindowResized;
                        {
                            const WindowMetrics metrics = GetWindowMetrics();
                            event.width = metrics.logicalWidth;
                            event.height = metrics.logicalHeight;
                        }
                        m_width = event.width;
                        m_height = event.height;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        event.type = PlatformEventType::WindowFocusGained;
                        SDL_SetRelativeMouseMode(m_relativeMouseModeRequested ? SDL_TRUE : SDL_FALSE);
                        SDL_ShowCursor(m_cursorVisible ? SDL_ENABLE : SDL_DISABLE);
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        event.type = PlatformEventType::WindowFocusLost;
                        SDL_CaptureMouse(SDL_FALSE);
                        SDL_SetRelativeMouseMode(SDL_FALSE);
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
                event.modifiers = NormalizeModifiers(SDL_GetModState(), SDL_GetMouseState(nullptr, nullptr));
                event.repeat = sdlEvent.key.repeat != 0;
                break;
            case SDL_KEYUP:
                event.type = PlatformEventType::KeyUp;
                event.keyCode = static_cast<std::uint32_t>(sdlEvent.key.keysym.sym);
                event.scancode = static_cast<std::uint32_t>(sdlEvent.key.keysym.scancode);
                event.modifiers = NormalizeModifiers(SDL_GetModState(), SDL_GetMouseState(nullptr, nullptr));
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
                event.modifiers = NormalizeModifiers(SDL_GetModState(), sdlEvent.motion.state);
                break;
            case SDL_MOUSEBUTTONDOWN:
                event.type = PlatformEventType::MouseButtonDown;
                event.button = sdlEvent.button.button;
                event.clickCount = sdlEvent.button.clicks;
                event.x = sdlEvent.button.x;
                event.y = sdlEvent.button.y;
                event.modifiers = NormalizeModifiers(SDL_GetModState(), SDL_GetMouseState(nullptr, nullptr));
                static_cast<void>(SDL_CaptureMouse(SDL_TRUE));
                break;
            case SDL_MOUSEBUTTONUP:
                event.type = PlatformEventType::MouseButtonUp;
                event.button = sdlEvent.button.button;
                event.clickCount = sdlEvent.button.clicks;
                event.x = sdlEvent.button.x;
                event.y = sdlEvent.button.y;
                event.modifiers = NormalizeModifiers(SDL_GetModState(), SDL_GetMouseState(nullptr, nullptr));
                if ((SDL_GetMouseState(nullptr, nullptr) &
                     (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK | SDL_BUTTON_RMASK)) == 0U) {
                    static_cast<void>(SDL_CaptureMouse(SDL_FALSE));
                }
                break;
            case SDL_MOUSEWHEEL:
                event.type = PlatformEventType::MouseWheel;
                event.wheelX = sdlEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                                   ? -sdlEvent.wheel.x : sdlEvent.wheel.x;
                event.wheelY = sdlEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                                   ? -sdlEvent.wheel.y : sdlEvent.wheel.y;
                event.modifiers = NormalizeModifiers(SDL_GetModState(), SDL_GetMouseState(&event.x, &event.y));
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
                event.pressed = sdlEvent.type == SDL_CONTROLLERBUTTONDOWN;
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

bool SDLPlatform::ApplyWindowDisplayConfig(const WindowDisplayConfig& config) {
    if (config.width <= 0 || config.height <= 0) return false;
    m_requestedWidth = config.width;
    m_requestedHeight = config.height;
    m_resizable = config.resizable;
    if (m_window == nullptr) {
        m_width = config.width;
        m_height = config.height;
        m_fullscreen = config.mode != WindowPresentationMode::Windowed;
        m_borderless = config.mode == WindowPresentationMode::Borderless;
        return true;
    }

    const int displayIndex = std::max(SDL_GetWindowDisplayIndex(m_window), 0);
    if (SDL_SetWindowFullscreen(m_window, 0) != 0) return false;
    static_cast<void>(SDL_SetWindowDisplayMode(m_window, nullptr));

    SDL_SetWindowResizable(m_window, config.resizable ? SDL_TRUE : SDL_FALSE);
    SDL_SetWindowBordered(m_window, config.mode == WindowPresentationMode::Windowed ? SDL_TRUE : SDL_FALSE);

    const bool useAdvancedColorFullscreen = config.mode == WindowPresentationMode::Fullscreen &&
                                            IsWindowsAdvancedColorEnabled(m_window);
    bool applied = false;
    switch (config.mode) {
        case WindowPresentationMode::Windowed:
            SDL_SetWindowSize(m_window, config.width, config.height);
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex));
            applied = true;
            break;
        case WindowPresentationMode::Borderless:
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex));
            applied = SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
            break;
        case WindowPresentationMode::Fullscreen:
            if (useAdvancedColorFullscreen) {
                applied = SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
                if (applied) {
                    Logger{}.Info("Windows HDR is active; using desktop fullscreen to preserve Advanced Color composition.");
                }
            } else if (const auto displayMode = ExactDisplayMode(m_window, config.width, config.height);
                       displayMode.has_value() && SDL_SetWindowDisplayMode(m_window, &*displayMode) == 0) {
                applied = SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN) == 0;
            }
            break;
    }

    const Uint32 flags = SDL_GetWindowFlags(m_window);
    if (config.mode != WindowPresentationMode::Windowed && (flags & SDL_WINDOW_FULLSCREEN) == 0U) {
        applied = false;
    }
    if (!applied) {
        static_cast<void>(SDL_SetWindowFullscreen(m_window, 0));
        static_cast<void>(SDL_SetWindowDisplayMode(m_window, nullptr));
        SDL_SetWindowBordered(m_window, SDL_TRUE);
        SDL_SetWindowSize(m_window, config.width, config.height);
    }

    const WindowMetrics metrics = GetWindowMetrics();
    m_width = metrics.logicalWidth;
    m_height = metrics.logicalHeight;
    m_fullscreen = applied && config.mode != WindowPresentationMode::Windowed;
    m_borderless = applied && (config.mode == WindowPresentationMode::Borderless || useAdvancedColorFullscreen);
    return applied;
}

void SDLPlatform::SetWindowFullscreen(bool fullscreen) {
    static_cast<void>(ApplyWindowDisplayConfig({m_requestedWidth, m_requestedHeight,
                                                fullscreen ? WindowPresentationMode::Fullscreen
                                                           : WindowPresentationMode::Windowed,
                                                m_resizable}));
}

void SDLPlatform::SetWindowBorderless(bool borderless) {
    const WindowPresentationMode mode = borderless
                                            ? WindowPresentationMode::Borderless
                                            : (m_fullscreen && !m_borderless
                                                   ? WindowPresentationMode::Fullscreen
                                                   : WindowPresentationMode::Windowed);
    static_cast<void>(ApplyWindowDisplayConfig({m_requestedWidth, m_requestedHeight, mode, m_resizable}));
}

void SDLPlatform::SetWindowResizable(bool resizable) {
    m_resizable = resizable;
    if (m_window != nullptr) {
        SDL_SetWindowResizable(m_window, resizable ? SDL_TRUE : SDL_FALSE);
    }
}

void SDLPlatform::SetWindowResolution(int width, int height) {
    const WindowPresentationMode mode = m_borderless ? WindowPresentationMode::Borderless
                                        : m_fullscreen ? WindowPresentationMode::Fullscreen
                                                       : WindowPresentationMode::Windowed;
    static_cast<void>(ApplyWindowDisplayConfig({width, height, mode, m_resizable}));
}

void SDLPlatform::SetWindowTitle(const std::string& title) {
    m_title = title;
    if (m_window != nullptr) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

void SDLPlatform::SetRelativeMouseMode(bool enabled) {
    m_relativeMouseModeRequested = enabled;
    if (m_window != nullptr) {
        SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
    }
}

void SDLPlatform::SetCursorVisible(bool visible) {
    m_cursorVisible = visible;
    if (m_window != nullptr) {
        SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
    }
}

void SDLPlatform::SetTextInputEnabled(bool enabled) {
    if (enabled) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

void SDLPlatform::SetVSync(bool enabled) {
    m_vsync = enabled;
    if (m_window != nullptr && m_graphicsApi == WindowGraphicsApi::OpenGL) {
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
    }
}

std::pair<int, int> SDLPlatform::GetDrawableSize() const {
    const WindowMetrics metrics = GetWindowMetrics();
    return {metrics.drawableWidth, metrics.drawableHeight};
}

WindowMetrics SDLPlatform::GetWindowMetrics() const {
    WindowMetrics metrics{std::max(m_width, 1), std::max(m_height, 1),
                          std::max(m_width, 1), std::max(m_height, 1)};
    if (m_window == nullptr) return metrics;
    SDL_GetWindowSize(m_window, &metrics.logicalWidth, &metrics.logicalHeight);
    if (m_graphicsApi == WindowGraphicsApi::OpenGL) {
        SDL_GL_GetDrawableSize(m_window, &metrics.drawableWidth, &metrics.drawableHeight);
    } else {
        metrics.drawableWidth = metrics.logicalWidth;
        metrics.drawableHeight = metrics.logicalHeight;
    }
    metrics.logicalWidth = std::max(metrics.logicalWidth, 1);
    metrics.logicalHeight = std::max(metrics.logicalHeight, 1);
    metrics.drawableWidth = std::max(metrics.drawableWidth, 1);
    metrics.drawableHeight = std::max(metrics.drawableHeight, 1);
    return metrics;
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
