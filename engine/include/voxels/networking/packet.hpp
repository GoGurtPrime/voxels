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
#include "voxels/world/world_clock.hpp"
#include "voxels/world/world_options.hpp"

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
    S2C_PlayerLeft,
    S2C_Reject,
    S2C_Disconnect,
    C2S_ChunkAck,
    S2C_WorldTime
};

inline constexpr std::uint16_t kProtocolVersion = 4;
inline constexpr std::size_t kMaximumPacketPayloadBytes = 1200;
/// Upper bound accepted for a reassembled RLE chunk payload (a 16^3 section is far smaller).
inline constexpr std::uint32_t kMaximumChunkTransferBytes = 512u * 1024u;
/// Fragment data budget leaving room for the fragment header inside the packet payload bound.
inline constexpr std::size_t kMaximumChunkFragmentBytes = 1100;

enum class ClientKind : std::uint8_t { Remote = 0, InProcessHost = 1 };

enum class RejectReason : std::uint8_t { ServerFull = 1, WorldNotReady = 2, WorldPrivate = 3 };

enum class DisconnectReason : std::uint8_t { HostClosedWorld = 1, ServerShutdown = 2 };

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

/// World identity the server hands a joining client so both simulate the same rules.
struct WorldInfo {
    std::uint64_t seed = 0;
    std::uint32_t generatorVersion = 0;
    bool sandboxMode = false;
    bool peaceful = false;
    bool alwaysSunny = true;
    bool permadeath = false;
    Vec3 spawnPosition{};
    WorldTick worldTick = kInitialWorldTick;
};

/// Handshake acceptance: the joiner's assigned entity plus the host world, when one is live.
struct ConnectAccept {
    EntityState state{};
    bool worldReady = false;
    WorldInfo world{};
};

/// One MTU-safe slice of an RLE-serialized chunk section in transit.
struct ChunkFragment {
    Vec3I chunkCoordinate{};
    std::uint16_t fragmentIndex = 0;
    std::uint16_t fragmentCount = 0;
    std::uint32_t totalBytes = 0;
    std::vector<std::uint8_t> data;
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
[[nodiscard]] std::vector<std::uint8_t> SerializeConnectAccept(const ConnectAccept& accept);
[[nodiscard]] bool DeserializeConnectAccept(std::span<const std::uint8_t> bytes, ConnectAccept& accept);
[[nodiscard]] std::vector<std::uint8_t> SerializeChunkFragment(const ChunkFragment& fragment);
[[nodiscard]] bool DeserializeChunkFragment(std::span<const std::uint8_t> bytes, ChunkFragment& fragment);
[[nodiscard]] std::vector<std::uint8_t> SerializeVec3I(const Vec3I& value);
[[nodiscard]] bool DeserializeVec3I(std::span<const std::uint8_t> bytes, Vec3I& value);
[[nodiscard]] std::vector<std::uint8_t> SerializeWorldTime(WorldTick worldTick);
[[nodiscard]] bool DeserializeWorldTime(std::span<const std::uint8_t> bytes, WorldTick& worldTick);

/// Splits an RLE chunk payload into MTU-safe fragments; empty result when the payload is
/// empty or exceeds the transfer bound.
[[nodiscard]] std::vector<ChunkFragment> FragmentChunkPayload(const Vec3I& chunkCoordinate,
                                                              std::span<const std::uint8_t> payload);

} // namespace voxels::networking