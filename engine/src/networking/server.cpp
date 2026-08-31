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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "voxels/networking/packet.hpp"
#include "voxels/world/generation_pipeline.hpp"

namespace voxels::networking {

class GameServer::Impl {
public:
    struct InFlightChunk {
        std::vector<std::vector<std::uint8_t>> fragmentPayloads;
        std::chrono::steady_clock::time_point lastSent;
    };

    struct Peer {
        asio::ip::udp::endpoint endpoint;
        std::chrono::steady_clock::time_point lastSeen;
        std::uint32_t playerId = 0;
        bool inProcess = false;
        bool hasMoved = false;
        int rejectStreak = 0;
        std::unordered_set<ChunkCoordinate, ChunkCoordinateHash> syncedChunks;
        std::unordered_map<ChunkCoordinate, InFlightChunk, ChunkCoordinateHash> inFlightChunks;
    };

    asio::io_context ioContext;
    asio::ip::udp::socket socket{ioContext};
    World world;
    WorldOptions worldOptions{};
    Vec3 worldSpawn{};
    bool worldReady = false;
    std::unordered_map<std::string, Peer> peers;
    std::unordered_map<std::uint32_t, EntityState> playerStates;
    std::vector<ChunkCoordinate> newlyGeneratedChunks;
    std::vector<BlockModify> remoteBlockEdits;
    std::uint32_t sequenceNumber = 0;
    std::uint32_t nextPlayerId = 1;
    bool running = false;
};

namespace {

/// Terrain occupies sections 0..7 with an all-air cap section above (ADR-009 world column).
constexpr int kTerrainSectionCount = 8;
constexpr std::size_t kMaxInFlightChunksPerPeer = 4;
constexpr std::chrono::milliseconds kChunkResendInterval{350};
constexpr std::chrono::seconds kPeerTimeout{10};
/// Consecutive rejected moves after which the server re-seeds a peer's position (respawn/teleport).
constexpr int kMovementRejectResyncThreshold = 30;

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

std::vector<std::uint8_t> SerializePlayerId(std::uint32_t playerId) {
    return {static_cast<std::uint8_t>(playerId >> 24), static_cast<std::uint8_t>(playerId >> 16),
            static_cast<std::uint8_t>(playerId >> 8), static_cast<std::uint8_t>(playerId)};
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

template <typename ServerImpl>
void BroadcastPlayerLeft(ServerImpl& impl, std::uint32_t playerId) {
    for (const auto& [key, peer] : impl.peers) {
        (void)key;
        SendPacket(impl, peer.endpoint, PacketId::S2C_PlayerLeft, SerializePlayerId(playerId));
    }
}

template <typename ServerImpl>
ConnectAccept MakeConnectAccept(const ServerImpl& impl, const EntityState& state) {
    ConnectAccept accept{state, impl.worldReady, {}};
    if (impl.worldReady) {
        accept.world.seed = impl.worldOptions.seed;
        accept.world.generatorVersion = impl.worldOptions.generatorVersion;
        accept.world.sandboxMode = impl.worldOptions.sandboxMode;
        accept.world.peaceful = impl.worldOptions.peaceful;
        accept.world.alwaysSunny = impl.worldOptions.alwaysSunny;
        accept.world.permadeath = impl.worldOptions.permadeath;
        accept.world.spawnPosition = impl.worldSpawn;
    }
    return accept;
}

int ColumnDistanceSquared(const ChunkCoordinate& coordinate, int columnX, int columnZ) {
    const int dx = coordinate.x - columnX;
    const int dz = coordinate.z - columnZ;
    return dx * dx + dz * dz;
}

/// Generates at most one missing terrain column near a remote peer per tick so joining players
/// can keep walking outward; generated coordinates are surfaced to the in-process host.
template <typename ServerImpl>
void GenerateColumnsAroundRemotePeers(ServerImpl& impl) {
    const int radius = std::clamp(impl.worldOptions.renderDistanceChunks, 2, 8);
    for (const auto& [key, peer] : impl.peers) {
        (void)key;
        if (peer.inProcess) continue;
        const auto state = impl.playerStates.find(peer.playerId);
        if (state == impl.playerStates.end()) continue;
        const int columnX = static_cast<int>(std::floor(state->second.movement.position.x / 16.0f));
        const int columnZ = static_cast<int>(std::floor(state->second.movement.position.z / 16.0f));
        for (int ring = 0; ring <= radius; ++ring) {
            for (int z = -ring; z <= ring; ++z) {
                for (int x = -ring; x <= ring; ++x) {
                    if (std::max(std::abs(x), std::abs(z)) != ring) continue;
                    const ChunkCoordinate base{columnX + x, 0, columnZ + z};
                    if (impl.world.HasChunk(base)) continue;
                    WorldGenerator generator(impl.worldOptions);
                    for (int y = 0; y < kTerrainSectionCount; ++y) {
                        const ChunkCoordinate coordinate{base.x, y, base.z};
                        Chunk generated = generator.GenerateChunk(coordinate);
                        generated.ClearDirty();
                        impl.world.GetOrCreateChunk(coordinate) = std::move(generated);
                        impl.newlyGeneratedChunks.push_back(coordinate);
                    }
                    const ChunkCoordinate cap{base.x, kTerrainSectionCount, base.z};
                    impl.world.GetOrCreateChunk(cap).ClearDirty();
                    impl.newlyGeneratedChunks.push_back(cap);
                    return;
                }
            }
        }
    }
}

/// Streams world chunks to remote peers nearest-first with per-peer in-flight and resend
/// bookkeeping; delivery is confirmed by C2S_ChunkAck so a lost datagram is retransmitted.
template <typename ServerImpl>
void StreamChunksToPeer(ServerImpl& impl, typename ServerImpl::Peer& peer,
                        std::chrono::steady_clock::time_point now) {
    for (auto& [coordinate, inFlight] : peer.inFlightChunks) {
        if (now - inFlight.lastSent < kChunkResendInterval) continue;
        for (const std::vector<std::uint8_t>& payload : inFlight.fragmentPayloads) {
            SendPacket(impl, peer.endpoint, PacketId::S2C_ChunkData, payload);
        }
        inFlight.lastSent = now;
    }
    const auto state = impl.playerStates.find(peer.playerId);
    const Vec3 position = state != impl.playerStates.end() ? state->second.movement.position : impl.worldSpawn;
    const int columnX = static_cast<int>(std::floor(position.x / 16.0f));
    const int columnZ = static_cast<int>(std::floor(position.z / 16.0f));
    while (peer.inFlightChunks.size() < kMaxInFlightChunksPerPeer) {
        const Chunk* nearest = nullptr;
        long long nearestScore = std::numeric_limits<long long>::max();
        for (const auto& [coordinate, chunk] : impl.world.GetChunks()) {
            if (peer.syncedChunks.contains(coordinate) || peer.inFlightChunks.contains(coordinate)) continue;
            const long long score =
                static_cast<long long>(ColumnDistanceSquared(coordinate, columnX, columnZ)) * 32 + coordinate.y;
            if (score < nearestScore) {
                nearestScore = score;
                nearest = chunk.get();
            }
        }
        if (nearest == nullptr) break;
        const std::vector<std::uint8_t> rle = nearest->SerializeRLE();
        const std::vector<ChunkFragment> fragments =
            FragmentChunkPayload({nearest->GetCoordinate().x, nearest->GetCoordinate().y, nearest->GetCoordinate().z}, rle);
        if (fragments.empty()) {
            peer.syncedChunks.insert(nearest->GetCoordinate());
            continue;
        }
        typename ServerImpl::InFlightChunk inFlight{{}, now};
        inFlight.fragmentPayloads.reserve(fragments.size());
        for (const ChunkFragment& fragment : fragments) {
            std::vector<std::uint8_t> payload = SerializeChunkFragment(fragment);
            SendPacket(impl, peer.endpoint, PacketId::S2C_ChunkData, payload);
            inFlight.fragmentPayloads.push_back(std::move(payload));
        }
        peer.inFlightChunks.emplace(nearest->GetCoordinate(), std::move(inFlight));
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
        for (const auto& [key, peer] : m_impl->peers) {
            (void)key;
            SendPacket(*m_impl, peer.endpoint, PacketId::S2C_Disconnect,
                       {static_cast<std::uint8_t>(DisconnectReason::ServerShutdown)});
        }
        asio::error_code error;
        m_impl->socket.close(error);
    }
    m_impl->peers.clear();
    m_impl->playerStates.clear();
    m_impl->newlyGeneratedChunks.clear();
    m_impl->remoteBlockEdits.clear();
    m_impl->worldReady = false;
    m_impl->running = false;
}

void GameServer::SetWorldReady(const WorldOptions& options, const Vec3& spawn) {
    m_impl->worldOptions = options;
    m_impl->worldSpawn = spawn;
    m_impl->worldReady = true;
    for (auto& [key, peer] : m_impl->peers) {
        (void)key;
        peer.syncedChunks.clear();
        peer.inFlightChunks.clear();
        auto state = m_impl->playerStates.find(peer.playerId);
        if (state != m_impl->playerStates.end() && !peer.hasMoved) {
            state->second.movement.position = spawn;
        }
        SendPacket(*m_impl, peer.endpoint, PacketId::S2C_ConnectAck,
                   SerializeConnectAccept(MakeConnectAccept(*m_impl, m_impl->playerStates.at(peer.playerId))));
    }
}

void GameServer::ClearWorld() {
    std::vector<std::uint32_t> departedIds;
    for (auto peer = m_impl->peers.begin(); peer != m_impl->peers.end();) {
        if (peer->second.inProcess) {
            peer->second.syncedChunks.clear();
            peer->second.inFlightChunks.clear();
            ++peer;
            continue;
        }
        SendPacket(*m_impl, peer->second.endpoint, PacketId::S2C_Disconnect,
                   {static_cast<std::uint8_t>(DisconnectReason::HostClosedWorld)});
        departedIds.push_back(peer->second.playerId);
        m_impl->playerStates.erase(peer->second.playerId);
        peer = m_impl->peers.erase(peer);
    }
    for (const std::uint32_t playerId : departedIds) {
        BroadcastPlayerLeft(*m_impl, playerId);
    }
    m_impl->world.Clear();
    m_impl->newlyGeneratedChunks.clear();
    m_impl->worldReady = false;
}

bool GameServer::IsWorldReady() const noexcept { return m_impl->worldReady; }

std::vector<ChunkCoordinate> GameServer::TakeNewlyGeneratedChunks() {
    std::vector<ChunkCoordinate> chunks;
    chunks.swap(m_impl->newlyGeneratedChunks);
    return chunks;
}

std::vector<BlockModify> GameServer::TakeRemoteBlockEdits() {
    std::vector<BlockModify> edits;
    edits.swap(m_impl->remoteBlockEdits);
    return edits;
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
            if (packet.payload.size() > 1 || (packet.payload.size() == 1 && packet.payload[0] > 1)) continue;
            const bool inProcess = packet.payload.size() == 1 &&
                                   packet.payload[0] == static_cast<std::uint8_t>(ClientKind::InProcessHost);
            const auto found = m_impl->peers.find(key);
            if (found == m_impl->peers.end()) {
                if (m_impl->peers.size() >= kMaxPlayers) {
                    SendPacket(*m_impl, sender, PacketId::S2C_Reject,
                               {static_cast<std::uint8_t>(RejectReason::ServerFull)});
                    continue;
                }
                if (!inProcess && !sender.address().is_loopback()) {
                    if (!m_impl->worldReady) {
                        SendPacket(*m_impl, sender, PacketId::S2C_Reject,
                                   {static_cast<std::uint8_t>(RejectReason::WorldNotReady)});
                        continue;
                    }
                    if (!m_impl->worldOptions.isPublic) {
                        SendPacket(*m_impl, sender, PacketId::S2C_Reject,
                                   {static_cast<std::uint8_t>(RejectReason::WorldPrivate)});
                        continue;
                    }
                }
                const std::uint32_t playerId = m_impl->nextPlayerId++;
                Impl::Peer peer{sender, now, playerId, inProcess, false, 0, {}, {}};
                m_impl->peers.emplace(key, std::move(peer));
                EntityState initialState{playerId, {}};
                if (m_impl->worldReady) initialState.movement.position = m_impl->worldSpawn;
                m_impl->playerStates.emplace(playerId, initialState);
                BroadcastEntityState(*m_impl, initialState);
            } else {
                found->second.lastSeen = now;
            }
            const std::uint32_t playerId = m_impl->peers.at(key).playerId;
            SendPacket(*m_impl, sender, PacketId::S2C_ConnectAck,
                       SerializeConnectAccept(MakeConnectAccept(*m_impl, m_impl->playerStates.at(playerId))));
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
                const bool accepted = !peer->second.hasMoved || IsMovementAccepted(state.movement, requested) ||
                                      peer->second.rejectStreak >= kMovementRejectResyncThreshold;
                if (!accepted) {
                    ++peer->second.rejectStreak;
                    continue;
                }
                peer->second.hasMoved = true;
                peer->second.rejectStreak = 0;
                state.movement = requested;
                BroadcastEntityState(*m_impl, state);
            } else if (packet.header.id == PacketId::C2S_BlockModify) {
                BlockModify modify;
                if (!DeserializeBlockModify(packet.payload, modify) ||
                    !IsEditInReach(m_impl->playerStates.at(peer->second.playerId).movement, modify)) {
                    continue;
                }
                m_impl->world.SetBlock(modify.position, modify.blockId);
                if (!peer->second.inProcess) m_impl->remoteBlockEdits.push_back(modify);
                const std::vector<std::uint8_t> payload = SerializeBlockModify(modify);
                for (const auto& [peerKey, connectedPeer] : m_impl->peers) {
                    (void)peerKey;
                    SendPacket(*m_impl, connectedPeer.endpoint, PacketId::S2C_BlockUpdate, payload);
                }
            } else if (packet.header.id == PacketId::C2S_ChunkAck) {
                Vec3I coordinate;
                if (!DeserializeVec3I(packet.payload, coordinate)) continue;
                const ChunkCoordinate chunkCoordinate{coordinate.x, coordinate.y, coordinate.z};
                if (peer->second.inFlightChunks.erase(chunkCoordinate) > 0) {
                    peer->second.syncedChunks.insert(chunkCoordinate);
                }
            } else if (packet.header.id == PacketId::C2S_Disconnect) {
                const std::uint32_t departedId = peer->second.playerId;
                m_impl->playerStates.erase(departedId);
                m_impl->peers.erase(peer);
                BroadcastPlayerLeft(*m_impl, departedId);
            }
        }
    }
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = now - kPeerTimeout;
    std::vector<std::uint32_t> timedOutIds;
    std::erase_if(m_impl->peers, [this, timeout, &timedOutIds](const auto& pair) {
        if (pair.second.lastSeen >= timeout) return false;
        timedOutIds.push_back(pair.second.playerId);
        m_impl->playerStates.erase(pair.second.playerId);
        return true;
    });
    for (const std::uint32_t playerId : timedOutIds) {
        BroadcastPlayerLeft(*m_impl, playerId);
    }
    if (m_impl->worldReady) {
        GenerateColumnsAroundRemotePeers(*m_impl);
        for (auto& [key, peer] : m_impl->peers) {
            (void)key;
            if (!peer.inProcess) StreamChunksToPeer(*m_impl, peer, now);
        }
    }
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