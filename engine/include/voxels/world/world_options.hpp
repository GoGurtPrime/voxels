/**
 * @file world_options.hpp
 * @brief World generation configuration and world-mode settings.
 *
 * @details Defines the deterministic world seed and gameplay options that shape terrain generation,
 *          world visibility, simulation distance, and the player experience profile. These values
 *          flow into the procedural generation pipeline and the spawn determinism logic before the
 *          gameplay session is created.
 */

#pragma once

#include <cstdint>

namespace voxels {

inline constexpr float kWorldDayDurationSeconds = 20.0f * 60.0f;

struct WorldOptions {
    std::uint64_t seed = 0;
    std::uint32_t generatorVersion = 2;
    bool peaceful = false;
    bool permadeath = false;
    bool alwaysSunny = true;
    bool sandboxMode = false;
    bool visibility = true;
    bool isPublic = true;
    int renderDistanceChunks = 8;
    int simulationDistanceChunks = 4;
};

} // namespace voxels
