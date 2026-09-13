/**
 * @file held_item_renderer.hpp
 * @brief Camera-relative OpenGL rendering for the local player's hand and held item.
 *
 * @details Builds compact cuboid silhouettes from item definitions at render time. The renderer
 * consumes presentation input only and never mutates authoritative gameplay state.
 */

#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "voxels/gameplay/inventory.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"

namespace voxels::graphics {

/// One atlas-backed vertex in camera-local coordinates.
struct HeldItemVertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::vec4 tint{1.0f};
    float atlasLayer = 0.0f;
};

/// Builds the player hand and an item-specific procedural cuboid silhouette for tests and render.
[[nodiscard]] std::vector<HeldItemVertex> BuildHeldItemMesh(
    const gameplay::ItemStack& held, const BlockRegistry& registry, const TextureAtlas& atlas,
    bool swinging, float elapsedSeconds);

/// Owns the first-person hand/tool draw resources used after transparent world geometry.
class HeldItemRenderer {
public:
    HeldItemRenderer() = default;
    ~HeldItemRenderer();

    HeldItemRenderer(const HeldItemRenderer&) = delete;
    HeldItemRenderer& operator=(const HeldItemRenderer&) = delete;

    void Render(const Camera& camera, const gameplay::ItemStack& held, const BlockRegistry& registry,
                const TextureAtlas& atlas, bool swinging, float elapsedSeconds);
    void Shutdown();

private:
    [[nodiscard]] bool EnsureResources();

    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_projectionUniform = -1;
    int m_textureUniform = -1;
};

} // namespace voxels::graphics