/**
 * @file camera.cpp
 * @brief Implements perspective and view-matrix math for the GL renderer test scene.
 */

#include "voxels/render/camera.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace voxels {

namespace {

glm::vec3 NormalizeDirection(glm::vec3 direction) {
    const float length = glm::length(direction);
    if (length < 1.0e-5f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return direction / length;
}

} // namespace

glm::mat4 Camera::Projection() const {
    return glm::perspective(fovY, aspect, nearPlane, farPlane);
}

glm::mat4 Camera::View() const {
    const float cosPitch = std::cos(pitch);
    const float sinPitch = std::sin(pitch);
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);

    const glm::vec3 forward = NormalizeDirection(glm::vec3(
        -sinYaw * cosPitch,
        sinPitch,
        -cosYaw * cosPitch));
    const glm::vec3 target = position + forward;
    return glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::ViewProjection() const {
    return Projection() * View();
}

void Frustum::Update(const glm::mat4& viewProjection) {
    const glm::mat4 matrix = glm::transpose(viewProjection);
    m_planes[0] = glm::normalize(matrix[3] + matrix[0]); // left
    m_planes[1] = glm::normalize(matrix[3] - matrix[0]); // right
    m_planes[2] = glm::normalize(matrix[3] + matrix[1]); // bottom
    m_planes[3] = glm::normalize(matrix[3] - matrix[1]); // top
    m_planes[4] = glm::normalize(matrix[3] + matrix[2]); // near
    m_planes[5] = glm::normalize(matrix[3] - matrix[2]); // far
}

bool Frustum::Intersects(const BoundingBox& bounds) const noexcept {
    const glm::vec3 minPoint(static_cast<float>(bounds.min.x), static_cast<float>(bounds.min.y),
                             static_cast<float>(bounds.min.z));
    const glm::vec3 maxPoint(static_cast<float>(bounds.max.x), static_cast<float>(bounds.max.y),
                             static_cast<float>(bounds.max.z));
    for (const glm::vec4& plane : m_planes) {
        const glm::vec3 positiveCorner = {
            plane.x >= 0.0f ? maxPoint.x : minPoint.x,
            plane.y >= 0.0f ? maxPoint.y : minPoint.y,
            plane.z >= 0.0f ? maxPoint.z : minPoint.z,
        };
        const float signedDistance = glm::dot(glm::vec3(plane), positiveCorner) + plane.w;
        if (signedDistance < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace voxels
