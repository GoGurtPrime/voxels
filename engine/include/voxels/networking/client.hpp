/**
 * @file client.hpp
 * @brief UDP game client with handshake, prediction state, and interpolation storage.
 *
 * @details GameClient sends input-oriented packets to GameServer and retains the latest
 *          server-authoritative entity state for rendering interpolation. Socket servicing is
 *          explicit to keep the client usable by interactive and headless app lifecycles.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "voxels/networking/packet.hpp"

namespace voxels::networking {

/// A fully reassembled chunk payload received from the server, still RLE-encoded.
struct NetworkChunk {
    Vec3I coordinate{};
    std::vector<std::uint8_t> rleData;
};

class GameClient {
public:
    GameClient();
    ~GameClient();
    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    [[nodiscard]] bool Connect(std::string host, std::uint16_t port, ClientKind kind = ClientKind::Remote);
    void Disconnect();
    void Tick();
    void SendPlayerMove(const PlayerMove& movement);
    void SendBlockModify(const BlockModify& modify);

    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] bool HasReceivedConnectAck() const noexcept;
    [[nodiscard]] std::uint32_t PlayerId() const noexcept;
    [[nodiscard]] const std::unordered_map<std::uint32_t, EntityState>& ReceivedEntityStates() const noexcept;
    [[nodiscard]] const std::vector<BlockModify>& ReceivedBlockUpdates() const noexcept;
    /// Drains block updates so each authoritative edit is applied to the local world exactly once.
    [[nodiscard]] std::vector<BlockModify> TakeReceivedBlockUpdates();
    /// Drains chunks whose fragments have all arrived and been acknowledged.
    [[nodiscard]] std::vector<NetworkChunk> TakeCompletedChunks();
    /// Drains ids of players the server reported as departed; they are removed from the entity map.
    [[nodiscard]] std::vector<std::uint32_t> TakeDepartedPlayers();

    [[nodiscard]] bool IsWorldReadyOnServer() const noexcept;
    [[nodiscard]] const WorldInfo& GetWorldInfo() const noexcept;
    /// Latest server world time extrapolated with the local monotonic clock between corrections.
    [[nodiscard]] float GetEstimatedWorldTimeSeconds() const noexcept;
    [[nodiscard]] bool WasRejected() const noexcept;
    [[nodiscard]] RejectReason GetRejectReason() const noexcept;
    [[nodiscard]] bool WasDisconnectedByServer() const noexcept;
    [[nodiscard]] double SecondsSinceLastServerPacket() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace voxels::networking