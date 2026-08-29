/**
 * @file dreamcast_platform.cpp
 * @brief Implements the fixed-mode `DreamcastPlatform` stub.
 */

#ifdef VOXELS_ENABLE_DREAMCAST

#include "voxels/platform/dreamcast_platform.hpp"

#include <chrono>

namespace voxels {

PlatformContext DreamcastPlatform::GetContext() const {
    PlatformContext context;
    context.type = PlatformType::Dreamcast;
    context.name = "Dreamcast";
    return context;
}

bool DreamcastPlatform::Initialize(const WindowConfig& /*config*/) {
    // Hardware display is fixed; requested width/height/fullscreen are intentionally ignored.
    m_initialized = true;
    return true;
}

void DreamcastPlatform::Shutdown() {
    m_initialized = false;
}

void DreamcastPlatform::PollEvents(IPlatformEventListener* /*listener*/) {
    // TODO: bridge KOS input/maple event polling once the KallistiOS toolchain is integrated.
}

void DreamcastPlatform::SwapBuffers() {
    // TODO: present the PowerVR2 framebuffer via the Dreamcast renderer backend.
}

void DreamcastPlatform::SetWindowFullscreen(bool /*fullscreen*/) {
    // No-op: the Dreamcast has no windowed display mode.
}

void DreamcastPlatform::SetWindowResolution(int /*width*/, int /*height*/) {
    // No-op: resolution is fixed at 640x480.
}

double DreamcastPlatform::GetHighResTimeSeconds() const {
    // TODO: replace with KOS `timer_ms_gettime64` once building against the KOS toolchain.
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace voxels

#endif // VOXELS_ENABLE_DREAMCAST
