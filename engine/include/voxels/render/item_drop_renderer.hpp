/**
 * @file item_drop_renderer.hpp
 * @brief Atlas-backed world rendering for dropped block and inventory items.
 *
 * @details Resolves item ids through the block catalogue, builds presentation-only cube,
 *          authored-model, or crossed-icon geometry, and renders bounded material batches in
 *          the entity pass described by ARCHITECTURE.md section 6.1 and DIAGRAMS.md diagram 6.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "voxels/gameplay/item_drop.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/render/celestial_lighting.hpp"
#include "voxels/render/model_registry.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"

namespace voxels::graphics {

/// Vertex consumed by the item entity shader. Positions are in world blocks.
struct ItemDropVertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::vec4 tint{1.0f};
    float atlasLayer = 0.0f;
};

/// CPU-side geometry grouped by depth/blend behavior so draw count stays bounded.
struct ItemDropRenderBatch {
    std::vector<ItemDropVertex> opaque;
    std::vector<ItemDropVertex> cutout;
    std::vector<ItemDropVertex> transparent;
};

/// Resolves every drop to catalogue material and presentation geometry without mutating it.
[[nodiscard]] ItemDropRenderBatch BuildItemDropRenderBatch(
    const std::vector<gameplay::ItemDrop>& drops, const BlockRegistry& registry,
    const TextureAtlas& atlas, const ModelRegistry* models = nullptr);

/// Per-frame draw statistics for performance and regression checks.
struct ItemDropRenderMetrics {
    std::size_t visibleDrops = 0;
    std::size_t drawCalls = 0;
    std::size_t triangles = 0;
    double cpuBuildMilliseconds = 0.0;
};

/// Owns the dropped-item GL resources and draws all visible drops in at most three batches.
class ItemDropRenderer {
public:
    ItemDropRenderer(BlockRegistry& registry, TextureAtlas& atlas);
    ~ItemDropRenderer();

    ItemDropRenderer(const ItemDropRenderer&) = delete;
    ItemDropRenderer& operator=(const ItemDropRenderer&) = delete;

    void SetCelestialLighting(const CelestialLighting& lighting) noexcept { m_lighting = lighting; }
    void Render(const Camera& camera, const std::vector<gameplay::ItemDrop>& drops);
    void Shutdown();

    [[nodiscard]] const ItemDropRenderMetrics& GetMetrics() const noexcept { return m_metrics; }

private:
    [[nodiscard]] bool EnsureResources();
    void DrawBatch(const std::vector<ItemDropVertex>& vertices, bool blend, bool cull);

    BlockRegistry& m_registry;
    TextureAtlas& m_atlas;
    ModelRegistry m_models;
    CelestialLighting m_lighting{};
    ItemDropRenderMetrics m_metrics{};
    std::unordered_set<BlockId> m_reportedMissingItems;
    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_viewProjectionUniform = -1;
    int m_sunDirectionUniform = -1;
    int m_sunColorUniform = -1;
    int m_ambientColorUniform = -1;
    int m_textureUniform = -1;
};

} // namespace voxels::graphics