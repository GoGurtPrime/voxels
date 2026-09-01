#pragma once

/**
 * @file network_session.hpp
 * @brief Transport-agnostic session vocabulary: roles, endpoints, and connection lifecycle.
 *
 * @details Abstract seam meant to decouple the app from any single transport (loopback
 *          local-server play, LAN, future peer-to-peer). The shipping UDP stack lives in
 *          packet.hpp, server.hpp, and client.hpp instead: protocol v2 with versioned packet
 *          headers, ConnectAccept handing WorldInfo to joiners, and RLE chunk sections
 *          streamed as <=1100-byte fragments under a per-peer in-flight window with 350 ms
 *          retransmit until acked. No concrete INetworkSession implementation exists yet.
 */

#include <cstdint>
#include <string>

namespace voxels {

/// Role a session plays in a game topology; Peer and Spectator are reserved for future
/// topologies (the current stack is strictly client/server).
enum class NetworkRole {
    Server,
    Client,
    Peer,
    Spectator
};

/// Connectable address: host name or dotted IP plus UDP port.
struct NetworkEndpoint {
    std::string host;
    std::uint16_t port = 0;
};

/// Abstract connect/disconnect lifecycle for one session; an implementation binds a role to
/// a concrete transport.
class INetworkSession {
public:
    virtual ~INetworkSession() = default;
    virtual bool Connect(const NetworkEndpoint& endpoint) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    virtual NetworkRole GetRole() const = 0;
};

} // namespace voxels
