/**
 * @file remote_player_renderer.hpp
 * @brief Renders replicated remote players as oriented body/head boxes.
 *
 * @details Draws in the model/entity pass of DIAGRAMS.md diagram 6 between opaque terrain and
 *          the HUD. Consumes plain visual structs so the render layer stays independent of the
 *          networking layer; the app layer converts replicated entity state into visuals.
 */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "voxels/render/camera.hpp"

namespace voxels::graphics {

/// One replicated player to draw: interpolation happens inside the renderer.
struct RemotePlayerVisual {
    std::uint32_t playerId = 0;
    glm::vec3 position{};
    float yawRadians = 0.0f;
};

class RemotePlayerRenderer {
public:
    ~RemotePlayerRenderer();
    void Render(const Camera& camera, const std::vector<RemotePlayerVisual>& players, float deltaSeconds);
    void Shutdown();

private:
    struct SmoothedPlayer {
        glm::vec3 position{};
        float yawRadians = 0.0f;
        bool seenThisFrame = false;
    };

    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_vertexCount = 0;
    std::unordered_map<std::uint32_t, SmoothedPlayer> m_smoothed;
};

} // namespace voxels::graphics
