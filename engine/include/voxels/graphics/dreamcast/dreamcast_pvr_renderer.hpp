/**
 * @file dreamcast_pvr_renderer.hpp
 * @brief Dreamcast PowerVR renderer backend scaffold.
 *
 * @details Defines the backend boundary for KallistiOS TA/PVR polygon DMA submission without
 *          exposing KallistiOS headers to the portable engine or automated test builds.
 */

#pragma once

#include "voxels/graphics/mock/mock_renderer.hpp"

namespace voxels::graphics {

class DreamcastPVRRenderer final : public MockRenderer {
protected:
    [[nodiscard]] RendererBackend backendType() const noexcept override { return RendererBackend::DreamcastPVR; }
};

} // namespace voxels::graphics