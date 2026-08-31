/**
 * @file packet.cpp
 * @brief Endian-stable serialization for networking packets.
 *
 * @details Packet fields are encoded explicitly in network byte order rather than relying on
 *          compiler struct layout, preserving protocol compatibility across supported targets.
 */

#include "voxels/networking/packet.hpp"

#include <limits>
#include <bit>
#include <cmath>

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

void WriteFloat(std::vector<std::uint8_t>& output, float value) {
    WriteUnsigned(output, std::bit_cast<std::uint32_t>(value));
}

bool ReadFloat(std::span<const std::uint8_t> input, std::size_t& offset, float& value) {
    std::uint32_t bits = 0;
    if (!ReadUnsigned(input, offset, bits)) return false;
    value = std::bit_cast<float>(bits);
    return std::isfinite(value);
}

void WriteVec3(std::vector<std::uint8_t>& output, const Vec3& value) {
    WriteFloat(output, value.x);
    WriteFloat(output, value.y);
    WriteFloat(output, value.z);
}

bool ReadVec3(std::span<const std::uint8_t> input, std::size_t& offset, Vec3& value) {
    return ReadFloat(input, offset, value.x) && ReadFloat(input, offset, value.y) &&
           ReadFloat(input, offset, value.z);
}

} // namespace

std::vector<std::uint8_t> SerializePacket(const Packet& packet) {
    if (packet.header.protocolVersion != kProtocolVersion ||
        packet.payload.size() > kMaximumPacketPayloadBytes) {
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(10 + packet.payload.size());
    WriteUnsigned(bytes, packet.header.protocolVersion);
    WriteUnsigned(bytes, static_cast<std::uint16_t>(packet.header.id));
    WriteUnsigned(bytes, packet.header.sequenceNum);
    WriteUnsigned(bytes, static_cast<std::uint16_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return bytes;
}

bool DeserializePacket(std::span<const std::uint8_t> bytes, Packet& packet) {
    std::size_t offset = 0;
    std::uint16_t protocolVersion = 0;
    std::uint16_t id = 0;
    std::uint16_t payloadSize = 0;
    if (!ReadUnsigned(bytes, offset, protocolVersion) || protocolVersion != kProtocolVersion ||
        !ReadUnsigned(bytes, offset, id) || !ReadUnsigned(bytes, offset, packet.header.sequenceNum) ||
        !ReadUnsigned(bytes, offset, payloadSize) || payloadSize > kMaximumPacketPayloadBytes ||
        bytes.size() - offset != payloadSize) {
        return false;
    }
    packet.header.id = static_cast<PacketId>(id);
    packet.header.payloadSize = payloadSize;
    packet.header.protocolVersion = protocolVersion;
    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    return true;
}

std::vector<std::uint8_t> SerializePlayerMove(const PlayerMove& movement) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(36);
    WriteVec3(bytes, movement.position);
    WriteVec3(bytes, movement.rotation);
    WriteVec3(bytes, movement.velocity);
    return bytes;
}

bool DeserializePlayerMove(std::span<const std::uint8_t> bytes, PlayerMove& movement) {
    std::size_t offset = 0;
    return ReadVec3(bytes, offset, movement.position) && ReadVec3(bytes, offset, movement.rotation) &&
           ReadVec3(bytes, offset, movement.velocity) && offset == bytes.size();
}

std::vector<std::uint8_t> SerializeEntityState(const EntityState& state) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(40);
    WriteUnsigned(bytes, state.entityId);
    const std::vector<std::uint8_t> movement = SerializePlayerMove(state.movement);
    bytes.insert(bytes.end(), movement.begin(), movement.end());
    return bytes;
}

bool DeserializeEntityState(std::span<const std::uint8_t> bytes, EntityState& state) {
    std::size_t offset = 0;
    if (!ReadUnsigned(bytes, offset, state.entityId)) return false;
    return DeserializePlayerMove(bytes.subspan(offset), state.movement);
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