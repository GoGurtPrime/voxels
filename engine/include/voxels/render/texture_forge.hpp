#pragma once

/**
 * @file texture_forge.hpp
 * @brief Procedural pixel art texture generator for voxel block types and missing-texture fallbacks.
 *
 * @details Implements deterministic, seeded procedural synthesis for all launch block textures
 *          (granite stone, speckled dirt, grass blades and side-fringe, wood bark rings,
 *          translucent water, ore flecks, leaves with alpha cutouts, etc.) and exports them
 *          as RGBA8 ImageData buffers or disk PNGs.
 *          Reference ARCHITECTURE.md §6.6 and work_items/04_block_definitions_textures_and_atlas.md.
 */

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "voxels/assets/asset_manager.hpp"

namespace voxels {

class TextureForge {
public:
    static constexpr int kDefaultTextureSize = 16;

    /// Generates a deterministic 16x16 RGBA8 texture for the requested texture identifier.
    /// If textureName is unknown, returns the standard missing-texture magenta/black checker.
    [[nodiscard]] static ImageData GenerateTexture(
        std::string_view textureName,
        std::uint32_t seed = 1337);

    /// Generates a standard magenta/black checkerboard image of the specified dimensions.
    [[nodiscard]] static ImageData GenerateMissingTexture(
        int width = kDefaultTextureSize,
        int height = kDefaultTextureSize);

    /// Generates one of ten progressive 16x16 RGBA8 mining-crack overlays.
    [[nodiscard]] static ImageData GenerateCrackTexture(int stage);

    /// Returns the list of standard launch texture identifiers.
    [[nodiscard]] static std::vector<std::string> GetLaunchTextureNames();

    /// Forges all launch block textures into the given target directory on disk.
    /// Writes `<targetDirectory>/<name>.png`. Returns the number of files written.
    static std::size_t ForgeLaunchTextures(
        const std::filesystem::path& targetDirectory,
        bool overwrite = false,
        std::uint32_t seed = 1337);

    /// Forges the crosshair, hotbar frame, selection frame, and ten crack overlays below `assetRoot`.
    static std::size_t ForgeInteractionAssets(
        const std::filesystem::path& assetRoot,
        bool overwrite = false);
};

} // namespace voxels
