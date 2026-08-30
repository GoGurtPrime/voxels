#pragma once

/**
 * @file texture_loader.hpp
 * @brief PNG and image decoding, encoding, and validation utilities using stb_image / stb_image_write.
 *
 * @details Implements RGBA8 image decoding from file or memory with power-of-two, square,
 *          and channel validation. Used by the texture atlas and asset loading systems.
 *          Reference ARCHITECTURE.md §6.6 and work_items/04_block_definitions_textures_and_atlas.md.
 */

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "voxels/assets/asset_manager.hpp"

namespace voxels {

class TextureLoader {
public:
    /// Loads and decodes a PNG/image from a file on disk into an RGBA8 ImageData buffer.
    /// If requirePowerOfTwo or requireSquare is true, validates dimensions and returns std::nullopt on mismatch.
    [[nodiscard]] static std::optional<ImageData> LoadFromFile(
        const std::filesystem::path& filePath,
        bool requirePowerOfTwo = true,
        bool requireSquare = true);

    /// Loads and decodes a PNG/image from a memory buffer into an RGBA8 ImageData buffer.
    [[nodiscard]] static std::optional<ImageData> LoadFromMemory(
        std::span<const std::uint8_t> data,
        bool requirePowerOfTwo = true,
        bool requireSquare = true);

    /// Encodes an RGBA8 pixel buffer as PNG bytes in memory.
    [[nodiscard]] static std::vector<std::uint8_t> EncodePngToMemory(
        int width, int height, int channels, const std::uint8_t* pixels);

    /// Writes an RGBA8 ImageData to a PNG file on disk.
    [[nodiscard]] static bool WritePngToFile(
        const std::filesystem::path& filePath,
        const ImageData& image);

    /// Writes raw pixel buffer to a PNG file on disk.
    [[nodiscard]] static bool WritePngToFile(
        const std::filesystem::path& filePath,
        int width, int height, int channels, const void* pixels);

    /// Returns true if dimension is a non-zero power of two (1, 2, 4, 8, 16, 32, ...).
    [[nodiscard]] static constexpr bool IsPowerOfTwo(int x) noexcept {
        return x > 0 && (x & (x - 1)) == 0;
    }
};

} // namespace voxels
