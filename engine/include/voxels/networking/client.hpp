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

class GameClient {
public:
    GameClient();
    ~GameClient();
    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    [[nodiscard]] bool Connect(std::string host, std::uint16_t port);
    void Disconnect();
    void Tick();
    void SendPlayerMove(const PlayerMove& movement);
    void SendBlockModify(const BlockModify& modify);

    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] bool HasReceivedConnectAck() const noexcept;
    [[nodiscard]] std::uint32_t PlayerId() const noexcept;
    [[nodiscard]] const std::unordered_map<std::uint32_t, EntityState>& ReceivedEntityStates() const noexcept;
    [[nodiscard]] const std::vector<BlockModify>& ReceivedBlockUpdates() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace voxels::networking