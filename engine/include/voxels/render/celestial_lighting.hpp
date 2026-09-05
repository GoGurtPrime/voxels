#pragma once

#include "voxels/core/math.hpp"
#include "voxels/world/world_options.hpp"

namespace voxels::graphics {

inline constexpr float kDayDurationSeconds = kWorldDayDurationSeconds;

/// Backend-neutral directional and ambient lighting sampled from authoritative world time.
struct CelestialLighting {
    Vec3 sunDirection{-0.472866f, 0.788110f, -0.394055f};
    Vec3 sunColor{0.65f, 0.62f, 0.55f};
    Vec3 ambientColor{0.42f, 0.48f, 0.56f};
    Vec3 skyColor{0.55f, 0.70f, 0.92f};
    float dayFraction = 0.25f;
};

[[nodiscard]] float NormalizeDayTime(float dayTimeSeconds) noexcept;
[[nodiscard]] CelestialLighting EvaluateCelestialLighting(float dayTimeSeconds, bool alwaysSunny) noexcept;

} // namespace voxels::graphics