#pragma once

/*
 * Scope: Networking session abstraction for game and peer-to-peer runtime connectivity.
 *
 * The network layer is intentionally generic so it can support loopback local-server play,
 * LAN discovery, and future peer-to-peer communication without binding the app to a single
 * transport implementation.
 *
 * Relation to the rest of the codebase: the app and future multiplayer systems use this
 * contract to connect clients and coordinate shared world updates.
 */

#include <cstdint>
#include <string>

namespace voxels {

enum class NetworkRole {
    Server,
    Client,
    Peer,
    Spectator
};

struct NetworkEndpoint {
    std::string host;
    std::uint16_t port = 0;
};

class INetworkSession {
public:
    virtual ~INetworkSession() = default;
    virtual bool Connect(const NetworkEndpoint& endpoint) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    virtual NetworkRole GetRole() const = 0;
};

} // namespace voxels
