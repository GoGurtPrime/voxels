/**
 * @file packet.cpp
 * @brief Endian-stable serialization for networking packets.
 *
 * @details Packet fields are encoded explicitly in network byte order rather than relying on
 *          compiler struct layout, preserving protocol compatibility across supported targets.
 */

#include "voxels/networking/packet.hpp"

#include <limits>

namespace voxels::networking {

namespace {

template <typename T>
void WriteUnsigned(std::vector<std::uint8_t>& output, T value) {
    for (int shift = static_cast<int>(sizeof(T) * 8) - 8; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

template <typename T>
bool ReadUnsigned(std::span<const std::uint8_t> input, std::size_t& offset, T& value) {
    if (input.size() - offset < sizeof(T)) {
        return false;
    }
    value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value = static_cast<T>((value << 8) | input[offset++]);
    }
    return true;
}

void WriteInt32(std::vector<std::uint8_t>& output, int value) {
    WriteUnsigned(output, static_cast<std::uint32_t>(value));
}

bool ReadInt32(std::span<const std::uint8_t> input, std::size_t& offset, int& value) {
    std::uint32_t raw = 0;
    if (!ReadUnsigned(input, offset, raw)) {
        return false;
    }
    value = static_cast<int>(raw);
    return true;
}

} // namespace

std::vector<std::uint8_t> SerializePacket(const Packet& packet) {
    if (packet.payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(8 + packet.payload.size());
    WriteUnsigned(bytes, static_cast<std::uint16_t>(packet.header.id));
    WriteUnsigned(bytes, packet.header.sequenceNum);
    WriteUnsigned(bytes, static_cast<std::uint16_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return bytes;
}

bool DeserializePacket(std::span<const std::uint8_t> bytes, Packet& packet) {
    std::size_t offset = 0;
    std::uint16_t id = 0;
    std::uint16_t payloadSize = 0;
    if (!ReadUnsigned(bytes, offset, id) || !ReadUnsigned(bytes, offset, packet.header.sequenceNum) ||
        !ReadUnsigned(bytes, offset, payloadSize) || bytes.size() - offset != payloadSize) {
        return false;
    }
    packet.header.id = static_cast<PacketId>(id);
    packet.header.payloadSize = payloadSize;
    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    return true;
}

std::vector<std::uint8_t> SerializeBlockModify(const BlockModify& modify) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(14);
    WriteInt32(bytes, modify.position.x);
    WriteInt32(bytes, modify.position.y);
    WriteInt32(bytes, modify.position.z);
    WriteUnsigned(bytes, modify.blockId);
    return bytes;
}

bool DeserializeBlockModify(std::span<const std::uint8_t> bytes, BlockModify& modify) {
    std::size_t offset = 0;
    return ReadInt32(bytes, offset, modify.position.x) && ReadInt32(bytes, offset, modify.position.y) &&
           ReadInt32(bytes, offset, modify.position.z) && ReadUnsigned(bytes, offset, modify.blockId) &&
           offset == bytes.size();
}

} // namespace voxels::networking