/**
 * @file vmdl_codec.cpp
 * @brief Deterministic, bounds-checked VMDL serialization and model transforms.
 *
 * @details Implements the little-endian VMDL v1 boundary in ARCHITECTURE.md §6.6.
 */

#include "voxels/assets/vmdl_codec.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace voxels {
namespace {

constexpr std::size_t kHeaderSize = 65;
constexpr std::size_t kCrcOffset = 61;
constexpr std::size_t kPaletteEntrySize = 8;

[[nodiscard]] std::uint32_t Crc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

template <typename T>
void Write(std::vector<std::byte>& output, T value) {
    static_assert(std::is_integral_v<T> || std::is_same_v<T, float>);
    using Raw = std::conditional_t<std::is_same_v<T, float>, std::uint32_t, T>;
    Raw raw = 0;
    if constexpr (std::is_same_v<T, float>) {
        raw = std::bit_cast<std::uint32_t>(value);
    } else {
        raw = static_cast<Raw>(value);
    }
    for (std::size_t i = 0; i < sizeof(Raw); ++i) {
        output.push_back(static_cast<std::byte>((raw >> (i * 8U)) & 0xFFU));
    }
}

template <typename T>
[[nodiscard]] T Read(std::span<const std::byte> input, std::size_t& offset, const char* field) {
    static_assert(std::is_integral_v<T> || std::is_same_v<T, float>);
    using Raw = std::conditional_t<std::is_same_v<T, float>, std::uint32_t, T>;
    if (offset > input.size() || input.size() - offset < sizeof(Raw)) {
        throw std::runtime_error(std::string("VMDL truncated while reading ") + field + ".");
    }
    Raw raw = 0;
    for (std::size_t i = 0; i < sizeof(Raw); ++i) {
        raw |= static_cast<Raw>(static_cast<std::uint8_t>(input[offset++])) << (i * 8U);
    }
    if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<float>(static_cast<std::uint32_t>(raw));
    } else {
        return static_cast<T>(raw);
    }
}

void WriteName(std::vector<std::byte>& output, const std::string& name) {
    if (name.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("VMDL name exceeds the 65535-byte limit.");
    }
    Write<std::uint16_t>(output, static_cast<std::uint16_t>(name.size()));
    for (const char character : name) output.push_back(static_cast<std::byte>(character));
}

[[nodiscard]] std::string ReadName(std::span<const std::byte> input, std::size_t& offset, const char* field) {
    const std::uint16_t length = Read<std::uint16_t>(input, offset, field);
    if (offset > input.size() || input.size() - offset < length) {
        throw std::runtime_error(std::string("VMDL truncated while reading ") + field + " name.");
    }
    std::string name;
    name.reserve(length);
    for (std::uint16_t i = 0; i < length; ++i) name.push_back(static_cast<char>(input[offset++]));
    return name;
}

void ValidateModel(const VoxelModel& model) {
    const std::size_t gridCount = model.GridVoxelCount();
    if (gridCount == 0 || model.voxels.size() != gridCount) {
        throw std::runtime_error("VMDL voxel grid size does not match declared dimensions.");
    }
    if (model.palette.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("VMDL palette exceeds the 65535-entry limit.");
    }
    for (const std::uint16_t index : model.voxels) {
        if (index > model.palette.size()) throw std::runtime_error("VMDL voxel palette index is out of range.");
    }
    if (model.elements.size() > std::numeric_limits<std::uint16_t>::max() ||
        model.attachments.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("VMDL element or attachment count exceeds the 65535-entry limit.");
    }
}

} // namespace

std::size_t VoxelModel::GridVoxelCount() const noexcept {
    return static_cast<std::size_t>(gridSize[0]) * gridSize[1] * gridSize[2];
}

bool VoxelModel::IsSolidAt(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept {
    if (x >= gridSize[0] || y >= gridSize[1] || z >= gridSize[2]) return false;
    const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(gridSize[0]) *
        (static_cast<std::size_t>(y) + static_cast<std::size_t>(gridSize[1]) * z);
    return index < voxels.size() && voxels[index] != 0;
}

std::size_t VoxelModel::SolidVoxelCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(voxels.begin(), voxels.end(), [](std::uint16_t index) { return index != 0; }));
}

void VoxelModel::ComputeBounds() noexcept {
    bool found = false;
    std::array<std::uint32_t, 3> minimum{};
    std::array<std::uint32_t, 3> maximum{};
    for (std::uint32_t z = 0; z < gridSize[2]; ++z) for (std::uint32_t y = 0; y < gridSize[1]; ++y) for (std::uint32_t x = 0; x < gridSize[0]; ++x) {
        if (!IsSolidAt(x, y, z)) continue;
        if (!found) { minimum = {x, y, z}; maximum = {x + 1, y + 1, z + 1}; found = true; }
        else {
            minimum = {std::min(minimum[0], x), std::min(minimum[1], y), std::min(minimum[2], z)};
            maximum = {std::max(maximum[0], x + 1), std::max(maximum[1], y + 1), std::max(maximum[2], z + 1)};
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float scale = 1.0F / static_cast<float>(gridSize[axis]);
        boundsMin[axis] = found ? static_cast<float>(minimum[axis]) * scale : 0.0F;
        boundsMax[axis] = found ? static_cast<float>(maximum[axis]) * scale : 0.0F;
    }
}

void VoxelModel::Rotate90() noexcept {
    std::vector<std::uint16_t> rotated(voxels.size());
    for (std::uint32_t z = 0; z < gridSize[2]; ++z) for (std::uint32_t y = 0; y < gridSize[1]; ++y) for (std::uint32_t x = 0; x < gridSize[0]; ++x) {
        const std::size_t source = x + static_cast<std::size_t>(gridSize[0]) * (y + static_cast<std::size_t>(gridSize[1]) * z);
        const std::uint32_t rotatedX = gridSize[2] - 1U - z;
        const std::uint32_t rotatedZ = x;
        const std::size_t target = rotatedX + static_cast<std::size_t>(gridSize[2]) * (y + static_cast<std::size_t>(gridSize[1]) * rotatedZ);
        rotated[target] = voxels[source];
    }
    std::swap(gridSize[0], gridSize[2]);
    voxels = std::move(rotated);
    for (auto& attachment : attachments) {
        const float oldX = attachment.position[0];
        attachment.position[0] = 1.0F - attachment.position[2];
        attachment.position[2] = oldX;
        attachment.rotation[1] += 90.0F;
    }
    ComputeBounds();
}

void VoxelModel::Mirror() noexcept {
    for (std::uint32_t z = 0; z < gridSize[2]; ++z) for (std::uint32_t y = 0; y < gridSize[1]; ++y) for (std::uint32_t x = 0; x < gridSize[0] / 2U; ++x) {
        const std::size_t left = x + static_cast<std::size_t>(gridSize[0]) * (y + static_cast<std::size_t>(gridSize[1]) * z);
        const std::size_t right = (gridSize[0] - 1U - x) + static_cast<std::size_t>(gridSize[0]) * (y + static_cast<std::size_t>(gridSize[1]) * z);
        std::swap(voxels[left], voxels[right]);
    }
    for (auto& attachment : attachments) attachment.position[0] = 1.0F - attachment.position[0];
    ComputeBounds();
}

std::vector<std::byte> VmdlCodec::Save(const VoxelModel& model) {
    ValidateModel(model);
    std::vector<std::byte> output;
    output.reserve(kHeaderSize + model.palette.size() * kPaletteEntrySize + model.voxels.size() * 4U + model.metadataJson.size());
    for (const char value : {'V', 'M', 'D', 'L'}) output.push_back(static_cast<std::byte>(value));
    Write<std::uint16_t>(output, VoxelModel::kVersion); Write<std::uint16_t>(output, 0);
    for (const std::uint8_t dimension : model.gridSize) Write<std::uint8_t>(output, dimension);
    for (const float value : model.pivot) Write<float>(output, value);
    for (const float value : model.boundsMin) Write<float>(output, value);
    for (const float value : model.boundsMax) Write<float>(output, value);
    Write<std::uint16_t>(output, static_cast<std::uint16_t>(model.palette.size()));
    Write<std::uint32_t>(output, static_cast<std::uint32_t>(model.SolidVoxelCount()));
    Write<std::uint16_t>(output, static_cast<std::uint16_t>(model.elements.size()));
    Write<std::uint16_t>(output, static_cast<std::uint16_t>(model.attachments.size()));
    const std::size_t metadataOffsetPatch = output.size(); Write<std::uint32_t>(output, 0); Write<std::uint32_t>(output, 0);
    for (const auto& entry : model.palette) {
        Write<std::uint8_t>(output, entry.red); Write<std::uint8_t>(output, entry.green); Write<std::uint8_t>(output, entry.blue); Write<std::uint8_t>(output, entry.alpha);
        Write<std::uint16_t>(output, entry.textureLayer); Write<std::uint8_t>(output, entry.emissive); Write<std::uint8_t>(output, entry.flags);
    }
    for (std::size_t index = 0; index < model.voxels.size();) {
        const std::uint16_t value = model.voxels[index];
        std::size_t run = 1;
        while (index + run < model.voxels.size() && model.voxels[index + run] == value && run < std::numeric_limits<std::uint16_t>::max()) ++run;
        Write<std::uint16_t>(output, static_cast<std::uint16_t>(run)); Write<std::uint16_t>(output, value); index += run;
    }
    for (const auto& element : model.elements) {
        WriteName(output, element.name); Write<std::uint32_t>(output, element.firstVoxel); Write<std::uint32_t>(output, element.voxelCount);
        for (const float value : element.localTransform) Write<float>(output, value);
    }
    for (const auto& attachment : model.attachments) {
        WriteName(output, attachment.name); for (const float value : attachment.position) Write<float>(output, value); for (const float value : attachment.rotation) Write<float>(output, value);
    }
    if (output.size() > std::numeric_limits<std::uint32_t>::max() || model.metadataJson.size() > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("VMDL payload exceeds 4 GiB.");
    const std::uint32_t metadataOffset = static_cast<std::uint32_t>(output.size());
    for (std::size_t i = 0; i < sizeof(metadataOffset); ++i) output[metadataOffsetPatch + i] = static_cast<std::byte>((metadataOffset >> (i * 8U)) & 0xFFU);
    for (const char character : model.metadataJson) output.push_back(static_cast<std::byte>(character));
    const std::uint32_t crc = Crc32(std::span<const std::byte>(output).subspan(kHeaderSize));
    for (std::size_t i = 0; i < sizeof(crc); ++i) output[kCrcOffset + i] = static_cast<std::byte>((crc >> (i * 8U)) & 0xFFU);
    return output;
}

VoxelModel VmdlCodec::Load(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) throw std::runtime_error("VMDL file is smaller than its header.");
    if (static_cast<char>(bytes[0]) != 'V' || static_cast<char>(bytes[1]) != 'M' || static_cast<char>(bytes[2]) != 'D' || static_cast<char>(bytes[3]) != 'L') throw std::runtime_error("VMDL bad magic; expected VMDL.");
    std::size_t offset = 4;
    const std::uint16_t version = Read<std::uint16_t>(bytes, offset, "version");
    if (version != VoxelModel::kVersion) throw std::runtime_error("VMDL unsupported version " + std::to_string(version) + ".");
    (void)Read<std::uint16_t>(bytes, offset, "flags");
    VoxelModel model;
    for (auto& dimension : model.gridSize) { dimension = Read<std::uint8_t>(bytes, offset, "grid dimension"); if (dimension == 0) throw std::runtime_error("VMDL grid dimensions must be nonzero."); }
    for (auto& value : model.pivot) value = Read<float>(bytes, offset, "pivot");
    for (auto& value : model.boundsMin) value = Read<float>(bytes, offset, "bounds minimum");
    for (auto& value : model.boundsMax) value = Read<float>(bytes, offset, "bounds maximum");
    const std::uint16_t paletteCount = Read<std::uint16_t>(bytes, offset, "palette count");
    const std::uint32_t declaredVoxelCount = Read<std::uint32_t>(bytes, offset, "voxel count");
    const std::uint16_t elementCount = Read<std::uint16_t>(bytes, offset, "element count");
    const std::uint16_t attachmentCount = Read<std::uint16_t>(bytes, offset, "attachment count");
    const std::uint32_t metadataOffset = Read<std::uint32_t>(bytes, offset, "metadata offset");
    const std::uint32_t storedCrc = Read<std::uint32_t>(bytes, offset, "crc32");
    if (Crc32(bytes.subspan(kHeaderSize)) != storedCrc) throw std::runtime_error("VMDL CRC mismatch.");
    if (metadataOffset < kHeaderSize || metadataOffset > bytes.size()) throw std::runtime_error("VMDL metadata offset is outside the file.");
    model.palette.reserve(paletteCount);
    for (std::uint16_t i = 0; i < paletteCount; ++i) {
        VoxelPaletteEntry entry;
        entry.red = Read<std::uint8_t>(bytes, offset, "palette red"); entry.green = Read<std::uint8_t>(bytes, offset, "palette green"); entry.blue = Read<std::uint8_t>(bytes, offset, "palette blue"); entry.alpha = Read<std::uint8_t>(bytes, offset, "palette alpha");
        entry.textureLayer = Read<std::uint16_t>(bytes, offset, "palette texture layer"); entry.emissive = Read<std::uint8_t>(bytes, offset, "palette emissive"); entry.flags = Read<std::uint8_t>(bytes, offset, "palette flags"); model.palette.push_back(entry);
    }
    const std::size_t gridCount = model.GridVoxelCount();
    model.voxels.reserve(gridCount);
    while (model.voxels.size() < gridCount) {
        const std::uint16_t run = Read<std::uint16_t>(bytes, offset, "RLE run length");
        const std::uint16_t paletteIndex = Read<std::uint16_t>(bytes, offset, "RLE palette index");
        if (run == 0 || run > gridCount - model.voxels.size()) throw std::runtime_error("VMDL RLE voxel count mismatch.");
        if (paletteIndex > paletteCount) throw std::runtime_error("VMDL RLE palette index is out of range.");
        model.voxels.insert(model.voxels.end(), run, paletteIndex);
    }
    for (std::uint16_t i = 0; i < elementCount; ++i) {
        VoxelModelElement element; element.name = ReadName(bytes, offset, "element"); element.firstVoxel = Read<std::uint32_t>(bytes, offset, "element first voxel"); element.voxelCount = Read<std::uint32_t>(bytes, offset, "element voxel count");
        if (element.firstVoxel > gridCount || element.voxelCount > gridCount - element.firstVoxel) throw std::runtime_error("VMDL element voxel range is outside the grid.");
        for (auto& value : element.localTransform) value = Read<float>(bytes, offset, "element transform"); model.elements.push_back(std::move(element));
    }
    for (std::uint16_t i = 0; i < attachmentCount; ++i) {
        VoxelModelAttachment attachment; attachment.name = ReadName(bytes, offset, "attachment"); for (auto& value : attachment.position) value = Read<float>(bytes, offset, "attachment position"); for (auto& value : attachment.rotation) value = Read<float>(bytes, offset, "attachment rotation"); model.attachments.push_back(std::move(attachment));
    }
    if (offset != metadataOffset) throw std::runtime_error("VMDL metadata offset does not follow encoded data.");
    model.metadataJson.clear();
    model.metadataJson.reserve(bytes.size() - metadataOffset);
    for (std::size_t i = metadataOffset; i < bytes.size(); ++i) model.metadataJson.push_back(static_cast<char>(bytes[i]));
    if (model.SolidVoxelCount() != declaredVoxelCount) throw std::runtime_error("VMDL declared voxel count does not match RLE payload.");
    ValidateModel(model);
    return model;
}

} // namespace voxels