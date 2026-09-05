/**
 * @file ui_manager.cpp
 * @brief Implementation of `ComputeUIScale` and the test-only `NullUIManager`.
 */

#include "voxels/ui/ui_manager.hpp"

#include <algorithm>

namespace voxels {

float ComputeUIScale(const UIDisplayMetrics& metrics, int baseWidth, int baseHeight) noexcept {
    if (baseWidth <= 0 || baseHeight <= 0 || metrics.windowWidth <= 0 || metrics.windowHeight <= 0) {
        return metrics.dpiScale;
    }
    const float scaleX = static_cast<float>(metrics.windowWidth) / static_cast<float>(baseWidth);
    const float scaleY = static_cast<float>(metrics.windowHeight) / static_cast<float>(baseHeight);
    return std::min(scaleX, scaleY) * metrics.dpiScale;
}

bool NullUIManager::Initialize(IPlatform* platform, graphics::IGraphicsRenderer* renderer) {
    m_platform = platform;
    m_renderer = renderer;
    if (m_platform) {
        const PlatformContext context = m_platform->GetContext();
        (void)context;
    }
    m_scale = ComputeUIScale(m_metrics);
    m_initialized = true;
    return true;
}

void NullUIManager::Shutdown() {
    m_platform = nullptr;
    m_renderer = nullptr;
    m_initialized = false;
    m_frameActive = false;
}

void NullUIManager::BeginFrame() {
    if (!m_initialized) {
        return;
    }
    m_frameActive = true;
}

void NullUIManager::EndFrame() {
    m_frameActive = false;
}

void NullUIManager::OnPlatformEvent(const PlatformEvent& event) {
    if (event.type == PlatformEventType::WindowResized) {
        m_metrics.windowWidth = event.width;
        m_metrics.windowHeight = event.height;
        m_scale = ComputeUIScale(m_metrics);
    }
}

} // namespace voxels
