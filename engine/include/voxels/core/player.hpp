#pragma once

/**
 * @file player.hpp
 * @brief Legacy plain-data player record (name, vitals, position).
 *
 * @details Early data model predating the gameplay layer. The runtime player type is
 *          `voxels::Player` in voxels/gameplay/player.hpp (which wraps `PlayerState` with
 *          physics/inventory fields); nothing currently includes this header, and the two
 *          types share a name, so it must not be included alongside the gameplay one.
 */

#include <cstdint>
#include <string>

namespace voxels {

/// Minimal persistent player record; position is world-space, in blocks.
struct Player {
    std::string name;
    std::uint32_t health = 100;
    std::uint32_t hunger = 100;
    std::uint64_t experience = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

} // namespace voxels
