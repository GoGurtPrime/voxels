#include "voxels/render/celestial_lighting.hpp"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace voxels::graphics {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float SmoothStep(float edge0, float edge1, float value) noexcept {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

float NormalizeDayTime(float dayTimeSeconds) noexcept {
    if (!std::isfinite(dayTimeSeconds)) return 0.0f;
    float normalized = std::fmod(dayTimeSeconds, kDayDurationSeconds);
    if (normalized < 0.0f) normalized += kDayDurationSeconds;
    return normalized;
}

CelestialLighting EvaluateCelestialLighting(float dayTimeSeconds, bool alwaysSunny) noexcept {
    const float dayFraction = alwaysSunny ? 0.25f : NormalizeDayTime(dayTimeSeconds) / kDayDurationSeconds;
    const float angle = dayFraction * 2.0f * kPi;
    const float altitude = std::sin(angle);
    const float daylight = SmoothStep(-0.12f, 0.18f, altitude);
    const float twilight = (1.0f - SmoothStep(0.05f, 0.45f, std::abs(altitude))) *
                            SmoothStep(-0.35f, 0.05f, altitude);

    CelestialLighting lighting;
    lighting.dayFraction = dayFraction;
    lighting.sunDirection = glm::normalize(Vec3{std::cos(angle) * 0.72f, altitude, -0.48f});
    lighting.sunColor = glm::mix(Vec3{0.18f, 0.22f, 0.32f}, Vec3{0.68f, 0.66f, 0.60f}, daylight);
    lighting.sunColor = glm::mix(lighting.sunColor, Vec3{0.90f, 0.42f, 0.18f}, twilight * 0.65f);
    lighting.ambientColor = glm::mix(Vec3{0.035f, 0.045f, 0.075f}, Vec3{0.38f, 0.46f, 0.56f}, daylight);
    const Vec3 baseSky = glm::mix(Vec3{0.012f, 0.020f, 0.055f}, Vec3{0.55f, 0.70f, 0.92f}, daylight);
    lighting.skyColor = glm::mix(baseSky, Vec3{0.76f, 0.30f, 0.14f}, twilight * 0.55f);
    return lighting;
}

} // namespace voxels::graphics