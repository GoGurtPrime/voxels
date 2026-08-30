/**
 * @file generation_pipeline.cpp
 * @brief Deterministic, globally sampled terrain generation phases.
 *
 * @details Each phase derives output solely from the world seed and world-space coordinates,
 *          so output is independent of generation order and worker count. See ARCHITECTURE.md 6.3.
 */

#include "voxels/world/generation_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "voxels/world/block.hpp"

namespace voxels {
namespace {
constexpr int kChunkSize = 16;
constexpr int kSeaLevel = 48;
constexpr BlockId kAir = static_cast<BlockId>(BlockType::Air);
constexpr BlockId kStone = static_cast<BlockId>(BlockType::Stone);
constexpr BlockId kDirt = static_cast<BlockId>(BlockType::Dirt);
constexpr BlockId kGrass = static_cast<BlockId>(BlockType::Grass);
constexpr BlockId kSand = static_cast<BlockId>(BlockType::Sand);
constexpr BlockId kWater = static_cast<BlockId>(BlockType::Water);
constexpr BlockId kCoal = static_cast<BlockId>(BlockType::Coal);
constexpr BlockId kIron = static_cast<BlockId>(BlockType::Iron);
constexpr BlockId kWood = static_cast<BlockId>(BlockType::Wood);
constexpr BlockId kLeaf = static_cast<BlockId>(BlockType::Leaf);
constexpr BlockId kBedrock = static_cast<BlockId>(BlockType::Bedrock);

[[nodiscard]] std::uint64_t Mix(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t PhaseSeed(std::uint64_t seed, std::uint64_t phase) noexcept {
    return Mix(seed ^ (phase * 0x9e3779b97f4a7c15ULL));
}

[[nodiscard]] double SmoothStep(double edge0, double edge1, double value) noexcept {
    const double normalized = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return normalized * normalized * (3.0 - 2.0 * normalized);
}

[[nodiscard]] bool IsSolidTerrain(BlockId block) noexcept {
    return block != kAir && block != kWater && block != kLeaf;
}

[[nodiscard]] TerrainColumn SampleColumnForSeed(std::uint64_t seed, int worldX, int worldZ) noexcept {
    const double x = static_cast<double>(worldX);
    const double z = static_cast<double>(worldZ);
    const Noise climate(PhaseSeed(seed, 1));
    const Noise terrain(PhaseSeed(seed, 2));
    const Noise peaks(PhaseSeed(seed, 3));
    const Noise cells(PhaseSeed(seed, 4));
    const double continentalness = terrain.DomainWarped2D(x * 0.0035, z * 0.0035, 0.019, 8.0);
    const double erosion = terrain.Fractal2D(x * 0.012, z * 0.012, 3, 0.55, 2.0);
    const double temperature = climate.Fractal2D(x * 0.005, z * 0.005, 3, 0.58, 2.0);
    const double humidity = climate.Fractal2D(x * 0.006 + 83.0, z * 0.006 - 29.0, 3, 0.58, 2.0);
    const double cellEdge = cells.Cellular2D(x * 0.012, z * 0.012);
    const double mountainWeight = SmoothStep(0.44, 0.73, peaks.Ridged2D(x * 0.009, z * 0.009, 4, 0.53, 2.0)) *
                                 SmoothStep(0.36, 0.70, continentalness * 0.5 + 0.5);
    Biome biome = Biome::Plains;
    if (continentalness < -0.22) biome = Biome::Ocean;
    else if (continentalness < -0.10) biome = Biome::Beach;
    else if (temperature > 0.61 && humidity < 0.47) biome = Biome::Desert;
    else if (mountainWeight > 0.58) biome = Biome::Mountains;
    else if (humidity > 0.58) biome = Biome::Forest;
    else if (erosion < 0.39) biome = Biome::Hills;

    const double rolling = terrain.Fractal2D(x * 0.021, z * 0.021, 4, 0.52, 2.0) * 13.0 - 6.5;
    const double mountains = mountainWeight * (30.0 + peaks.Ridged2D(x * 0.016, z * 0.016, 3, 0.55, 2.0) * 36.0);
    double height = static_cast<double>(kSeaLevel) + rolling + mountains - (1.0 - erosion) * 5.0;
    if (biome == Biome::Ocean) height = kSeaLevel - 8.0 + rolling * 0.22;
    if (biome == Biome::Beach) height = kSeaLevel + rolling * 0.16;
    if (biome == Biome::Desert) height += 2.0;
    return {std::clamp(static_cast<int>(std::lround(height)), 5, 118), biome,
            static_cast<float>(SmoothStep(0.12, 0.34, cellEdge))};
}

[[nodiscard]] bool IsCave(std::uint64_t seed, int x, int y, int z, int surfaceY) noexcept {
    if (y < 4 || y >= surfaceY - 3 || (surfaceY <= kSeaLevel && y < kSeaLevel)) return false;
    const Noise worm(PhaseSeed(seed, 5));
    const Noise cavern(PhaseSeed(seed, 6));
    const double tunnel = worm.Ridged3D(x * 0.035, y * 0.052, z * 0.035, 3, 0.55, 2.0);
    const double cavernValue = cavern.Ridged3D(x * 0.016, y * 0.021, z * 0.016, 3, 0.55, 2.0);
    return tunnel > 0.79 || (y < 38 && cavernValue > 0.84);
}

[[nodiscard]] BlockId OreFor(std::uint64_t seed, int x, int y, int z) noexcept {
    const Noise coal(PhaseSeed(seed, 7));
    const Noise iron(PhaseSeed(seed, 8));
    if (y >= 22 && y <= 72 && coal.Ridged3D(x * 0.13, y * 0.13, z * 0.13, 2, 0.58, 2.0) > 0.78) return kCoal;
    if (y >= 7 && y <= 48 && iron.Ridged3D(x * 0.16, y * 0.16, z * 0.16, 2, 0.58, 2.0) > 0.83) return kIron;
    return kStone;
}

void PlaceTree(Chunk& chunk, int worldX, int worldZ, const TerrainColumn& column, std::uint64_t seed) {
    if (column.biome != Biome::Forest && column.biome != Biome::Plains && column.biome != Biome::Hills) return;
    const Noise placement(PhaseSeed(seed, 9));
    const double density = column.biome == Biome::Forest ? 0.71 : column.biome == Biome::Hills ? 0.82 : 0.89;
    if (placement.Cellular2D(worldX * 0.12, worldZ * 0.12) < density) return;
    const int height = 4 + static_cast<int>(Mix(static_cast<std::uint64_t>(worldX) ^ (static_cast<std::uint64_t>(worldZ) << 32U) ^ seed) % 3U);
    const ChunkCoordinate coordinate = chunk.GetCoordinate();
    for (int dz = -2; dz <= 2; ++dz) for (int dx = -2; dx <= 2; ++dx) for (int dy = -1; dy <= 2; ++dy) {
        if (dx * dx + dz * dz + dy * dy > 6) continue;
        const int localX = worldX + dx - coordinate.x * kChunkSize;
        const int localY = column.surfaceY + height + dy - coordinate.y * kChunkSize;
        const int localZ = worldZ + dz - coordinate.z * kChunkSize;
        if (chunk.InBounds(localX, localY, localZ) && chunk.GetBlock(localX, localY, localZ) == kAir) chunk.SetBlock(localX, localY, localZ, kLeaf);
    }
    const int localX = worldX - coordinate.x * kChunkSize;
    const int localZ = worldZ - coordinate.z * kChunkSize;
    for (int y = 1; y <= height; ++y) {
        const int localY = column.surfaceY + y - coordinate.y * kChunkSize;
        if (chunk.InBounds(localX, localY, localZ) && chunk.GetBlock(localX, localY, localZ) == kAir) chunk.SetBlock(localX, localY, localZ, kWood);
    }
}
} // namespace

WorldGenerator::WorldGenerator() : WorldGenerator(WorldOptions{}) {}
WorldGenerator::WorldGenerator(const WorldOptions& options) : m_options(options), m_noise(PhaseSeed(options.seed, 0)) {}

void WorldGenerator::AddPhase(std::unique_ptr<IGenerationPhase> phase) { if (phase) m_phases.push_back(std::move(phase)); }
TerrainColumn WorldGenerator::SampleColumn(int worldX, int worldZ) const noexcept { return SampleColumnForSeed(m_options.seed, worldX, worldZ); }

Chunk WorldGenerator::GenerateChunk(const ChunkCoordinate& coordinate) const {
    Chunk chunk(coordinate, kChunkSize, kChunkSize, kChunkSize);
    if (!m_phases.empty()) {
        for (const auto& phase : m_phases) if (phase) phase->Execute(chunk);
        return chunk;
    }
    const int originX = coordinate.x * kChunkSize;
    const int originY = coordinate.y * kChunkSize;
    const int originZ = coordinate.z * kChunkSize;
    for (int z = 0; z < kChunkSize; ++z) for (int x = 0; x < kChunkSize; ++x) {
        const int worldX = originX + x;
        const int worldZ = originZ + z;
        const TerrainColumn column = SampleColumn(worldX, worldZ);
        for (int y = 0; y < kChunkSize; ++y) {
            const int worldY = originY + y;
            BlockId block = kAir;
            if (worldY == 0) block = kBedrock;
            else if (worldY <= column.surfaceY) {
                if (worldY == column.surfaceY) block = (column.biome == Biome::Desert || column.biome == Biome::Beach || column.biome == Biome::Ocean) ? kSand : kGrass;
                else if (worldY >= column.surfaceY - 3) block = (column.biome == Biome::Desert || column.biome == Biome::Beach || column.biome == Biome::Ocean) ? kSand : kDirt;
                else block = OreFor(m_options.seed, worldX, worldY, worldZ);
                if (!m_options.peaceful && IsCave(m_options.seed, worldX, worldY, worldZ, column.surfaceY)) block = kAir;
            } else if (worldY <= kSeaLevel) block = kWater;
            chunk.SetBlock(x, y, z, block);
        }
    }
    if (!m_options.sandboxMode) {
        for (int anchorZ = originZ - 2; anchorZ < originZ + kChunkSize + 2; ++anchorZ) {
            for (int anchorX = originX - 2; anchorX < originX + kChunkSize + 2; ++anchorX) {
                PlaceTree(chunk, anchorX, anchorZ, SampleColumn(anchorX, anchorZ), m_options.seed);
            }
        }
    }
    for (int z = 0; z < kChunkSize; ++z) for (int x = 0; x < kChunkSize; ++x) {
        bool skyVisible = true;
        for (int y = kChunkSize - 1; y >= 0; --y) {
            const BlockId block = chunk.GetBlock(x, y, z);
            chunk.SetSkyLight(x, y, z, skyVisible ? 15 : 0);
            if (IsSolidTerrain(block)) skyVisible = false;
        }
    }
    return chunk;
}
} // namespace voxels