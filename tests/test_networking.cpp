/**
 * @file test_networking.cpp
 * @brief Automated tests for Work Item 09 packet and loopback networking behavior.
 */

#include <catch2/catch_test_macros.hpp>

#include "voxels/networking/client.hpp"
#include "voxels/networking/packet.hpp"
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

    voxels::networking::BlockModify decoded;
    REQUIRE(voxels::networking::DeserializeBlockModify(packet.payload, decoded));
    REQUIRE(decoded.position == source.position);
    REQUIRE(decoded.blockId == source.blockId);
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

    const voxels::networking::BlockModify modify{
        {10, 64, -5}, static_cast<voxels::BlockId>(voxels::BlockType::Dirt)};
    client.SendBlockModify(modify);
    Service(server, client);

    REQUIRE(server.GetWorld().GetBlock(modify.position) == modify.blockId);
    REQUIRE(client.ReceivedBlockUpdates().size() == 1);
    REQUIRE(client.ReceivedBlockUpdates().front().position == modify.position);
    REQUIRE(client.ReceivedBlockUpdates().front().blockId == modify.blockId);
}