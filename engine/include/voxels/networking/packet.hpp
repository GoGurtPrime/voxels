/**
 * @file packet.hpp
 * @brief Binary UDP packet protocol used by the client/server networking layer.
 *
 * @details Defines endian-stable packet framing and payload structures for gameplay state,
 *          block updates, chunk transfer, and keep-alives. Serialization is transport-neutral
 *          so platform socket backends can share the same protocol contract.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "voxels/core/game_types.hpp"
#include "voxels/core/math.hpp"

namespace voxels::networking {

enum class PacketId : std::uint16_t {
    C2S_Connect = 1,
    S2C_ConnectAck,
    C2S_PlayerMove,
    S2C_EntityState,
    C2S_BlockModify,
    S2C_BlockUpdate,
    S2C_ChunkData,
    C2S_KeepAlive,
    S2C_KeepAliveAck,
    C2S_Disconnect,
    S2C_PlayerLeft
};

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kMaximumPacketPayloadBytes = 1200;

struct PacketHeader {
    PacketId id{};
    std::uint32_t sequenceNum = 0;
    std::uint16_t payloadSize = 0;
    std::uint16_t protocolVersion = kProtocolVersion;
};

struct PlayerMove {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 velocity{};
};

struct EntityState {
    std::uint32_t entityId = 0;
    PlayerMove movement{};
};

struct BlockModify {
    Vec3I position{};
    BlockId blockId = 0;
};

struct ChunkDataPayload {
    Vec3I chunkCoordinate{};
    std::vector<std::uint8_t> compressedData;
};

struct Packet {
    PacketHeader header{};
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::vector<std::uint8_t> SerializePacket(const Packet& packet);
[[nodiscard]] bool DeserializePacket(std::span<const std::uint8_t> bytes, Packet& packet);
[[nodiscard]] std::vector<std::uint8_t> SerializePlayerMove(const PlayerMove& movement);
[[nodiscard]] bool DeserializePlayerMove(std::span<const std::uint8_t> bytes, PlayerMove& movement);
[[nodiscard]] std::vector<std::uint8_t> SerializeEntityState(const EntityState& state);
[[nodiscard]] bool DeserializeEntityState(std::span<const std::uint8_t> bytes, EntityState& state);
[[nodiscard]] std::vector<std::uint8_t> SerializeBlockModify(const BlockModify& modify);
[[nodiscard]] bool DeserializeBlockModify(std::span<const std::uint8_t> bytes, BlockModify& modify);

} // namespace voxels::networking