/**
 * @file display_settings.cpp
 * @brief Implements atomic player display preference application.
 *
 * @details Converts application-facing modes into the platform-neutral display contract while
 *          preserving the supported resolution bounds. See ARCHITECTURE.md section 6.8.
 */

#include "voxels/app/display_settings.hpp"

#include <algorithm>

namespace voxels {

WindowDisplayConfig BuildWindowDisplayConfig(const WindowMode mode,
                                             const Resolution& resolution) noexcept {
    WindowPresentationMode presentationMode = WindowPresentationMode::Windowed;
    switch (mode) {
        case WindowMode::Windowed: presentationMode = WindowPresentationMode::Windowed; break;
        case WindowMode::Borderless: presentationMode = WindowPresentationMode::Borderless; break;
        case WindowMode::Fullscreen: presentationMode = WindowPresentationMode::Fullscreen; break;
    }
    return {.width = std::clamp(resolution.width, 640, 7680),
            .height = std::clamp(resolution.height, 360, 4320),
            .mode = presentationMode,
            .resizable = false};
}

bool ApplyWindowPreferences(IPlatform& platform, const WindowMode mode,
                            const Resolution& resolution) {
    return platform.ApplyWindowDisplayConfig(BuildWindowDisplayConfig(mode, resolution));
}

bool ApplyWindowPreferences(IPlatform& platform, const GamePreferences& preferences) {
    return ApplyWindowPreferences(platform, preferences.windowMode, preferences.resolution);
}

} // namespace voxels