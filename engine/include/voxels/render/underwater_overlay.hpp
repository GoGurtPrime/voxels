/**
 * @file underwater_overlay.hpp
 * @brief Full-screen blue absorption/fog treatment applied only when the camera eye is submerged.
 *
 * @details One constant-cost OpenGL 3.3 full-screen pass drawn after transparent world geometry
 *          and before HUD/UI passes (see DIAGRAMS.md diagram 6). It never runs above water, so the
 *          above-water sky and fog established by `SkyRenderer` are left untouched.
 */

#pragma once

#include <glad/glad.h>

#include "voxels/core/math.hpp"

namespace voxels::graphics {

/// OpenGL 3.3 full-screen underwater tint. All calls must occur on the render thread.
class UnderwaterOverlay {
public:
    UnderwaterOverlay() = default;
    ~UnderwaterOverlay();

    UnderwaterOverlay(const UnderwaterOverlay&) = delete;
    UnderwaterOverlay& operator=(const UnderwaterOverlay&) = delete;

    /// Draws a translucent blue tint over the whole framebuffer, scaled by `submersionFraction`
    /// (0..1). Callers must only invoke this when the eye is actually submerged; the overlay does
    /// not sample world state itself.
    void Render(float submersionFraction);
    /// Releases GL objects while a context remains current.
    void Shutdown() noexcept;

private:
    [[nodiscard]] bool EnsureProgram();

    GLuint m_program = 0;
    GLuint m_vertexArray = 0;
    GLint m_tintColorUniform = -1;
    GLint m_tintAlphaUniform = -1;
};

} // namespace voxels::graphics
