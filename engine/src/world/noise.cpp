#include "voxels/world/noise.hpp"

#include <algorithm>
#include <cmath>

namespace voxels {

namespace {
constexpr double kOneOver256 = 1.0 / 256.0;

std::uint64_t Mix(std::uint64_t value) noexcept {
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33u;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33u;
    return value;
}

std::uint64_t HashSeeded(std::uint64_t hash, std::uint64_t seed) noexcept {
    return Mix(hash ^ (seed + 0x9e3779b97f4a7c15ULL + (hash << 6u) + (hash >> 2u)));
}
} // namespace

Noise::Noise(std::uint64_t seed) : m_seed(seed) {
}

double Noise::Fade(double value) noexcept {
    return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

double Noise::Lerp(double a, double b, double t) noexcept {
    return a + (b - a) * t;
}

double Noise::Clamp(double value, double minValue, double maxValue) noexcept {
    return std::max(minValue, std::min(maxValue, value));
}

int Noise::Hash2D(int x, int y, std::uint64_t seed) noexcept {
    const std::uint64_t mixed = HashSeeded(static_cast<std::uint64_t>(x * 374761393u + y * 668265263u), seed);
    return static_cast<int>(mixed & 255u) & 255;
}

int Noise::Hash3D(int x, int y, int z, std::uint64_t seed) noexcept {
    const std::uint64_t mixed = HashSeeded(
        static_cast<std::uint64_t>(x * 374761393u + y * 668265263u + z * 1103515245u), seed);
    return static_cast<int>(mixed & 255u) & 255;
}

double Noise::Grad(int hash, double x, double y, double z) noexcept {
    const int h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double Noise::Perlin2D(double x, double y) const noexcept {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const double xf = x - static_cast<double>(x0);
    const double yf = y - static_cast<double>(y0);
    const double u = Fade(xf);
    const double v = Fade(yf);

    const double aa = static_cast<double>(Hash2D(x0, y0, m_seed)) * kOneOver256;
    const double ba = static_cast<double>(Hash2D(x1, y0, m_seed)) * kOneOver256;
    const double ab = static_cast<double>(Hash2D(x0, y1, m_seed)) * kOneOver256;
    const double bb = static_cast<double>(Hash2D(x1, y1, m_seed)) * kOneOver256;

    const double x1Weight = Lerp(aa, ba, u);
    const double x2Weight = Lerp(ab, bb, u);
    return Lerp(x1Weight, x2Weight, v) * 2.0 - 1.0;
}

double Noise::Perlin3D(double x, double y, double z) const noexcept {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;

    const double xf = x - static_cast<double>(x0);
    const double yf = y - static_cast<double>(y0);
    const double zf = z - static_cast<double>(z0);
    const double u = Fade(xf);
    const double v = Fade(yf);
    const double w = Fade(zf);

    const double x000 = Grad(Hash3D(x0, y0, z0, m_seed), xf, yf, zf);
    const double x100 = Grad(Hash3D(x1, y0, z0, m_seed), xf - 1.0, yf, zf);
    const double x010 = Grad(Hash3D(x0, y1, z0, m_seed), xf, yf - 1.0, zf);
    const double x110 = Grad(Hash3D(x1, y1, z0, m_seed), xf - 1.0, yf - 1.0, zf);
    const double x001 = Grad(Hash3D(x0, y0, z1, m_seed), xf, yf, zf - 1.0);
    const double x101 = Grad(Hash3D(x1, y0, z1, m_seed), xf - 1.0, yf, zf - 1.0);
    const double x011 = Grad(Hash3D(x0, y1, z1, m_seed), xf, yf - 1.0, zf - 1.0);
    const double x111 = Grad(Hash3D(x1, y1, z1, m_seed), xf - 1.0, yf - 1.0, zf - 1.0);

    const double x00 = Lerp(x000, x100, u);
    const double x10 = Lerp(x010, x110, u);
    const double x01 = Lerp(x001, x101, u);
    const double x11 = Lerp(x011, x111, u);
    const double y00 = Lerp(x00, x10, v);
    const double y01 = Lerp(x01, x11, v);
    return Lerp(y00, y01, w);
}

double Noise::Evaluate2D(double x, double y) const noexcept {
    return Perlin2D(x, y);
}

double Noise::Evaluate3D(double x, double y, double z) const noexcept {
    return Perlin3D(x, y, z);
}

double Noise::Fractal2D(double x, double y, int octaves, double persistence, double lacunarity) const noexcept {
    double amplitude = 1.0;
    double frequency = 1.0;
    double total = 0.0;
    double normalizer = 0.0;

    for (int octave = 0; octave < octaves; ++octave) {
        total += amplitude * (Evaluate2D(x * frequency, y * frequency) * 0.5 + 0.5);
        normalizer += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    if (normalizer == 0.0) {
        return 0.0;
    }
    return total / normalizer;
}

double Noise::Ridged2D(double x, double y, int octaves, double persistence, double lacunarity) const noexcept {
    double amplitude = 1.0;
    double frequency = 1.0;
    double total = 0.0;
    double normalizer = 0.0;

    for (int octave = 0; octave < octaves; ++octave) {
        const double sample = std::abs(Evaluate2D(x * frequency, y * frequency));
        total += amplitude * (1.0 - sample);
        normalizer += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    if (normalizer == 0.0) {
        return 0.0;
    }
    return total / normalizer;
}

} // namespace voxels
