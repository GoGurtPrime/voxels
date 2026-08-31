/**
 * @file client.cpp
 * @brief Nonblocking Asio UDP client implementation.
 *
 * @details The client sends handshake and gameplay packets to its configured server endpoint,
 *          then records authoritative updates received during each explicit network tick.
 */

#include "voxels/networking/client.hpp"

#include <asio.hpp>

#include <array>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace voxels::networking {

namespace {

struct Vec3IHash {
    std::size_t operator()(const Vec3I& value) const noexcept {
        std::size_t seed = std::hash<int>{}(value.x);
        seed ^= std::hash<int>{}(value.y) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>{}(value.z) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct ChunkReassembly {
    std::uint16_t fragmentCount = 0;
    std::uint32_t totalBytes = 0;
    std::uint16_t receivedFragments = 0;
    std::vector<std::vector<std::uint8_t>> parts;
};

constexpr std::chrono::seconds kKeepAliveInterval{2};

} // namespace

class GameClient::Impl {
public:
    asio::io_context ioContext;
    asio::ip::udp::socket socket{ioContext};
    asio::ip::udp::endpoint serverEndpoint;
    std::uint32_t sequenceNumber = 0;
    std::uint32_t playerId = 0;
    bool connected = false;
    bool receivedConnectAck = false;
    bool worldReady = false;
    WorldInfo worldInfo{};
    bool rejected = false;
    RejectReason rejectReason = RejectReason::ServerFull;
    bool disconnectedByServer = false;
    std::chrono::steady_clock::time_point lastServerPacket = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastKeepAlive = std::chrono::steady_clock::now();
    std::unordered_map<std::uint32_t, EntityState> receivedEntityStates;
    std::vector<BlockModify> receivedBlockUpdates;
    std::vector<std::uint32_t> departedPlayers;
    std::unordered_map<Vec3I, ChunkReassembly, Vec3IHash> reassembly;
    std::unordered_set<Vec3I, Vec3IHash> acknowledgedChunks;
    std::vector<NetworkChunk> completedChunks;
};

namespace {

template <typename ClientImpl>
void SendPacket(ClientImpl& impl, PacketId id, std::vector<std::uint8_t> payload = {}) {
    Packet packet{{id, ++impl.sequenceNumber, 0}, std::move(payload)};
    const std::vector<std::uint8_t> bytes = SerializePacket(packet);
    asio::error_code error;
    impl.socket.send_to(asio::buffer(bytes), impl.serverEndpoint, 0, error);
}

} // namespace

GameClient::GameClient() : m_impl(std::make_unique<Impl>()) {}

GameClient::~GameClient() { Disconnect(); }

bool GameClient::Connect(std::string host, std::uint16_t port, ClientKind kind) {
    Disconnect();
    asio::error_code error;
    const auto address = asio::ip::make_address(host, error);
    if (error) {
        return false;
    }
    m_impl->socket.open(asio::ip::udp::v4(), error);
    if (error) {
        return false;
    }
    m_impl->socket.non_blocking(true, error);
    if (error) {
        Disconnect();
        return false;
    }
    m_impl->serverEndpoint = {address, port};
    m_impl->connected = true;
    m_impl->lastServerPacket = std::chrono::steady_clock::now();
    m_impl->lastKeepAlive = m_impl->lastServerPacket;
    SendPacket(*m_impl, PacketId::C2S_Connect, {static_cast<std::uint8_t>(kind)});
    return true;
}

void GameClient::Disconnect() {
    if (m_impl->connected && m_impl->socket.is_open()) {
        SendPacket(*m_impl, PacketId::C2S_Disconnect);
    }
    if (m_impl->socket.is_open()) {
        asio::error_code error;
        m_impl->socket.close(error);
    }
    m_impl->connected = false;
    m_impl->receivedConnectAck = false;
    m_impl->worldReady = false;
    m_impl->worldInfo = {};
    m_impl->rejected = false;
    m_impl->disconnectedByServer = false;
    m_impl->playerId = 0;
    m_impl->receivedEntityStates.clear();
    m_impl->receivedBlockUpdates.clear();
    m_impl->departedPlayers.clear();
    m_impl->reassembly.clear();
    m_impl->acknowledgedChunks.clear();
    m_impl->completedChunks.clear();
}

void GameClient::Tick() {
    if (!m_impl->connected) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - m_impl->lastKeepAlive >= kKeepAliveInterval) {
        SendPacket(*m_impl, PacketId::C2S_KeepAlive);
        m_impl->lastKeepAlive = now;
    }
    std::array<std::uint8_t, 65535> buffer{};
    asio::ip::udp::endpoint sender;
    for (;;) {
        asio::error_code error;
        const std::size_t received = m_impl->socket.receive_from(asio::buffer(buffer), sender, 0, error);
        if (error == asio::error::would_block || error == asio::error::try_again) {
            break;
        }
        if (error) {
            break;
        }
        Packet packet;
        if (!DeserializePacket(std::span<const std::uint8_t>(buffer.data(), received), packet)) {
            continue;
        }
        if (sender != m_impl->serverEndpoint) continue;
        m_impl->lastServerPacket = now;
        if (packet.header.id == PacketId::S2C_ConnectAck) {
            ConnectAccept accept;
            if (DeserializeConnectAccept(packet.payload, accept)) {
                m_impl->playerId = accept.state.entityId;
                m_impl->receivedEntityStates.insert_or_assign(accept.state.entityId, accept.state);
                m_impl->receivedConnectAck = true;
                m_impl->worldReady = accept.worldReady;
                if (accept.worldReady) m_impl->worldInfo = accept.world;
            }
        } else if (packet.header.id == PacketId::S2C_EntityState) {
            EntityState state;
            if (DeserializeEntityState(packet.payload, state)) {
                m_impl->receivedEntityStates.insert_or_assign(state.entityId, state);
            }
        } else if (packet.header.id == PacketId::S2C_BlockUpdate) {
            BlockModify modify;
            if (DeserializeBlockModify(packet.payload, modify)) {
                m_impl->receivedBlockUpdates.push_back(modify);
            }
        } else if (packet.header.id == PacketId::S2C_ChunkData) {
            ChunkFragment fragment;
            if (!DeserializeChunkFragment(packet.payload, fragment)) continue;
            if (m_impl->acknowledgedChunks.contains(fragment.chunkCoordinate)) {
                // The server retransmitted a chunk whose completion ack was lost; re-ack it.
                SendPacket(*m_impl, PacketId::C2S_ChunkAck, SerializeVec3I(fragment.chunkCoordinate));
                continue;
            }
            ChunkReassembly& entry = m_impl->reassembly[fragment.chunkCoordinate];
            if (entry.fragmentCount == 0) {
                entry.fragmentCount = fragment.fragmentCount;
                entry.totalBytes = fragment.totalBytes;
                entry.parts.resize(fragment.fragmentCount);
            } else if (entry.fragmentCount != fragment.fragmentCount || entry.totalBytes != fragment.totalBytes) {
                m_impl->reassembly.erase(fragment.chunkCoordinate);
                continue;
            }
            std::vector<std::uint8_t>& slot = entry.parts[fragment.fragmentIndex];
            if (!slot.empty()) continue;
            slot = std::move(fragment.data);
            ++entry.receivedFragments;
            if (entry.receivedFragments < entry.fragmentCount) continue;
            const std::uint32_t expectedBytes = entry.totalBytes;
            NetworkChunk chunk{fragment.chunkCoordinate, {}};
            chunk.rleData.reserve(expectedBytes);
            for (const std::vector<std::uint8_t>& part : entry.parts) {
                chunk.rleData.insert(chunk.rleData.end(), part.begin(), part.end());
            }
            m_impl->reassembly.erase(fragment.chunkCoordinate);
            if (chunk.rleData.size() != expectedBytes) continue;
            SendPacket(*m_impl, PacketId::C2S_ChunkAck, SerializeVec3I(chunk.coordinate));
            m_impl->acknowledgedChunks.insert(chunk.coordinate);
            m_impl->completedChunks.push_back(std::move(chunk));
        } else if (packet.header.id == PacketId::S2C_PlayerLeft) {
            if (packet.payload.size() != 4) continue;
            const std::uint32_t departedId = (static_cast<std::uint32_t>(packet.payload[0]) << 24) |
                                             (static_cast<std::uint32_t>(packet.payload[1]) << 16) |
                                             (static_cast<std::uint32_t>(packet.payload[2]) << 8) |
                                             static_cast<std::uint32_t>(packet.payload[3]);
            m_impl->receivedEntityStates.erase(departedId);
            m_impl->departedPlayers.push_back(departedId);
        } else if (packet.header.id == PacketId::S2C_Reject) {
            if (packet.payload.size() != 1 || packet.payload[0] < 1 || packet.payload[0] > 3) continue;
            m_impl->rejected = true;
            m_impl->rejectReason = static_cast<RejectReason>(packet.payload[0]);
        } else if (packet.header.id == PacketId::S2C_Disconnect) {
            m_impl->disconnectedByServer = true;
        }
    }
}

void GameClient::SendPlayerMove(const PlayerMove& movement) {
    if (m_impl->connected && m_impl->receivedConnectAck) {
        SendPacket(*m_impl, PacketId::C2S_PlayerMove, SerializePlayerMove(movement));
    }
}

void GameClient::SendBlockModify(const BlockModify& modify) {
    if (m_impl->connected) {
        SendPacket(*m_impl, PacketId::C2S_BlockModify, SerializeBlockModify(modify));
    }
}

bool GameClient::IsConnected() const noexcept { return m_impl->connected; }

bool GameClient::HasReceivedConnectAck() const noexcept { return m_impl->receivedConnectAck; }

std::uint32_t GameClient::PlayerId() const noexcept { return m_impl->playerId; }

const std::unordered_map<std::uint32_t, EntityState>& GameClient::ReceivedEntityStates() const noexcept {
    return m_impl->receivedEntityStates;
}

const std::vector<BlockModify>& GameClient::ReceivedBlockUpdates() const noexcept {
    return m_impl->receivedBlockUpdates;
}

std::vector<BlockModify> GameClient::TakeReceivedBlockUpdates() {
    std::vector<BlockModify> updates;
    updates.swap(m_impl->receivedBlockUpdates);
    return updates;
}

std::vector<NetworkChunk> GameClient::TakeCompletedChunks() {
    std::vector<NetworkChunk> chunks;
    chunks.swap(m_impl->completedChunks);
    return chunks;
}

std::vector<std::uint32_t> GameClient::TakeDepartedPlayers() {
    std::vector<std::uint32_t> departed;
    departed.swap(m_impl->departedPlayers);
    return departed;
}

bool GameClient::IsWorldReadyOnServer() const noexcept { return m_impl->worldReady; }

const WorldInfo& GameClient::GetWorldInfo() const noexcept { return m_impl->worldInfo; }

bool GameClient::WasRejected() const noexcept { return m_impl->rejected; }

RejectReason GameClient::GetRejectReason() const noexcept { return m_impl->rejectReason; }

bool GameClient::WasDisconnectedByServer() const noexcept { return m_impl->disconnectedByServer; }

double GameClient::SecondsSinceLastServerPacket() const noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_impl->lastServerPacket).count();
}

} // namespace voxels::networking