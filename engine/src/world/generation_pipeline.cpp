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
constexpr int kSeaLevel = 24;
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
constexpr int kTreeCellSize = 7;
constexpr int kMaxTreeSurfaceY = 56;

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

[[nodiscard]] int FloorDiv(int value, int divisor) noexcept {
    int quotient = value / divisor;
    if (value % divisor < 0) --quotient;
    return quotient;
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
    const double mountainMask = SmoothStep(0.58, 0.78, peaks.Fractal2D(x * 0.0038, z * 0.0038, 3, 0.58, 2.0));
    const double mountainWeight = mountainMask * SmoothStep(0.46, 0.72, continentalness * 0.5 + 0.5) *
                                  SmoothStep(0.42, 0.70, erosion);
    Biome biome = Biome::Plains;
    if (continentalness < -0.22) biome = Biome::Ocean;
    else if (continentalness < -0.10) biome = Biome::Beach;
    else if (temperature > 0.61 && humidity < 0.47) biome = Biome::Desert;
    else if (mountainWeight > 0.58) biome = Biome::Mountains;
    else if (humidity > 0.58) biome = Biome::Forest;
    else if (erosion < 0.39) biome = Biome::Hills;

    const double rolling = terrain.Fractal2D(x * 0.014, z * 0.014, 3, 0.56, 2.0) * 9.0 - 4.5;
    const double foothills = peaks.Fractal2D(x * 0.008, z * 0.008, 3, 0.58, 2.0) * 10.0 - 5.0;
    const double mountains = mountainWeight * (18.0 + peaks.Ridged2D(x * 0.011, z * 0.011, 2, 0.58, 2.0) * 20.0);
    double height = static_cast<double>(kSeaLevel) + rolling + foothills * mountainWeight + mountains - (1.0 - erosion) * 3.0;
    if (biome == Biome::Ocean) height = kSeaLevel - 8.0 + rolling * 0.22;
    if (biome == Biome::Beach) height = kSeaLevel + rolling * 0.16;
    if (biome == Biome::Desert) height += 2.0;
    return {std::clamp(static_cast<int>(std::lround(height)), 5, 70), biome,
            static_cast<float>(SmoothStep(0.12, 0.34, cellEdge))};
}

[[nodiscard]] bool IsCave(std::uint64_t seed, int x, int y, int z, int surfaceY) noexcept {
    if (y < 8 || y >= surfaceY - 8 || (surfaceY <= kSeaLevel && y < kSeaLevel)) return false;
    const Noise worm(PhaseSeed(seed, 5));
    const Noise cavern(PhaseSeed(seed, 6));
    const double tunnel = worm.Ridged3D(x * 0.046, y * 0.062, z * 0.046, 2, 0.58, 2.0);
    const double cavernValue = cavern.Ridged3D(x * 0.022, y * 0.027, z * 0.022, 2, 0.58, 2.0);
    return tunnel > 0.90 || (y < 30 && cavernValue > 0.93);
}

[[nodiscard]] BlockId OreFor(std::uint64_t seed, int x, int y, int z) noexcept {
    const Noise coal(PhaseSeed(seed, 7));
    const Noise iron(PhaseSeed(seed, 8));
    if (y >= 18 && y <= 44 && coal.Ridged3D(x * 0.18, y * 0.18, z * 0.18, 2, 0.55, 2.0) > 0.91) return kCoal;
    if (y >= 6 && y <= 28 && iron.Ridged3D(x * 0.21, y * 0.21, z * 0.21, 2, 0.55, 2.0) > 0.94) return kIron;
    return kStone;
}

void PlaceTree(Chunk& chunk, int worldX, int worldZ, const TerrainColumn& column, std::uint64_t seed) {
    if (column.biome != Biome::Forest && column.biome != Biome::Plains && column.biome != Biome::Hills) return;
    if (column.surfaceY <= kSeaLevel || column.surfaceY > kMaxTreeSurfaceY) return;
    const int cellX = FloorDiv(worldX, kTreeCellSize);
    const int cellZ = FloorDiv(worldZ, kTreeCellSize);
    const std::uint64_t treeHash = Mix(PhaseSeed(seed, 9) ^ (static_cast<std::uint64_t>(cellX) << 32U) ^ static_cast<std::uint32_t>(cellZ));
    const int candidateX = cellX * kTreeCellSize + static_cast<int>(treeHash % kTreeCellSize);
    const int candidateZ = cellZ * kTreeCellSize + static_cast<int>((treeHash >> 8U) % kTreeCellSize);
    if (worldX != candidateX || worldZ != candidateZ) return;
    for (int neighborZ = cellZ - 1; neighborZ <= cellZ + 1; ++neighborZ) {
        for (int neighborX = cellX - 1; neighborX <= cellX + 1; ++neighborX) {
            if (neighborX == cellX && neighborZ == cellZ) continue;
            const std::uint64_t neighborHash = Mix(PhaseSeed(seed, 9) ^ (static_cast<std::uint64_t>(neighborX) << 32U) ^ static_cast<std::uint32_t>(neighborZ));
            const int neighborCandidateX = neighborX * kTreeCellSize + static_cast<int>(neighborHash % kTreeCellSize);
            const int neighborCandidateZ = neighborZ * kTreeCellSize + static_cast<int>((neighborHash >> 8U) % kTreeCellSize);
            if (std::max(std::abs(neighborCandidateX - worldX), std::abs(neighborCandidateZ - worldZ)) <= 4 &&
                neighborHash < treeHash) return;
        }
    }
    const int chance = column.biome == Biome::Forest ? 38 : column.biome == Biome::Hills ? 16 : 8;
    if (static_cast<int>((treeHash >> 16U) % 100U) >= chance) return;
    for (int offsetZ = -1; offsetZ <= 1; ++offsetZ) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            if (std::abs(SampleColumnForSeed(seed, worldX + offsetX, worldZ + offsetZ).surfaceY - column.surfaceY) > 1) return;
        }
    }
    const int height = 4 + static_cast<int>((treeHash >> 24U) % 2U);
    const ChunkCoordinate coordinate = chunk.GetCoordinate();
    const int localX = worldX - coordinate.x * kChunkSize;
    const int localZ = worldZ - coordinate.z * kChunkSize;
    for (int y = 1; y <= height; ++y) {
        const int localY = column.surfaceY + y - coordinate.y * kChunkSize;
        if (chunk.InBounds(localX, localY, localZ) && chunk.GetBlock(localX, localY, localZ) == kAir) chunk.SetBlock(localX, localY, localZ, kWood);
    }
    for (int canopyY = height - 1; canopyY <= height + 2; ++canopyY) {
        const int radius = canopyY == height + 2 ? 1 : 2;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) + std::abs(dz) > radius + (canopyY == height ? 1 : 0)) continue;
                const int localCanopyX = worldX + dx - coordinate.x * kChunkSize;
                const int localCanopyY = column.surfaceY + canopyY - coordinate.y * kChunkSize;
                const int localCanopyZ = worldZ + dz - coordinate.z * kChunkSize;
                if (chunk.InBounds(localCanopyX, localCanopyY, localCanopyZ) &&
                    chunk.GetBlock(localCanopyX, localCanopyY, localCanopyZ) == kAir) {
                    chunk.SetBlock(localCanopyX, localCanopyY, localCanopyZ, kLeaf);
                }
            }
        }
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