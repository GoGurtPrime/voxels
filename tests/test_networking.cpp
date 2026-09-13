/**
 * @file test_networking.cpp
 * @brief Automated tests for Work Item 09 packet and loopback networking behavior.
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

#include "voxels/app/network_sync.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/packet.hpp"
#include "voxels/networking/reliable_channel.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/generation_pipeline.hpp"

namespace {

void Service(voxels::networking::GameServer& server, voxels::networking::GameClient& client) {
    server.Tick();
    client.Tick();
}

} // namespace

TEST_CASE("Packet.Serialization", "[networking][packet]") {
    const voxels::networking::BlockModify source{{10, 64, -5}, static_cast<voxels::BlockId>(voxels::BlockType::Coal)};
    const std::vector<std::uint8_t> payload = voxels::networking::SerializeBlockModify(source);
    const voxels::networking::Packet sourcePacket{
        {voxels::networking::PacketId::C2S_BlockModify, 42, 0}, payload};

    const std::vector<std::uint8_t> bytes = voxels::networking::SerializePacket(sourcePacket);
    voxels::networking::Packet packet;
    REQUIRE(voxels::networking::DeserializePacket(bytes, packet));
    REQUIRE(packet.header.id == voxels::networking::PacketId::C2S_BlockModify);
    REQUIRE(packet.header.sequenceNum == 42);
    REQUIRE(packet.header.payloadSize == payload.size());
    REQUIRE(packet.header.protocolVersion == voxels::networking::kProtocolVersion);

    voxels::networking::BlockModify decoded;
    REQUIRE(voxels::networking::DeserializeBlockModify(packet.payload, decoded));
    REQUIRE(decoded.position == source.position);
    REQUIRE(decoded.blockId == source.blockId);

    voxels::networking::InventoryState inventory;
    inventory.slots[0] = {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 17};
    const auto inventoryBytes = voxels::networking::SerializeInventoryState(inventory);
    voxels::networking::InventoryState decodedInventory;
    REQUIRE(voxels::networking::DeserializeInventoryState(inventoryBytes, decodedInventory));
    REQUIRE(decodedInventory.slots[0].blockId == inventory.slots[0].blockId);
    REQUIRE(decodedInventory.slots[0].count == 17);

    const voxels::networking::ItemPickup pickup{
        static_cast<voxels::BlockId>(voxels::BlockType::Coal), 3};
    const auto pickupBytes = voxels::networking::SerializeItemPickup(pickup);
    voxels::networking::ItemPickup decodedPickup;
    REQUIRE(voxels::networking::DeserializeItemPickup(pickupBytes, decodedPickup));
    REQUIRE(decodedPickup.blockId == pickup.blockId);
    REQUIRE(decodedPickup.count == pickup.count);
}

TEST_CASE("Packet.VersionMismatchAndOversizedPayloadAreRejected", "[networking][packet]") {
    voxels::networking::Packet mismatched{{voxels::networking::PacketId::C2S_KeepAlive, 1, 0, 99}, {}};
    REQUIRE(voxels::networking::SerializePacket(mismatched).empty());

    std::vector<std::uint8_t> bytes{0, 99, 0, 8, 0, 0, 0, 1, 0, 0};
    voxels::networking::Packet decoded;
    REQUIRE_FALSE(voxels::networking::DeserializePacket(bytes, decoded));

    voxels::networking::Packet oversized{{voxels::networking::PacketId::C2S_KeepAlive, 1, 0},
                                         std::vector<std::uint8_t>(voxels::networking::kMaximumPacketPayloadBytes + 1)};
    REQUIRE(voxels::networking::SerializePacket(oversized).empty());
}

TEST_CASE("Reliability.LostReliablePacketsAreRetransmittedAndDeliveredOnce", "[networking][reliability]") {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::time_point{};
    voxels::networking::ReliableChannel sender;
    voxels::networking::ReliableChannel receiver;
    const std::uint32_t sequence = sender.Queue({1, 2, 3}, start);

    const auto firstSend = sender.CollectDue(start, std::chrono::milliseconds{100});
    REQUIRE(firstSend.size() == 1);
    REQUIRE(firstSend.front().sequence == sequence);
    REQUIRE(sender.CollectDue(start + std::chrono::milliseconds{99}, std::chrono::milliseconds{100}).empty());

    const auto retry = sender.CollectDue(start + std::chrono::milliseconds{100}, std::chrono::milliseconds{100});
    REQUIRE(retry.size() == 1);
    REQUIRE(retry.front().attempts == 2);
    REQUIRE(receiver.Observe(retry.front().sequence));
    REQUIRE_FALSE(receiver.Observe(retry.front().sequence));
    sender.Acknowledge(receiver.LatestReceived(), receiver.ReceivedAcknowledgementBits());
    REQUIRE(sender.PendingCount() == 0);
}

TEST_CASE("Network.LoopbackConnection", "[networking]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    REQUIRE(server.Port() != 0);

    voxels::networking::GameClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port()));
    Service(server, client);

    REQUIRE(server.PeerCount() == 1);
    REQUIRE(client.HasReceivedConnectAck());
}

TEST_CASE("Network.BlockModificationSync", "[networking]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));

    voxels::networking::GameClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port()));
    Service(server, client);
    REQUIRE(client.HasReceivedConnectAck());

    const voxels::networking::PlayerMove movement{{1.5f, 0.5f, -0.5f}, {}, {}};
    client.SendPlayerMove(movement);
    Service(server, client);
    const voxels::networking::BlockModify modify{
        {1, 0, -1}, static_cast<voxels::BlockId>(voxels::BlockType::Dirt)};
    client.SendBlockModify(modify);
    Service(server, client);

    REQUIRE(server.GetWorld().GetBlock(modify.position) == modify.blockId);
    REQUIRE(client.ReceivedBlockUpdates().size() == 1);
    REQUIRE(client.ReceivedBlockUpdates().front().position == modify.position);
    REQUIRE(client.ReceivedBlockUpdates().front().blockId == modify.blockId);
}

TEST_CASE("Replication.TwoClientsObserveAuthoritativeMovement", "[networking][replication]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    voxels::networking::GameClient first;
    voxels::networking::GameClient second;
    REQUIRE(first.Connect("127.0.0.1", server.Port()));
    REQUIRE(second.Connect("127.0.0.1", server.Port()));
    server.Tick();
    first.Tick();
    second.Tick();
    REQUIRE(first.PlayerId() != 0);
    REQUIRE(second.PlayerId() != 0);
    REQUIRE(first.PlayerId() != second.PlayerId());

    const voxels::networking::PlayerMove movement{{0.5f, 0.5f, -0.5f}, {0.2f, 0.0f, 0.0f}, {}};
    first.SendPlayerMove(movement);
    server.Tick();
    first.Tick();
    second.Tick();

    REQUIRE(server.GetPlayerStates().at(first.PlayerId()).movement.position == movement.position);
    REQUIRE(second.ReceivedEntityStates().at(first.PlayerId()).movement.position == movement.position);
    REQUIRE(second.ReceivedEntityStates().at(first.PlayerId()).movement.rotation == movement.rotation);
}

TEST_CASE("Multiplayer.HeadlessClientsReplicateMovementAndBlockEdits", "[networking][integration]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));

    voxels::networking::GameClient firstClient;
    voxels::networking::GameClient secondClient;
    REQUIRE(firstClient.Connect("127.0.0.1", server.Port()));
    REQUIRE(secondClient.Connect("127.0.0.1", server.Port()));
    server.Tick();
    firstClient.Tick();
    secondClient.Tick();
    REQUIRE(firstClient.HasReceivedConnectAck());
    REQUIRE(secondClient.HasReceivedConnectAck());

    const voxels::networking::PlayerMove firstMovement{{0.8f, 0.8f, 0.8f}, {0.25f, 0.0f, 0.0f}, {}};
    const voxels::networking::PlayerMove secondMovement{{-1.0f, 1.0f, 0.0f}, {-0.25f, 0.0f, 0.0f}, {}};
    firstClient.SendPlayerMove(firstMovement);
    secondClient.SendPlayerMove(secondMovement);
    server.Tick();
    firstClient.Tick();
    secondClient.Tick();

    REQUIRE(server.GetPlayerStates().at(firstClient.PlayerId()).movement.position == firstMovement.position);
    REQUIRE(server.GetPlayerStates().at(secondClient.PlayerId()).movement.position == secondMovement.position);
    REQUIRE(secondClient.ReceivedEntityStates().at(firstClient.PlayerId()).movement.position == firstMovement.position);
    REQUIRE(firstClient.ReceivedEntityStates().at(secondClient.PlayerId()).movement.position == secondMovement.position);

    const voxels::networking::BlockModify edit{{1, 1, 1}, static_cast<voxels::BlockId>(voxels::BlockType::Planks)};
    firstClient.SendBlockModify(edit);
    server.Tick();
    firstClient.Tick();
    secondClient.Tick();

    REQUIRE(server.GetWorld().GetBlock(edit.position) == edit.blockId);
    REQUIRE_FALSE(firstClient.ReceivedBlockUpdates().empty());
    REQUIRE_FALSE(secondClient.ReceivedBlockUpdates().empty());
    REQUIRE(secondClient.ReceivedBlockUpdates().back().position == edit.position);
    REQUIRE(secondClient.ReceivedBlockUpdates().back().blockId == edit.blockId);
}

TEST_CASE("Protocol.ConnectAcceptRoundTripsWorldInfo", "[networking][packet]") {
    voxels::networking::ConnectAccept source{{7, {{1.5f, 40.0f, -3.5f}, {0.4f, -0.1f, 0.0f}, {}}, 9}, true, {}};
    source.world.seed = 0xDEADBEEFCAFEF00Dull;
    source.world.generatorVersion = 2;
    source.world.sandboxMode = true;
    source.world.alwaysSunny = true;
    source.world.spawnPosition = {8.5f, 41.9f, 8.5f};
    source.world.worldTick = 9876543210123ULL;

    const std::vector<std::uint8_t> bytes = voxels::networking::SerializeConnectAccept(source);
    voxels::networking::ConnectAccept decoded;
    REQUIRE(voxels::networking::DeserializeConnectAccept(bytes, decoded));
    REQUIRE(decoded.state.entityId == 7);
    REQUIRE(decoded.state.health == 9);
    REQUIRE(decoded.worldReady);
    REQUIRE(decoded.world.seed == source.world.seed);
    REQUIRE(decoded.world.generatorVersion == 2);
    REQUIRE(decoded.world.sandboxMode);
    REQUIRE_FALSE(decoded.world.peaceful);
    REQUIRE(decoded.world.spawnPosition == source.world.spawnPosition);
    REQUIRE(decoded.world.worldTick == source.world.worldTick);

    const voxels::networking::ConnectAccept notReady{{3, {}}, false, {}};
    const std::vector<std::uint8_t> notReadyBytes = voxels::networking::SerializeConnectAccept(notReady);
    voxels::networking::ConnectAccept decodedNotReady;
    REQUIRE(voxels::networking::DeserializeConnectAccept(notReadyBytes, decodedNotReady));
    REQUIRE_FALSE(decodedNotReady.worldReady);

    // Truncated and trailing-garbage payloads must be rejected, never read out of bounds.
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        voxels::networking::ConnectAccept scratch;
        REQUIRE_FALSE(voxels::networking::DeserializeConnectAccept(
            std::span<const std::uint8_t>(bytes.data(), length), scratch));
    }
    std::vector<std::uint8_t> oversized = bytes;
    oversized.push_back(0);
    voxels::networking::ConnectAccept scratch;
    REQUIRE_FALSE(voxels::networking::DeserializeConnectAccept(oversized, scratch));

    voxels::WorldTick decodedTime = 0;
    REQUIRE(voxels::networking::DeserializeWorldTime(
        voxels::networking::SerializeWorldTime(9876543210123ULL), decodedTime));
    REQUIRE(decodedTime == 9876543210123ULL);
}

TEST_CASE("WorldClock.ServerAdvancesExactTicksAndHonorsLocalPause", "[networking][world_clock]") {
    voxels::networking::GameServer server;
    voxels::WorldOptions options{};
    options.alwaysSunny = false;
    constexpr voxels::WorldTick savedTick = 4ULL * voxels::kWorldTicksPerDay + 12345ULL;

    REQUIRE(server.Start("127.0.0.1", 0));
    server.SetWorldReady(options, {}, savedTick);
    server.Tick();
    server.Tick();
    REQUIRE(server.GetWorldTick() == savedTick + 2ULL);

    server.Tick(false);
    REQUIRE(server.GetWorldTick() == savedTick + 2ULL);
    REQUIRE(voxels::WorldDayIndex(server.GetWorldTick()) == 4ULL);
    REQUIRE(voxels::WorldDayFraction(server.GetWorldTick()) > 0.0f);
    server.Stop();
}

TEST_CASE("WorldClock.LoopbackReplicationPreservesExactSavedTick", "[networking][world_clock]") {
    voxels::networking::GameServer server;
    voxels::networking::GameClient client;
    voxels::WorldOptions options{};
    options.alwaysSunny = false;
    constexpr voxels::WorldTick savedTick = 9876543210123ULL;

    REQUIRE(server.Start("127.0.0.1", 0));
    REQUIRE(client.Connect("127.0.0.1", server.Port(), voxels::networking::ClientKind::InProcessHost));
    Service(server, client);
    server.SetWorldReady(options, {}, savedTick);
    client.Tick();
    REQUIRE(client.GetWorldTick() == savedTick);

    for (int tick = 0; tick < 20; ++tick) Service(server, client);
    REQUIRE(server.GetWorldTick() == savedTick + 20ULL);
    REQUIRE(client.GetWorldTick() == savedTick + 20ULL);
    client.Disconnect();
    server.Stop();
}

TEST_CASE("WorldClock.DayDerivationDependsOnlyOnTickSequence", "[networking][world_clock]") {
    constexpr voxels::WorldTick startTick = 17ULL * voxels::kWorldTicksPerDay + 34567ULL;
    voxels::WorldTick steadyRenderTick = startTick;
    voxels::WorldTick unevenRenderTick = startTick;

    for (int frame = 0; frame < 120; ++frame) ++steadyRenderTick;
    for (int frame = 0; frame < 40; ++frame) {
        unevenRenderTick += 1ULL;
        unevenRenderTick += 2ULL;
    }

    REQUIRE(steadyRenderTick == unevenRenderTick);
    REQUIRE(voxels::WorldDayIndex(steadyRenderTick) == voxels::WorldDayIndex(unevenRenderTick));
    REQUIRE(voxels::WorldDayFraction(steadyRenderTick) == voxels::WorldDayFraction(unevenRenderTick));
}

TEST_CASE("Fragmentation.LargeChunkPayloadReassemblesExactly", "[networking][packet]") {
    std::vector<std::uint8_t> payload(5000);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<std::uint8_t>(index * 31u);
    }
    const auto fragments = voxels::networking::FragmentChunkPayload({4, 2, -9}, payload);
    REQUIRE(fragments.size() == 5);
    std::vector<std::uint8_t> reassembled;
    for (const voxels::networking::ChunkFragment& fragment : fragments) {
        REQUIRE(fragment.chunkCoordinate == voxels::Vec3I{4, 2, -9});
        REQUIRE(fragment.fragmentCount == 5);
        REQUIRE(fragment.totalBytes == payload.size());
        const std::vector<std::uint8_t> wire = voxels::networking::SerializeChunkFragment(fragment);
        REQUIRE_FALSE(wire.empty());
        REQUIRE(wire.size() <= voxels::networking::kMaximumPacketPayloadBytes);
        voxels::networking::ChunkFragment decoded;
        REQUIRE(voxels::networking::DeserializeChunkFragment(wire, decoded));
        reassembled.insert(reassembled.end(), decoded.data.begin(), decoded.data.end());
    }
    REQUIRE(reassembled == payload);
}

TEST_CASE("Protocol.MalformedChunkFragmentsAreRejectedSafely", "[networking][packet]") {
    voxels::networking::ChunkFragment fragment;
    // Too short for the header.
    REQUIRE_FALSE(voxels::networking::DeserializeChunkFragment(std::vector<std::uint8_t>(10), fragment));
    // Header only, no data bytes.
    const auto valid = voxels::networking::FragmentChunkPayload({0, 0, 0}, std::vector<std::uint8_t>(64, 7));
    REQUIRE(valid.size() == 1);
    std::vector<std::uint8_t> wire = voxels::networking::SerializeChunkFragment(valid.front());
    REQUIRE(voxels::networking::DeserializeChunkFragment(wire, fragment));
    wire.resize(24);
    REQUIRE_FALSE(voxels::networking::DeserializeChunkFragment(wire, fragment));
    // fragmentIndex >= fragmentCount.
    voxels::networking::ChunkFragment inverted = valid.front();
    inverted.fragmentIndex = 1;
    REQUIRE(voxels::networking::SerializeChunkFragment(inverted).size() > 0);
    const std::vector<std::uint8_t> invertedWire = voxels::networking::SerializeChunkFragment(inverted);
    REQUIRE_FALSE(voxels::networking::DeserializeChunkFragment(invertedWire, fragment));
    // totalBytes over the transfer bound refuses to serialize at all.
    voxels::networking::ChunkFragment oversized = valid.front();
    oversized.totalBytes = voxels::networking::kMaximumChunkTransferBytes + 1;
    REQUIRE(voxels::networking::SerializeChunkFragment(oversized).empty());
}

TEST_CASE("Streaming.RemoteClientReceivesHostWorldAndAppliesIt", "[networking][integration]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));

    // Host a tiny 1x1-column world with a marker block.
    voxels::WorldOptions options{};
    options.seed = 1234;
    voxels::World& hostWorld = server.GetWorld();
    hostWorld.Initialize(options);
    voxels::WorldGenerator generator(options);
    for (int y = 0; y < 8; ++y) {
        hostWorld.GetOrCreateChunk({0, y, 0}) = generator.GenerateChunk({0, y, 0});
    }
    hostWorld.GetOrCreateChunk({0, 8, 0});
    const voxels::Vec3I marker{3, 100, 3};
    REQUIRE(hostWorld.SetBlock(marker, static_cast<voxels::BlockId>(voxels::BlockType::Planks)));
    server.SetWorldReady(options, {8.0f, 60.0f, 8.0f});

    voxels::networking::GameClient joiner;
    REQUIRE(joiner.Connect("127.0.0.1", server.Port(), voxels::networking::ClientKind::Remote));
    voxels::World joinerWorld;
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    voxels::RemoteChunkApplier applier;
    for (int tick = 0; tick < 50 && !applier.IsColumnComplete(0, 0); ++tick) {
        server.Tick();
        joiner.Tick();
        (void)applier.Apply(joiner, joinerWorld, registry);
    }
    REQUIRE(joiner.HasReceivedConnectAck());
    REQUIRE(joiner.IsWorldReadyOnServer());
    REQUIRE(joiner.GetWorldInfo().seed == 1234);
    REQUIRE(applier.IsColumnComplete(0, 0));
    // The requested column arrived; the server may also stream neighbours it generated
    // around the joiner's position, so the count is a lower bound.
    REQUIRE(applier.AppliedChunkCount() >= 9);

    // The streamed column matches the authoritative world block-for-block, including the edit.
    REQUIRE(joinerWorld.GetBlock(marker) == static_cast<voxels::BlockId>(voxels::BlockType::Planks));
    for (int y = 0; y < 128; y += 3) {
        for (int x = 0; x < 16; x += 5) {
            for (int z = 0; z < 16; z += 5) {
                REQUIRE(joinerWorld.GetBlock({x, y, z}) == hostWorld.GetBlock({x, y, z}));
            }
        }
    }
    // Skylight was rebuilt above the surface so streamed terrain is not rendered black.
    const auto topSection = joinerWorld.GetChunks().find(voxels::ChunkCoordinate{0, 7, 0});
    REQUIRE(topSection != joinerWorld.GetChunks().end());
    REQUIRE(topSection->second->GetSkyLight(0, 15, 0) == 15);
}

TEST_CASE("Connection.NinthClientIsRejectedAsServerFull", "[networking]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    std::array<voxels::networking::GameClient, 8> clients;
    for (voxels::networking::GameClient& client : clients) {
        REQUIRE(client.Connect("127.0.0.1", server.Port()));
        server.Tick();
        client.Tick();
        REQUIRE(client.HasReceivedConnectAck());
    }
    REQUIRE(server.PeerCount() == voxels::networking::GameServer::kMaxPlayers);
    voxels::networking::GameClient ninth;
    REQUIRE(ninth.Connect("127.0.0.1", server.Port()));
    server.Tick();
    ninth.Tick();
    REQUIRE_FALSE(ninth.HasReceivedConnectAck());
    REQUIRE(ninth.WasRejected());
    REQUIRE(ninth.GetRejectReason() == voxels::networking::RejectReason::ServerFull);
}

TEST_CASE("Connection.ClosingHostedWorldDisconnectsRemotePeers", "[networking]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    server.SetWorldReady({}, {0.0f, 2.0f, 0.0f});

    voxels::networking::GameClient host;
    voxels::networking::GameClient remote;
    REQUIRE(host.Connect("127.0.0.1", server.Port(), voxels::networking::ClientKind::InProcessHost));
    REQUIRE(remote.Connect("127.0.0.1", server.Port(), voxels::networking::ClientKind::Remote));
    server.Tick();
    host.Tick();
    remote.Tick();
    REQUIRE(server.PeerCount() == 2);

    server.ClearWorld();
    server.Tick();
    host.Tick();
    remote.Tick();
    REQUIRE(remote.WasDisconnectedByServer());
    REQUIRE_FALSE(host.WasDisconnectedByServer());
    REQUIRE(server.PeerCount() == 1);
    // The remaining host learned the remote player left.
    REQUIRE_FALSE(host.TakeDepartedPlayers().empty());
}

TEST_CASE("ItemDrop.RemoteBreakSpawnsOnceAndHostedWorldTeardownClearsIt",
          "[networking][item_drop]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    server.SetBlockRegistry(&registry);
    const voxels::Vec3I target{0, 1, 0};
    REQUIRE(server.GetWorld().SetBlock(target,
        static_cast<voxels::BlockId>(voxels::BlockType::Stone)));
    server.SetWorldReady({}, {0.5f, 1.9f, 0.5f});

    voxels::networking::GameClient remote;
    REQUIRE(remote.Connect("127.0.0.1", server.Port(), voxels::networking::ClientKind::Remote));
    server.Tick();
    remote.Tick();
    REQUIRE(remote.HasReceivedConnectAck());

    remote.SendBlockModify({target, static_cast<voxels::BlockId>(voxels::BlockType::Air)});
    server.Tick();
    REQUIRE(server.GetItemDrops().Drops().size() == 1);
    const std::uint32_t authoritativeDropId = server.GetItemDrops().Drops().front().id;

    remote.SendBlockModify({target, static_cast<voxels::BlockId>(voxels::BlockType::Air)});
    server.Tick();
    REQUIRE(server.GetItemDrops().Drops().size() == 1);
    REQUIRE(server.GetItemDrops().Drops().front().id == authoritativeDropId);

    server.ClearWorld();
    REQUIRE(server.GetItemDrops().Drops().empty());
}

TEST_CASE("ItemDrop.ServerPickupWaitsForCapacityAndGrantsClientOnce",
          "[networking][item_drop][inventory]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    server.SetBlockRegistry(&registry);
    server.SetWorldReady({}, {0.5f, 1.18f, 0.5f});
    server.GetWorld().SetBlock({0, 0, 0}, static_cast<voxels::BlockId>(voxels::BlockType::Stone));

    voxels::networking::GameClient client;
    REQUIRE(client.Connect("127.0.0.1", server.Port(), voxels::networking::ClientKind::Remote));
    Service(server, client);
    REQUIRE(client.HasReceivedConnectAck());

    voxels::networking::InventoryState fullInventory;
    for (auto& slot : fullInventory.slots) {
        slot = {static_cast<voxels::BlockId>(voxels::BlockType::Stone), 64};
    }
    client.SendInventoryState(fullInventory);
    Service(server, client);
    const std::uint32_t dropId = server.GetItemDrops().Spawn(
        {static_cast<voxels::BlockId>(voxels::BlockType::Dirt), 5}, {0.5f, 1.18f, 0.5f}, {});

    Service(server, client);
    REQUIRE(server.GetItemDrops().Drops().size() == 1);
    REQUIRE(server.GetItemDrops().Drops().front().id == dropId);
    REQUIRE(client.TakeReceivedItemPickups().empty());

    fullInventory.slots[0] = {};
    client.SendInventoryState(fullInventory);
    Service(server, client);
    const auto pickups = client.TakeReceivedItemPickups();
    REQUIRE(pickups.size() == 1);
    REQUIRE(pickups.front().blockId == static_cast<voxels::BlockId>(voxels::BlockType::Dirt));
    REQUIRE(pickups.front().count == 5);
    REQUIRE(server.GetItemDrops().Drops().empty());

    Service(server, client);
    REQUIRE(client.TakeReceivedItemPickups().empty());
}

TEST_CASE("Connection.DisconnectBroadcastsPlayerLeft", "[networking]") {
    voxels::networking::GameServer server;
    REQUIRE(server.Start("127.0.0.1", 0));
    voxels::networking::GameClient stayer;
    voxels::networking::GameClient leaver;
    REQUIRE(stayer.Connect("127.0.0.1", server.Port()));
    REQUIRE(leaver.Connect("127.0.0.1", server.Port()));
    server.Tick();
    stayer.Tick();
    leaver.Tick();
    const std::uint32_t leaverId = leaver.PlayerId();
    REQUIRE(leaverId != 0);
    REQUIRE(stayer.ReceivedEntityStates().contains(leaverId));

    leaver.Disconnect();
    server.Tick();
    stayer.Tick();
    const std::vector<std::uint32_t> departed = stayer.TakeDepartedPlayers();
    REQUIRE_FALSE(departed.empty());
    REQUIRE(departed.front() == leaverId);
    REQUIRE_FALSE(stayer.ReceivedEntityStates().contains(leaverId));
}