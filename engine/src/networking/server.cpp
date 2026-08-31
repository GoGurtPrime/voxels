/**
 * @file server.cpp
 * @brief Nonblocking Asio UDP server implementation.
 *
 * @details The server processes all pending datagrams on each explicit tick, tracks connected
 *          peers by endpoint, applies block modifications to its authoritative World, and
 *          broadcasts accepted changes to every connected client.
 */

#include "voxels/networking/server.hpp"

#include <asio.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

#include "voxels/networking/packet.hpp"

namespace voxels::networking {

class GameServer::Impl {
public:
    struct Peer {
        asio::ip::udp::endpoint endpoint;
        std::chrono::steady_clock::time_point lastSeen;
        std::uint32_t playerId = 0;
    };

    asio::io_context ioContext;
    asio::ip::udp::socket socket{ioContext};
    World world;
    std::unordered_map<std::string, Peer> peers;
    std::unordered_map<std::uint32_t, EntityState> playerStates;
    std::uint32_t sequenceNumber = 0;
    std::uint32_t nextPlayerId = 1;
    bool running = false;
};

namespace {

std::string PeerKey(const asio::ip::udp::endpoint& endpoint) {
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

template <typename ServerImpl>
void SendPacket(ServerImpl& impl, const asio::ip::udp::endpoint& endpoint, PacketId id,
                std::vector<std::uint8_t> payload = {}) {
    Packet packet{{id, ++impl.sequenceNumber, 0}, std::move(payload)};
    const std::vector<std::uint8_t> bytes = SerializePacket(packet);
    asio::error_code error;
    impl.socket.send_to(asio::buffer(bytes), endpoint, 0, error);
}

bool IsMovementAccepted(const PlayerMove& previous, const PlayerMove& requested) {
    const float dx = requested.position.x - previous.position.x;
    const float dy = requested.position.y - previous.position.y;
    const float dz = requested.position.z - previous.position.z;
    constexpr float kMaximumMovementPerNetworkTick = 1.5f;
    return dx * dx + dy * dy + dz * dz <= kMaximumMovementPerNetworkTick * kMaximumMovementPerNetworkTick;
}

bool IsEditInReach(const PlayerMove& movement, const BlockModify& modify) {
    const float dx = static_cast<float>(modify.position.x) + 0.5f - movement.position.x;
    const float dy = static_cast<float>(modify.position.y) + 0.5f - movement.position.y;
    const float dz = static_cast<float>(modify.position.z) + 0.5f - movement.position.z;
    constexpr float kMaximumEditDistance = 6.0f;
    return dx * dx + dy * dy + dz * dz <= kMaximumEditDistance * kMaximumEditDistance;
}

template <typename ServerImpl>
void BroadcastEntityState(ServerImpl& impl, const EntityState& state) {
    const std::vector<std::uint8_t> payload = SerializeEntityState(state);
    for (const auto& [key, peer] : impl.peers) {
        (void)key;
        SendPacket(impl, peer.endpoint, PacketId::S2C_EntityState, payload);
    }
}

} // namespace

GameServer::GameServer() : m_impl(std::make_unique<Impl>()) {}

GameServer::~GameServer() { Stop(); }

bool GameServer::Start(std::string host, std::uint16_t port) {
    Stop();
    asio::error_code error;
    const auto address = asio::ip::make_address(host, error);
    if (error) {
        return false;
    }
    m_impl->socket.open(asio::ip::udp::v4(), error);
    if (error) {
        return false;
    }
    m_impl->socket.bind({address, port}, error);
    if (error) {
        m_impl->socket.close();
        return false;
    }
    m_impl->socket.non_blocking(true, error);
    m_impl->running = !error;
    return m_impl->running;
}

void GameServer::Stop() {
    if (m_impl->socket.is_open()) {
        asio::error_code error;
        m_impl->socket.close(error);
    }
    m_impl->peers.clear();
    m_impl->playerStates.clear();
    m_impl->running = false;
}

void GameServer::Tick() {
    if (!m_impl->running) {
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
        const auto now = std::chrono::steady_clock::now();
        const std::string key = PeerKey(sender);
        if (packet.header.id == PacketId::C2S_Connect) {
            const auto found = m_impl->peers.find(key);
            if (found == m_impl->peers.end()) {
                if (m_impl->peers.size() >= 8) continue;
                const std::uint32_t playerId = m_impl->nextPlayerId++;
                m_impl->peers.emplace(key, Impl::Peer{sender, now, playerId});
                m_impl->playerStates.emplace(playerId, EntityState{playerId, {}});
            } else {
                found->second.lastSeen = now;
            }
            const std::uint32_t playerId = m_impl->peers.at(key).playerId;
            SendPacket(*m_impl, sender, PacketId::S2C_ConnectAck,
                       SerializeEntityState(m_impl->playerStates.at(playerId)));
            for (const auto& [id, state] : m_impl->playerStates) {
                (void)id;
                SendPacket(*m_impl, sender, PacketId::S2C_EntityState, SerializeEntityState(state));
            }
        } else if (const auto peer = m_impl->peers.find(key); peer != m_impl->peers.end()) {
            peer->second.lastSeen = now;
            if (packet.header.id == PacketId::C2S_KeepAlive) {
                SendPacket(*m_impl, sender, PacketId::S2C_KeepAliveAck);
            } else if (packet.header.id == PacketId::C2S_PlayerMove) {
                PlayerMove requested;
                if (!DeserializePlayerMove(packet.payload, requested)) continue;
                EntityState& state = m_impl->playerStates.at(peer->second.playerId);
                if (!IsMovementAccepted(state.movement, requested)) continue;
                state.movement = requested;
                BroadcastEntityState(*m_impl, state);
            } else if (packet.header.id == PacketId::C2S_BlockModify) {
                BlockModify modify;
                if (!DeserializeBlockModify(packet.payload, modify) ||
                    !IsEditInReach(m_impl->playerStates.at(peer->second.playerId).movement, modify)) {
                    continue;
                }
                m_impl->world.SetBlock(modify.position, modify.blockId);
                const std::vector<std::uint8_t> payload = SerializeBlockModify(modify);
                for (const auto& [peerKey, connectedPeer] : m_impl->peers) {
                    (void)peerKey;
                    SendPacket(*m_impl, connectedPeer.endpoint, PacketId::S2C_BlockUpdate, payload);
                }
            } else if (packet.header.id == PacketId::C2S_Disconnect) {
                m_impl->playerStates.erase(peer->second.playerId);
                m_impl->peers.erase(peer);
            }
        }
    }
    const auto timeout = std::chrono::steady_clock::now() - std::chrono::seconds(30);
    std::erase_if(m_impl->peers, [this, timeout](const auto& pair) {
        if (pair.second.lastSeen >= timeout) return false;
        m_impl->playerStates.erase(pair.second.playerId);
        return true;
    });
}

bool GameServer::IsRunning() const noexcept { return m_impl->running; }

std::uint16_t GameServer::Port() const noexcept {
    asio::error_code error;
    return m_impl->socket.is_open() ? m_impl->socket.local_endpoint(error).port() : 0;
}

std::size_t GameServer::PeerCount() const noexcept { return m_impl->peers.size(); }

World& GameServer::GetWorld() noexcept { return m_impl->world; }

const World& GameServer::GetWorld() const noexcept { return m_impl->world; }

const std::unordered_map<std::uint32_t, EntityState>& GameServer::GetPlayerStates() const noexcept {
    return m_impl->playerStates;
}

} // namespace voxels::networking