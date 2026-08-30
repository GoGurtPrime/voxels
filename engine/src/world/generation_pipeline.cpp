#include "voxels/world/generation_pipeline.hpp"

#include <algorithm>
#include <cmath>

#include "voxels/world/block.hpp"
#include "voxels/world/noise.hpp"

namespace voxels {

namespace {
constexpr int kChunkSize = 16;
constexpr int kWaterLevel = 62;

class TerrainShapePhase : public IGenerationPhase {
public:
    explicit TerrainShapePhase(Noise noise) : m_noise(std::move(noise)) {}

    void Execute(Chunk& chunk) override {
        const int width = static_cast<int>(chunk.GetWidth());
        const int depth = static_cast<int>(chunk.GetDepth());
        const int height = static_cast<int>(chunk.GetHeight());
        const int chunkX = chunk.GetCoordinate().x * kChunkSize;
        const int chunkZ = chunk.GetCoordinate().z * kChunkSize;
        const int chunkYOrigin = chunk.GetCoordinate().y * height;

        for (int z = 0; z < depth; ++z) {
            for (int x = 0; x < width; ++x) {
                const double nx = static_cast<double>(chunkX + x) * 0.12;
                const double nz = static_cast<double>(chunkZ + z) * 0.12;
                const double terrain = m_noise.Fractal2D(nx, nz, 5, 0.55, 2.0);
                const int baseHeight = static_cast<int>(std::round(terrain * 18.0 + 28.0));
                const bool surfaceIsUnderwater = baseHeight <= kWaterLevel;

                for (int y = 0; y < height; ++y) {
                    const int worldY = chunkYOrigin + y;
                    if (worldY <= 2) {
                        chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Stone));
                    } else if (worldY <= baseHeight) {
                        if (worldY > baseHeight - 3) {
                            const bool isTopLayer = worldY == baseHeight;
                            if (isTopLayer && surfaceIsUnderwater) {
                                chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Sand));
                            } else if (isTopLayer) {
                                chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Grass));
                            } else {
                                chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Dirt));
                            }
                        } else {
                            chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Stone));
                        }
                    } else if (worldY <= kWaterLevel) {
                        chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Water));
                    } else {
                        chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Air));
                    }
                }
            }
        }
    }

private:
    Noise m_noise;
};

class CavePhase : public IGenerationPhase {
public:
    explicit CavePhase(Noise noise) : m_noise(std::move(noise)) {}

    void Execute(Chunk& chunk) override {
        const int width = static_cast<int>(chunk.GetWidth());
        const int depth = static_cast<int>(chunk.GetDepth());
        const int height = static_cast<int>(chunk.GetHeight());

        for (int z = 0; z < depth; ++z) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double caveValue = m_noise.Evaluate3D(
                        static_cast<double>(x) * 0.35 + 7.0,
                        static_cast<double>(y) * 0.35 + 11.0,
                        static_cast<double>(z) * 0.35 + 13.0);
                    if (chunk.GetBlock(x, y, z) == static_cast<BlockId>(BlockType::Stone) && caveValue > 0.62 && y < 40) {
                        chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Air));
                    }
                }
            }
        }
    }

private:
    Noise m_noise;
};

class VegetationPhase : public IGenerationPhase {
public:
    explicit VegetationPhase(Noise noise) : m_noise(std::move(noise)) {}

    void Execute(Chunk& chunk) override {
        const int width = static_cast<int>(chunk.GetWidth());
        const int depth = static_cast<int>(chunk.GetDepth());
        const int height = static_cast<int>(chunk.GetHeight());

        for (int z = 0; z < depth; ++z) {
            for (int x = 0; x < width; ++x) {
                int surface = height - 1;
                while (surface > 0 && chunk.GetBlock(x, surface, z) == static_cast<BlockId>(BlockType::Air)) {
                    --surface;
                }
                if (surface <= 0 || (chunk.GetBlock(x, surface, z) != static_cast<BlockId>(BlockType::Dirt) &&
                                     chunk.GetBlock(x, surface, z) != static_cast<BlockId>(BlockType::Grass))) {
                    continue;
                }

                const double treeNoise = m_noise.Fractal2D(
                    static_cast<double>(x + chunk.GetCoordinate().x * kChunkSize) * 0.65,
                    static_cast<double>(z + chunk.GetCoordinate().z * kChunkSize) * 0.65,
                    2,
                    0.7,
                    2.5);
                if (treeNoise > 0.68 && surface > 4 && surface < height - 3) {
                    for (int y = surface + 1; y <= surface + 4 && y < height; ++y) {
                        if (chunk.GetBlock(x, y, z) == static_cast<BlockId>(BlockType::Air)) {
                            chunk.SetBlock(x, y, z, static_cast<BlockId>(BlockType::Wood));
                        }
                    }
                    for (int oy = 0; oy < 3; ++oy) {
                        for (int ox = -2; ox <= 2; ++ox) {
                            for (int oz = -2; oz <= 2; ++oz) {
                                const int px = x + ox;
                                const int pz = z + oz;
                                const int py = surface + 4 + oy;
                                if (px >= 0 && px < width && pz >= 0 && pz < depth && py >= 0 && py < height) {
                                    const double distance = std::sqrt(static_cast<double>(ox * ox + oz * oz + oy * oy));
                                    if (distance <= 2.2 && chunk.GetBlock(px, py, pz) == static_cast<BlockId>(BlockType::Air)) {
                                        chunk.SetBlock(px, py, pz, static_cast<BlockId>(BlockType::Leaf));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

private:
    Noise m_noise;
};

/// Simple chunk-local top-down skylight fill: each column is lit at full brightness from the
/// chunk's top down to (and including) the first opaque block; everything below stays dark.
/// This is a deliberately cheap approximation - it does not propagate sky light across chunk
/// boundaries (e.g. an underground chunk sitting below a solid chunk above it) - full multi-chunk
/// light propagation is out of scope for work item 05 and belongs to work item 12.
class SkylightPhase : public IGenerationPhase {
public:
    void Execute(Chunk& chunk) override {
        const int width = static_cast<int>(chunk.GetWidth());
        const int depth = static_cast<int>(chunk.GetDepth());
        const int height = static_cast<int>(chunk.GetHeight());

        for (int z = 0; z < depth; ++z) {
            for (int x = 0; x < width; ++x) {
                bool lit = true;
                for (int y = height - 1; y >= 0; --y) {
                    const BlockId block = chunk.GetBlock(x, y, z);
                    if (lit) {
                        chunk.SetSkyLight(x, y, z, 15);
                        if (block != static_cast<BlockId>(BlockType::Air) &&
                            block != static_cast<BlockId>(BlockType::Water)) {
                            lit = false; // this block itself is lit; everything strictly below is not
                        }
                    }
                }
            }
        }
    }
};
} // namespace

WorldGenerator::WorldGenerator() : WorldGenerator(WorldOptions{}) {}

WorldGenerator::WorldGenerator(const WorldOptions& options) : m_options(options), m_noise(options.seed + 1u) {}

void WorldGenerator::AddPhase(std::unique_ptr<IGenerationPhase> phase) {
    if (phase) {
        m_phases.push_back(std::move(phase));
    }
}

Chunk WorldGenerator::GenerateChunk(const ChunkCoordinate& coordinate) const {
    Chunk chunk(coordinate, kChunkSize, kChunkSize, kChunkSize);

    if (!m_phases.empty()) {
        for (const auto& phase : m_phases) {
            if (phase) {
                phase->Execute(chunk);
            }
        }
        return chunk;
    }

    TerrainShapePhase terrainPhase(Noise(m_options.seed + 11u));
    terrainPhase.Execute(chunk);

    if (!m_options.peaceful) {
        CavePhase cavePhase(Noise(m_options.seed + 27u));
        cavePhase.Execute(chunk);
    }

    if (!m_options.sandboxMode) {
        VegetationPhase vegetationPhase(Noise(m_options.seed + 51u));
        vegetationPhase.Execute(chunk);
    }

    SkylightPhase skylightPhase;
    skylightPhase.Execute(chunk);

    return chunk;
}

} // namespace voxels
