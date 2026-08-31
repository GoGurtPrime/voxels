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
#include <unordered_map>
#include <utility>

namespace voxels::networking {

class GameClient::Impl {
public:
    asio::io_context ioContext;
    asio::ip::udp::socket socket{ioContext};
    asio::ip::udp::endpoint serverEndpoint;
    std::uint32_t sequenceNumber = 0;
    std::uint32_t playerId = 0;
    bool connected = false;
    bool receivedConnectAck = false;
    std::unordered_map<std::uint32_t, EntityState> receivedEntityStates;
    std::vector<BlockModify> receivedBlockUpdates;
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

bool GameClient::Connect(std::string host, std::uint16_t port) {
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
    SendPacket(*m_impl, PacketId::C2S_Connect);
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
    m_impl->playerId = 0;
    m_impl->receivedEntityStates.clear();
    m_impl->receivedBlockUpdates.clear();
}

void GameClient::Tick() {
    if (!m_impl->connected) {
        return;
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
        if (packet.header.id == PacketId::S2C_ConnectAck) {
            EntityState state;
            if (DeserializeEntityState(packet.payload, state)) {
                m_impl->playerId = state.entityId;
                m_impl->receivedEntityStates.insert_or_assign(state.entityId, state);
                m_impl->receivedConnectAck = true;
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

} // namespace voxels::networking