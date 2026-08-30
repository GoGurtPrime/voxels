/**
 * @file gameplay_hud.hpp
 * @brief OpenGL primitives for the in-game target feedback and hotbar HUD.
 *
 * @details Draws after terrain in the HUD/debug passes from DIAGRAMS.md diagram 6 and does not
 *          depend on the menu UI system.
 */

#pragma once

#include <array>
#include <string>
#include <vector>

#include <imstb_truetype.h>
#include "voxels/gameplay/inventory.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/world/world.hpp"

namespace voxels::graphics {

class GameplayHudRenderer {
public:
    ~GameplayHudRenderer();
    void Render(const Camera& camera, const RaycastHit& target, float breakProgress,
                const gameplay::Inventory& inventory, const std::string& selectedItemLabel,
                float selectedItemLabelAge, const std::vector<Vec3I>& particleBursts);
    void Shutdown();

private:
    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_textProgram = 0;
    unsigned int m_textVao = 0;
    unsigned int m_textVbo = 0;
    unsigned int m_fontTexture = 0;
    std::array<stbtt_bakedchar, 96> m_glyphs{};
    bool m_fontReady = false;
};

} // namespace voxels::graphics