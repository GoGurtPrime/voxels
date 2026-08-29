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
    };

    asio::io_context ioContext;
    asio::ip::udp::socket socket{ioContext};
    World world;
    std::unordered_map<std::string, Peer> peers;
    std::uint32_t sequenceNumber = 0;
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
            m_impl->peers.insert_or_assign(key, Impl::Peer{sender, now});
            SendPacket(*m_impl, sender, PacketId::S2C_ConnectAck);
        } else if (const auto peer = m_impl->peers.find(key); peer != m_impl->peers.end()) {
            peer->second.lastSeen = now;
            if (packet.header.id == PacketId::C2S_KeepAlive) {
                SendPacket(*m_impl, sender, PacketId::S2C_KeepAliveAck);
            } else if (packet.header.id == PacketId::C2S_BlockModify) {
                BlockModify modify;
                if (!DeserializeBlockModify(packet.payload, modify)) {
                    continue;
                }
                m_impl->world.SetBlock(modify.position, modify.blockId);
                const std::vector<std::uint8_t> payload = SerializeBlockModify(modify);
                for (const auto& [peerKey, connectedPeer] : m_impl->peers) {
                    (void)peerKey;
                    SendPacket(*m_impl, connectedPeer.endpoint, PacketId::S2C_BlockUpdate, payload);
                }
            }
        }
    }
    const auto timeout = std::chrono::steady_clock::now() - std::chrono::seconds(30);
    std::erase_if(m_impl->peers, [timeout](const auto& pair) { return pair.second.lastSeen < timeout; });
}

bool GameServer::IsRunning() const noexcept { return m_impl->running; }

std::uint16_t GameServer::Port() const noexcept {
    asio::error_code error;
    return m_impl->socket.is_open() ? m_impl->socket.local_endpoint(error).port() : 0;
}

std::size_t GameServer::PeerCount() const noexcept { return m_impl->peers.size(); }

World& GameServer::GetWorld() noexcept { return m_impl->world; }

const World& GameServer::GetWorld() const noexcept { return m_impl->world; }

} // namespace voxels::networking