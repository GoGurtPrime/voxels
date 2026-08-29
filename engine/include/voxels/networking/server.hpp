/**
 * @file server.hpp
 * @brief Server-authoritative UDP game server and peer management interface.
 *
 * @details GameServer owns networking peers and the authoritative World. Its tick method
 *          is deliberately explicit, allowing an application loop or headless host to run
 *          at the intended 20 Hz rate without coupling to rendering or audio subsystems.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "voxels/world/world.hpp"

namespace voxels::networking {

class GameServer {
public:
    static constexpr std::uint16_t kDefaultPort = 27015;
    static constexpr std::chrono::milliseconds kTickInterval{50};

    GameServer();
    ~GameServer();
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    [[nodiscard]] bool Start(std::string host = "127.0.0.1", std::uint16_t port = kDefaultPort);
    void Stop();
    void Tick();

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] std::uint16_t Port() const noexcept;
    [[nodiscard]] std::size_t PeerCount() const noexcept;
    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace voxels::networking