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
#include <unordered_map>
#include <vector>

#include "voxels/gameplay/item_drop.hpp"
#include "voxels/networking/packet.hpp"
#include "voxels/world/world.hpp"

namespace voxels::networking {

class GameServer {
public:
    static constexpr std::uint16_t kDefaultPort = 27015;
    static constexpr std::chrono::milliseconds kTickInterval{50};
    static constexpr std::size_t kMaxPlayers = 8;

    GameServer();
    ~GameServer();
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    [[nodiscard]] bool Start(std::string host = "127.0.0.1", std::uint16_t port = kDefaultPort);
    void Stop();
    /// Services networking and advances the world one fixed simulation tick when requested.
    void Tick(bool advanceWorldTime = true);

    /// Marks the authoritative world live for joiners and records the spawn handed to them.
    void SetWorldReady(const WorldOptions& options, const Vec3& spawn, WorldTick worldTick = kInitialWorldTick);
    /// Ends the hosted session: remote peers are disconnected and the world is dropped.
    void ClearWorld();
    /// Supplies the data-driven drop table used when remote players break blocks.
    void SetBlockRegistry(const BlockRegistry* registry) noexcept;
    [[nodiscard]] bool IsWorldReady() const noexcept;
    [[nodiscard]] WorldTick GetWorldTick() const noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] std::uint16_t Port() const noexcept;
    [[nodiscard]] std::size_t PeerCount() const noexcept;
    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;
    [[nodiscard]] gameplay::ItemDropSimulation& GetItemDrops() noexcept;
    [[nodiscard]] const gameplay::ItemDropSimulation& GetItemDrops() const noexcept;
    [[nodiscard]] const std::unordered_map<std::uint32_t, EntityState>& GetPlayerStates() const noexcept;
    /// Chunks generated server-side for remote peers since the last call; the in-process host
    /// drains these so its renderer learns about terrain it did not generate itself.
    [[nodiscard]] std::vector<ChunkCoordinate> TakeNewlyGeneratedChunks();
    /// Block edits applied on behalf of remote peers since the last call; the in-process host
    /// drains these to relight and remesh chunks the server mutated in the shared world.
    [[nodiscard]] std::vector<BlockModify> TakeRemoteBlockEdits();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace voxels::networking