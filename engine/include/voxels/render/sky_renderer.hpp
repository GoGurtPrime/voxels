/**
 * @file sky_renderer.hpp
 * @brief Renders the celestial sky background and derives chunk fog bounds.
 *
 * @details Provides the OpenGL 3.3 background pass used before world geometry and the
 *          render-distance fog contract consumed by chunk shaders. The inputs are sampled
 *          from authoritative presentation state; this module never advances world time.
 */

#pragma once

#include <glad/glad.h>

#include "voxels/render/camera.hpp"
#include "voxels/render/celestial_lighting.hpp"

namespace voxels::graphics {

inline constexpr float kChunkWidthBlocks = 16.0f;

/// Camera-relative distances in blocks at which terrain fog begins and becomes opaque.
struct FogRange {
    float startBlocks = 64.0f;
    float endBlocks = 112.0f;
};

/// Keeps fog inside the loaded horizontal world boundary, reserving its final chunk.
[[nodiscard]] FogRange FogRangeForRenderDistance(int renderDistanceChunks) noexcept;
/// Derives the horizon color shared by the background and fogged world geometry.
[[nodiscard]] Vec3 SkyHorizonColor(const CelestialLighting& lighting) noexcept;

/// OpenGL 3.3 full-screen procedural sky. All calls must occur on the render thread.
class SkyRenderer {
public:
    SkyRenderer() = default;
    ~SkyRenderer();

    SkyRenderer(const SkyRenderer&) = delete;
    SkyRenderer& operator=(const SkyRenderer&) = delete;

    /// Draws the background before opaque world geometry, preserving depth for later passes.
    void Render(const Camera& camera, const CelestialLighting& lighting);
    /// Releases GL objects while a context remains current.
    void Shutdown() noexcept;

private:
    [[nodiscard]] bool EnsureProgram();

    GLuint m_program = 0;
    GLuint m_vertexArray = 0;
    GLint m_inverseViewProjectionUniform = -1;
    GLint m_zenithColorUniform = -1;
    GLint m_horizonColorUniform = -1;
    GLint m_cameraPositionUniform = -1;
    GLint m_sunDirectionUniform = -1;
    GLint m_sunColorUniform = -1;
};

} // namespace voxels::graphics