/**
 * @file packet.cpp
 * @brief Endian-stable serialization for networking packets.
 *
 * @details Packet fields are encoded explicitly in network byte order rather than relying on
 *          compiler struct layout, preserving protocol compatibility across supported targets.
 */

#include "voxels/networking/packet.hpp"

#include <algorithm>
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

std::vector<std::uint8_t> SerializeConnectAccept(const ConnectAccept& accept) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(72);
    const std::vector<std::uint8_t> state = SerializeEntityState(accept.state);
    bytes.insert(bytes.end(), state.begin(), state.end());
    bytes.push_back(accept.worldReady ? 1 : 0);
    if (accept.worldReady) {
        WriteUnsigned(bytes, accept.world.seed);
        WriteUnsigned(bytes, accept.world.generatorVersion);
        const std::uint8_t flags = static_cast<std::uint8_t>((accept.world.sandboxMode ? 1u : 0u) |
                                                             (accept.world.peaceful ? 2u : 0u) |
                                                             (accept.world.alwaysSunny ? 4u : 0u) |
                                                             (accept.world.permadeath ? 8u : 0u));
        bytes.push_back(flags);
        WriteVec3(bytes, accept.world.spawnPosition);
    }
    return bytes;
}

bool DeserializeConnectAccept(std::span<const std::uint8_t> bytes, ConnectAccept& accept) {
    constexpr std::size_t kEntityStateBytes = 40;
    if (bytes.size() < kEntityStateBytes + 1 ||
        !DeserializeEntityState(bytes.first(kEntityStateBytes), accept.state)) {
        return false;
    }
    std::size_t offset = kEntityStateBytes;
    const std::uint8_t ready = bytes[offset++];
    if (ready > 1) return false;
    accept.worldReady = ready == 1;
    if (!accept.worldReady) return offset == bytes.size();
    std::uint8_t flags = 0;
    if (!ReadUnsigned(bytes, offset, accept.world.seed) ||
        !ReadUnsigned(bytes, offset, accept.world.generatorVersion) ||
        !ReadUnsigned(bytes, offset, flags) || (flags & ~0x0Fu) != 0 ||
        !ReadVec3(bytes, offset, accept.world.spawnPosition) || offset != bytes.size()) {
        return false;
    }
    accept.world.sandboxMode = (flags & 1u) != 0;
    accept.world.peaceful = (flags & 2u) != 0;
    accept.world.alwaysSunny = (flags & 4u) != 0;
    accept.world.permadeath = (flags & 8u) != 0;
    return true;
}

std::vector<std::uint8_t> SerializeChunkFragment(const ChunkFragment& fragment) {
    if (fragment.data.size() > kMaximumChunkFragmentBytes ||
        fragment.totalBytes > kMaximumChunkTransferBytes) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(24 + fragment.data.size());
    WriteInt32(bytes, fragment.chunkCoordinate.x);
    WriteInt32(bytes, fragment.chunkCoordinate.y);
    WriteInt32(bytes, fragment.chunkCoordinate.z);
    WriteUnsigned(bytes, fragment.fragmentIndex);
    WriteUnsigned(bytes, fragment.fragmentCount);
    WriteUnsigned(bytes, fragment.totalBytes);
    bytes.insert(bytes.end(), fragment.data.begin(), fragment.data.end());
    return bytes;
}

bool DeserializeChunkFragment(std::span<const std::uint8_t> bytes, ChunkFragment& fragment) {
    std::size_t offset = 0;
    if (!ReadInt32(bytes, offset, fragment.chunkCoordinate.x) ||
        !ReadInt32(bytes, offset, fragment.chunkCoordinate.y) ||
        !ReadInt32(bytes, offset, fragment.chunkCoordinate.z) ||
        !ReadUnsigned(bytes, offset, fragment.fragmentIndex) ||
        !ReadUnsigned(bytes, offset, fragment.fragmentCount) ||
        !ReadUnsigned(bytes, offset, fragment.totalBytes)) {
        return false;
    }
    const std::size_t dataBytes = bytes.size() - offset;
    if (fragment.fragmentCount == 0 || fragment.fragmentIndex >= fragment.fragmentCount ||
        fragment.totalBytes == 0 || fragment.totalBytes > kMaximumChunkTransferBytes ||
        dataBytes == 0 || dataBytes > kMaximumChunkFragmentBytes || dataBytes > fragment.totalBytes) {
        return false;
    }
    // Every fragment except the last is full-sized; the last carries the exact remainder.
    const std::size_t expectedBytes = fragment.fragmentIndex + 1 == fragment.fragmentCount
        ? fragment.totalBytes - static_cast<std::size_t>(fragment.fragmentCount - 1) * kMaximumChunkFragmentBytes
        : kMaximumChunkFragmentBytes;
    if (fragment.totalBytes <= static_cast<std::size_t>(fragment.fragmentCount - 1) * kMaximumChunkFragmentBytes ||
        dataBytes != expectedBytes) {
        return false;
    }
    fragment.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    return true;
}

std::vector<std::uint8_t> SerializeVec3I(const Vec3I& value) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(12);
    WriteInt32(bytes, value.x);
    WriteInt32(bytes, value.y);
    WriteInt32(bytes, value.z);
    return bytes;
}

bool DeserializeVec3I(std::span<const std::uint8_t> bytes, Vec3I& value) {
    std::size_t offset = 0;
    return ReadInt32(bytes, offset, value.x) && ReadInt32(bytes, offset, value.y) &&
           ReadInt32(bytes, offset, value.z) && offset == bytes.size();
}

std::vector<ChunkFragment> FragmentChunkPayload(const Vec3I& chunkCoordinate,
                                                std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaximumChunkTransferBytes) {
        return {};
    }
    const std::size_t fragmentCount = (payload.size() + kMaximumChunkFragmentBytes - 1) / kMaximumChunkFragmentBytes;
    if (fragmentCount > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }
    std::vector<ChunkFragment> fragments;
    fragments.reserve(fragmentCount);
    for (std::size_t index = 0; index < fragmentCount; ++index) {
        const std::size_t begin = index * kMaximumChunkFragmentBytes;
        const std::size_t length = std::min(kMaximumChunkFragmentBytes, payload.size() - begin);
        ChunkFragment fragment{chunkCoordinate, static_cast<std::uint16_t>(index),
                               static_cast<std::uint16_t>(fragmentCount),
                               static_cast<std::uint32_t>(payload.size()), {}};
        fragment.data.assign(payload.begin() + static_cast<std::ptrdiff_t>(begin),
                             payload.begin() + static_cast<std::ptrdiff_t>(begin + length));
        fragments.push_back(std::move(fragment));
    }
    return fragments;
}

} // namespace voxels::networking