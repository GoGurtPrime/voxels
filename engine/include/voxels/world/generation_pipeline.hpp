#pragma once

/*
 * Scope: Extensible world generation pipeline.
 *
 * Generation is intentionally implemented as a queue of stages so rough terrain, cave
 * carving, and vegetation systems can be added, removed, or reordered without rewriting
 * the world system itself.
 *
 * Relation to the rest of the codebase: the world manager uses this abstraction to drive
 * the loading and generation sequence before the player is placed in the world.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "voxels/world/chunk.hpp"
#include "voxels/world/noise.hpp"
#include "voxels/world/world_options.hpp"

namespace voxels {

using GenerationStep = std::function<void()>;

class IGenerationPhase {
public:
    virtual ~IGenerationPhase() = default;
    virtual void Execute(Chunk& chunk) = 0;
};

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

class WorldGenerator {
public:
    WorldGenerator();
    explicit WorldGenerator(const WorldOptions& options);

    void AddPhase(std::unique_ptr<IGenerationPhase> phase);
    [[nodiscard]] Chunk GenerateChunk(const ChunkCoordinate& coordinate) const;

    [[nodiscard]] const WorldOptions& GetOptions() const noexcept { return m_options; }

private:
    WorldOptions m_options;
    Noise m_noise;
    std::vector<std::unique_ptr<IGenerationPhase>> m_phases;
};

} // namespace voxels
