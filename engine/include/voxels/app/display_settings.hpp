/**
 * @file display_settings.hpp
 * @brief Maps player display preferences to one atomic platform window transition.
 *
 * @details Keeps startup, native settings, and browser-driven settings on the same validated
 *          path so no caller can expose a partially-applied fullscreen or borderless state.
 *          See ARCHITECTURE.md section 6.8 and DIAGRAMS.md section 9.5.
 */

#pragma once

#include "voxels/core/game_types.hpp"
#include "voxels/platform/platform.hpp"

namespace voxels {

/// Builds a platform display request from clamped player preferences.
[[nodiscard]] WindowDisplayConfig BuildWindowDisplayConfig(WindowMode mode,
                                                           const Resolution& resolution) noexcept;

/// Applies player display preferences atomically, returning false when no safe native mode can
/// be established.
[[nodiscard]] bool ApplyWindowPreferences(IPlatform& platform, WindowMode mode,
                                          const Resolution& resolution);

/// Applies the display subset of a complete preference snapshot.
[[nodiscard]] bool ApplyWindowPreferences(IPlatform& platform,
                                          const GamePreferences& preferences);

} // namespace voxels