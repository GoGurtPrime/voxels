/**
 * @file noise.hpp
 * @brief Procedural noise primitives used by the world generation pipeline.
 *
 * @details Wraps a deterministic, seed-based noise generator for 2D/3D terrain shaping, cave carving,
 *          and vegetation placement. The implementation focuses on deterministic results so the same
 *          world configuration produces identical chunk data across repeated runs.
 */

#pragma once

#include <cstdint>

namespace voxels {

class Noise {
public:
    explicit Noise(std::uint64_t seed = 0);

    [[nodiscard]] double Evaluate2D(double x, double y) const noexcept;
    [[nodiscard]] double Evaluate3D(double x, double y, double z) const noexcept;
    [[nodiscard]] double Fractal2D(double x, double y,
                                  int octaves = 4,
                                  double persistence = 0.5,
                                  double lacunarity = 2.0) const noexcept;
    [[nodiscard]] double Ridged2D(double x, double y,
                                 int octaves = 4,
                                 double persistence = 0.5,
                                 double lacunarity = 2.0) const noexcept;
    [[nodiscard]] double Ridged3D(double x, double y, double z,
                                  int octaves = 4,
                                  double persistence = 0.5,
                                  double lacunarity = 2.0) const noexcept;
    [[nodiscard]] double DomainWarped2D(double x, double y, double warpFrequency, double warpStrength) const noexcept;
    [[nodiscard]] double Cellular2D(double x, double y) const noexcept;

private:
    std::uint64_t m_seed;

    [[nodiscard]] double Perlin2D(double x, double y) const noexcept;
    [[nodiscard]] double Perlin3D(double x, double y, double z) const noexcept;
    [[nodiscard]] static double Fade(double value) noexcept;
    [[nodiscard]] static double Lerp(double a, double b, double t) noexcept;
    [[nodiscard]] static double Clamp(double value, double minValue, double maxValue) noexcept;
    [[nodiscard]] static int Hash2D(int x, int y, std::uint64_t seed) noexcept;
    [[nodiscard]] static int Hash3D(int x, int y, int z, std::uint64_t seed) noexcept;
    [[nodiscard]] static double Grad(int hash, double x, double y, double z) noexcept;
};

} // namespace voxels
