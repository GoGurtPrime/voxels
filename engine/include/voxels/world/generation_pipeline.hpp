#pragma once

/**
 * @file generation_pipeline.hpp
 * @brief World generation pipeline: biome sampling, phase interface, and the chunk generator.
 *
 * @details WorldGenerator populates one vertically stacked 16^3 chunk section at a time:
 *          terrain shaping from globally sampled columns, cave carving, ore and tree
 *          placement, then a chunk-local skylight fill (cross-chunk light propagation is a
 *          known follow-up). Output is a pure function of seed and coordinates, so chunks are
 *          identical regardless of generation order or thread count (ARCHITECTURE.md §6.3).
 *          Registered IGenerationPhase objects replace the built-in sequence entirely.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "voxels/world/chunk.hpp"
#include "voxels/world/noise.hpp"
#include "voxels/world/world_options.hpp"

namespace voxels {

/// Coarse biome class from climate/terrain sampling; selects surface material and vegetation.
enum class Biome : std::uint8_t {
    Plains,
    Forest,
    Hills,
    Mountains,
    Desert,
    Beach,
    Ocean
};

/// Per-column terrain sample for a world-space (x, z) position.
struct TerrainColumn {
    int surfaceY = 0;         ///< World-space Y of the topmost terrain block.
    Biome biome = Biome::Plains;
    float biomeBlend = 0.0f;  ///< 0-1 cellular-edge blend factor; computed but not yet consumed.
};

/// One deferred generation action in the simple sequential GenerationPipeline.
using GenerationStep = std::function<void()>;

/// One ordered chunk-generation stage; Execute() mutates the chunk in place. Registering any
/// phase on WorldGenerator overrides its built-in terrain/cave/vegetation/skylight sequence.
class IGenerationPhase {
public:
    virtual ~IGenerationPhase() = default;
    virtual void Execute(Chunk& chunk) = 0;
};

/// Minimal ordered step queue; Run() executes steps in insertion order, skipping empty ones.
struct GenerationPipeline {
    std::vector<GenerationStep> steps;

    void AddStep(GenerationStep step) {
        steps.push_back(std::move(step));
    }

    void Run() {
        for (auto& step : steps) {
            if (step) {
                step();
            }
        }
    }
};

/// Deterministic chunk generator: the same seed and coordinate yield an identical chunk on
/// any thread, enabling parallel generation jobs without ordering constraints.
class WorldGenerator {
public:
    /// Current output revision; recorded in saves and WorldInfo so old worlds keep their shape.
    static constexpr std::uint32_t kGeneratorVersion = 2;

    WorldGenerator();
    explicit WorldGenerator(const WorldOptions& options);

    /// Appends a custom phase; once any phase is registered, only phases run in GenerateChunk.
    void AddPhase(std::unique_ptr<IGenerationPhase> phase);
    /// Builds one fully populated chunk section, including its chunk-local skylight fill.
    [[nodiscard]] Chunk GenerateChunk(const ChunkCoordinate& coordinate) const;
    /// Surface height and biome for a world-space column; pure in (seed, x, z).
    [[nodiscard]] TerrainColumn SampleColumn(int worldX, int worldZ) const noexcept;

    [[nodiscard]] const WorldOptions& GetOptions() const noexcept { return m_options; }

private:
    WorldOptions m_options;
    Noise m_noise;
    std::vector<std::unique_ptr<IGenerationPhase>> m_phases;
};

} // namespace voxels
