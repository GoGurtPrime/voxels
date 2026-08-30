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

class Frustum {
public:
    void Update(const glm::mat4& viewProjection);
    [[nodiscard]] bool Intersects(const BoundingBox& bounds) const noexcept;

private:
    std::array<glm::vec4, 6> m_planes{};
};

} // namespace voxels
