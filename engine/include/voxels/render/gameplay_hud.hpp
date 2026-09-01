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

/// Immediate-style GL overlay for gameplay feedback: crosshair, hotbar, target highlight, mining
/// cracks, break particles, and the selected-item label. GL objects are created lazily on the
/// first Render and every draw is skipped when GL is not loaded, so headless runs are safe.
class GameplayHudRenderer {
public:
    ~GameplayHudRenderer();
    /// `breakProgress` in [0,1] selects one of the ten crack overlay stages on the targeted
    /// block; `selectedItemLabelAge` is seconds since the hotbar selection changed (hold/fade);
    /// `particleBursts` are world block coords to draw break particles at this frame.
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
    unsigned int m_crackProgram = 0;
    unsigned int m_crackVao = 0;
    unsigned int m_crackVbo = 0;
    std::array<unsigned int, 10> m_crackTextures{};
    std::array<stbtt_bakedchar, 96> m_glyphs{};
    bool m_fontReady = false;
};

} // namespace voxels::graphics