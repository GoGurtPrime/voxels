/**
 * @file vmdl_codec.hpp
 * @brief Versioned codec and runtime representation for sub-voxel models.
 *
 * @details Defines the untrusted .vmdl boundary described by ARCHITECTURE.md §6.6.
 * The codec owns no GPU state, allowing tools and runtime loading to share validation.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace voxels {

struct VoxelPaletteEntry {
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;
    std::uint8_t alpha = 255;
    std::uint16_t textureLayer = 0;
    std::uint8_t emissive = 0;
    std::uint8_t flags = 0;

    [[nodiscard]] bool operator==(const VoxelPaletteEntry&) const noexcept = default;
};

struct VoxelModelElement {
    std::string name;
    std::uint32_t firstVoxel = 0;
    std::uint32_t voxelCount = 0;
    std::array<float, 16> localTransform = {1.0f, 0.0f, 0.0f, 0.0f,
                                             0.0f, 1.0f, 0.0f, 0.0f,
                                             0.0f, 0.0f, 1.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, 1.0f};
};

struct VoxelModelAttachment {
    std::string name;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
};

struct VoxelModel {
    static constexpr std::uint16_t kVersion = 1;

    std::array<std::uint8_t, 3> gridSize = {16, 16, 16};
    std::array<float, 3> pivot{};
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax = {1.0f, 1.0f, 1.0f};
    std::vector<VoxelPaletteEntry> palette;
    /// X-fastest palette indices. Zero is empty; nonzero values address palette[index - 1].
    std::vector<std::uint16_t> voxels;
    std::vector<VoxelModelElement> elements;
    std::vector<VoxelModelAttachment> attachments;
    std::string metadataJson = "{}";

    [[nodiscard]] std::size_t GridVoxelCount() const noexcept;
    [[nodiscard]] bool IsSolidAt(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept;
    [[nodiscard]] std::size_t SolidVoxelCount() const noexcept;
    void ComputeBounds() noexcept;
    void Rotate90() noexcept;
    void Mirror() noexcept;
};

class VmdlCodec {
public:
    /// Serializes canonical bytes or throws std::runtime_error when the supplied model is invalid.
    [[nodiscard]] static std::vector<std::byte> Save(const VoxelModel& model);
    /// Decodes and validates a complete VMDL payload or throws std::runtime_error with a cause.
    [[nodiscard]] static VoxelModel Load(std::span<const std::byte> bytes);
};

} // namespace voxels