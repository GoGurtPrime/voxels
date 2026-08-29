#pragma once

/*
 * Scope: Player runtime model and persistent save state.
 *
 * This contract defines the in-memory player representation used by the gameplay systems,
 * spawn logic, and save serialization pipeline. Actual gameplay attributes and progression
 * systems will be expanded as the game design matures.
 *
 * Relation to the rest of the codebase: world placement, multiplayer synchronization, and
 * save serialization all consume this model.
 */

#include <cstdint>
#include <string>

namespace voxels {

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
