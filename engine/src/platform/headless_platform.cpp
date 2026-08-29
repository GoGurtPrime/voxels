/**
 * @file headless_platform.cpp
 * @brief Implements the in-memory `HeadlessPlatform` fallback.
 */

#include "voxels/platform/headless_platform.hpp"

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
    m_eventQueue.clear();
    m_initialized = true;
    return true;
}

void HeadlessPlatform::Shutdown() {
    m_eventQueue.clear();
    m_initialized = false;
}

void HeadlessPlatform::PollEvents(IPlatformEventListener* listener) {
    while (!m_eventQueue.empty()) {
        const PlatformEvent event = m_eventQueue.front();
        m_eventQueue.pop_front();
        if (listener != nullptr) {
            listener->OnPlatformEvent(event);
        }
    }
}

void HeadlessPlatform::SwapBuffers() {
    // No-op: there is no real framebuffer to present in headless mode.
}

void HeadlessPlatform::SetWindowFullscreen(bool fullscreen) {
    m_fullscreen = fullscreen;
    SimulateEvent(PlatformEvent{PlatformEventType::WindowResized, m_width, m_height});
}

void HeadlessPlatform::SetWindowResolution(int width, int height) {
    m_width = width;
    m_height = height;
    SimulateEvent(PlatformEvent{PlatformEventType::WindowResized, width, height});
}

double HeadlessPlatform::GetHighResTimeSeconds() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void HeadlessPlatform::SimulateEvent(const PlatformEvent& event) {
    m_eventQueue.push_back(event);
}

} // namespace voxels
