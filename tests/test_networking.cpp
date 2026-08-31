/**
 * @file test_networking.cpp
 * @brief Automated tests for Work Item 09 packet and loopback networking behavior.
 */

#include <catch2/catch_test_macros.hpp>

#include "voxels/networking/client.hpp"
#include "voxels/networking/packet.hpp"
#include "voxels/networking/reliable_channel.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/world/block.hpp"

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