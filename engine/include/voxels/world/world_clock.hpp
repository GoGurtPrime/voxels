/**
 * @file world_clock.hpp
 * @brief Deterministic conversions for the server-authoritative world clock.
 *
 * @details World time is represented as a monotonic simulation tick, avoiding floating-point
 *          drift across persistence, networking, and variable-rate rendering (ARCHITECTURE.md 6.4).
 */

#pragma once

#include <cstdint>

namespace voxels {

using WorldTick = std::uint64_t;

inline constexpr std::uint64_t kWorldTicksPerSecond = 60;
inline constexpr std::uint64_t kWorldSecondsPerDay = 20 * 60;
inline constexpr std::uint64_t kWorldTicksPerDay = kWorldTicksPerSecond * kWorldSecondsPerDay;
inline constexpr WorldTick kInitialWorldTick = kWorldTicksPerDay / 4;

[[nodiscard]] constexpr std::uint64_t WorldDayIndex(WorldTick tick) noexcept {
    return tick / kWorldTicksPerDay;
}

[[nodiscard]] constexpr float WorldDayFraction(WorldTick tick) noexcept {
    return static_cast<float>(tick % kWorldTicksPerDay) / static_cast<float>(kWorldTicksPerDay);
}

[[nodiscard]] constexpr float WorldDayTimeSeconds(WorldTick tick) noexcept {
    return WorldDayFraction(tick) * static_cast<float>(kWorldSecondsPerDay);
}

} // namespace voxels