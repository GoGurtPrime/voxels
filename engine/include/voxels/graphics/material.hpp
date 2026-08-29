/**
 * @file material.hpp
 * @brief API-neutral material and sampler definitions.
 *
 * @details Represents a voxel material instance that references a block atlas texture. Native
 *          renderer backends translate these values to their own descriptor or sampler objects.
 */

#pragma once

#include "voxels/graphics/rhi.hpp"

namespace voxels::graphics {

enum class TextureFilter { Nearest, Linear };
enum class TextureAddressMode { Repeat, ClampToEdge };

struct SamplerState {
    TextureFilter minFilter{TextureFilter::Nearest};
    TextureFilter magFilter{TextureFilter::Nearest};
    TextureAddressMode addressU{TextureAddressMode::Repeat};
    TextureAddressMode addressV{TextureAddressMode::Repeat};
};

struct MaterialInstance {
    TexturePtr blockAtlas;
    SamplerState sampler{};
};

} // namespace voxels::graphics