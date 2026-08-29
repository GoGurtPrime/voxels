/**
 * @file platform_factory.cpp
 * @brief Selects the active `IPlatform` implementation for the current build target.
 */

#include "voxels/platform/platform.hpp"

#include "voxels/platform/headless_platform.hpp"

#ifdef VOXELS_ENABLE_DREAMCAST
#include "voxels/platform/dreamcast_platform.hpp"
#endif

#ifdef VOXELS_HAS_SDL2
#include "voxels/platform/sdl_platform.hpp"
#endif

namespace voxels {

std::unique_ptr<IPlatform> CreateDefaultPlatform() {
#if defined(VOXELS_ENABLE_DREAMCAST)
    return std::make_unique<DreamcastPlatform>();
#elif defined(VOXELS_HAS_SDL2)
    return std::make_unique<SDLPlatform>();
#else
    return std::make_unique<HeadlessPlatform>();
#endif
}

} // namespace voxels
