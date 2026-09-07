/**
 * @file headless_platform.cpp
 * @brief Implements the in-memory `HeadlessPlatform` fallback.
 */

#include "voxels/platform/headless_platform.hpp"

#include <algorithm>
#include <chrono>

namespace voxels {

PlatformContext HeadlessPlatform::GetContext() const {
    PlatformContext context;
    context.type = PlatformType::Unknown;
    context.name = "Headless";
    return context;
}

bool HeadlessPlatform::Initialize(const WindowConfig& config) {
    m_width = config.width;
    m_height = config.height;
    m_fullscreen = config.fullscreen;
    m_borderless = false;
    m_resizable = config.resizable;
    m_cursorVisible = true;
    m_relativeMouseMode = false;
    m_eventQueue.clear();
    m_initialized = true;
    return true;
}

void HeadlessPlatform::Shutdown() {
    m_eventQueue.clear();
    m_listeners.clear();
    m_defaultListener = nullptr;
    m_initialized = false;
}

void HeadlessPlatform::PollEvents(IPlatformEventListener* listener) {
    if (listener != nullptr) {
        m_defaultListener = listener;
        m_defaultListenerPriority = 0;
    }

    while (!m_eventQueue.empty()) {
        const PlatformEvent event = m_eventQueue.front();
        m_eventQueue.pop_front();

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

void HeadlessPlatform::SwapBuffers() {
    // No-op: there is no real framebuffer to present in headless mode.
}

bool HeadlessPlatform::ApplyWindowDisplayConfig(const WindowDisplayConfig& config) {
    if (config.width <= 0 || config.height <= 0) return false;
    m_width = config.width;
    m_height = config.height;
    m_fullscreen = config.mode == WindowPresentationMode::Fullscreen;
    m_borderless = config.mode == WindowPresentationMode::Borderless;
    m_resizable = config.resizable;
    SimulateEvent(PlatformEvent{PlatformEventType::WindowResized, m_width, m_height});
    return true;
}

void HeadlessPlatform::SetWindowFullscreen(bool fullscreen) {
    m_fullscreen = fullscreen;
    SimulateEvent(PlatformEvent{PlatformEventType::WindowResized, m_width, m_height});
}

void HeadlessPlatform::SetWindowBorderless(bool borderless) {
    m_borderless = borderless;
}

void HeadlessPlatform::SetWindowResizable(bool resizable) {
    m_resizable = resizable;
}

void HeadlessPlatform::SetWindowResolution(int width, int height) {
    m_width = width;
    m_height = height;
    SimulateEvent(PlatformEvent{PlatformEventType::WindowResized, width, height});
}

void HeadlessPlatform::SetWindowTitle(const std::string&) {}

void HeadlessPlatform::SetRelativeMouseMode(bool enabled) {
    m_relativeMouseMode = enabled;
    if (!enabled) {
        m_cursorVisible = true;
    }
}

void HeadlessPlatform::SetCursorVisible(bool visible) {
    m_cursorVisible = visible;
}

void HeadlessPlatform::SetVSync(bool enabled) {
    (void)enabled;
}

std::pair<int, int> HeadlessPlatform::GetDrawableSize() const {
    return {m_width, m_height};
}

WindowMetrics HeadlessPlatform::GetWindowMetrics() const {
    return {m_width, m_height, m_width, m_height};
}

double HeadlessPlatform::GetHighResTimeSeconds() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void HeadlessPlatform::RegisterEventListener(IPlatformEventListener* listener, int priority) {
    if (listener == nullptr) {
        return;
    }
    auto it = std::find_if(m_listeners.begin(), m_listeners.end(), [listener](const ListenerEntry& entry) {
        return entry.listener == listener;
    });
    if (it != m_listeners.end()) {
        it->priority = priority;
        return;
    }
    m_listeners.push_back({listener, priority});
}

void HeadlessPlatform::UnregisterEventListener(IPlatformEventListener* listener) {
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(), [listener](const ListenerEntry& entry) {
        return entry.listener == listener;
    }), m_listeners.end());
    if (m_defaultListener == listener) {
        m_defaultListener = nullptr;
    }
}

void HeadlessPlatform::SimulateEvent(const PlatformEvent& event) {
    m_eventQueue.push_back(event);
}

} // namespace voxels
