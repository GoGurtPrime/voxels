/**
 * @file camera.hpp
 * @brief Camera and frustum math used by the OpenGL render path and debug culling.
 *
 * @details Defines a minimal perspective camera and a frustum built from the view-projection
 *          matrix so render validation can cull scene bounds and maintain a responsive orbit
 *          camera in the temporary rendering test scene.
 */

#pragma once

#include <array>

#include <glm/glm.hpp>

#include "voxels/world/geometry.hpp"

namespace voxels {

/// Perspective camera; right-handed, Y-up, positions in world blocks. Yaw and pitch are in
/// radians: yaw 0 faces -Z and increases counter-clockwise around +Y, positive pitch looks up
/// (no clamping here - callers such as CameraController clamp).
struct Camera {
    glm::vec3 position{0.0f, 1.5f, 5.0f};
    float yaw{-90.0f};
    float pitch{0.0f};
    float fovY{glm::radians(60.0f)};
    float aspect{1.0f};
    float nearPlane{0.1f};
    float farPlane{100.0f};

    [[nodiscard]] glm::mat4 Projection() const;
    [[nodiscard]] glm::mat4 View() const;
    [[nodiscard]] glm::mat4 ViewProjection() const;
};

/// View frustum as six inward-facing planes extracted from a view-projection matrix
/// (Gribb-Hartmann); drives chunk AABB culling.
class Frustum {
public:
    /// Re-extracts the six planes; call whenever the camera moves.
    void Update(const glm::mat4& viewProjection);
    /// Conservative world-space AABB test (positive-vertex trick): true when the box is inside
    /// or straddles the frustum. May keep borderline boxes but never falsely culls.
    [[nodiscard]] bool Intersects(const BoundingBox& bounds) const noexcept;

private:
    std::array<glm::vec4, 6> m_planes{};
};

} // namespace voxels
