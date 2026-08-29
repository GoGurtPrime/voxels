# Work Item 10: Networking & Client/Server Architecture

## 🎯 Objective & Overview
Integrate Standalone Asio / modern C++ socket networking to implement a client/server network architecture. The game application must seamlessly support running a local internal server listening on loopback (`127.0.0.1`) when playing singleplayer, which seamlessly enables peer-to-peer (P2P) hosting or dedicated server hosting without refactoring gameplay code.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `02_core_engine_foundation`, `06_world_voxel_core`, `09_app_lifecycle_ui_and_menus`
* **Target Subsystems:** `engine/include/voxels/networking/`, `engine/src/networking/`

---

## 📋 Detailed Task Breakdown
1. **Network Packet Protocol (`voxels/networking/packet.hpp`):**
   * Binary packet header: `PacketId (uint16_t)`, `SequenceNum (uint32_t)`, `PayloadSize (uint16_t)`.
   * Packet definitions:
     * `C2S_Connect`, `S2C_ConnectAck`
     * `C2S_PlayerMove` (Position, rotation, velocity)
     * `S2C_EntityState` (Server-authoritative position sync)
     * `C2S_BlockModify` (Voxel coordinate, new block ID)
     * `S2C_BlockUpdate` (Broadcast block change to clients)
     * `S2C_ChunkData` (Compressed chunk data payload)
     * `C2S_KeepAlive`, `S2C_KeepAliveAck`

2. **Server Architecture (`voxels/networking/server.hpp`):**
   * `GameServer` class:
     * Listens on configurable UDP/TCP port (default `27015`).
     * Manages client connections (`Peer` list, timeout management).
     * Server-authoritative tick loop (20 Hz tick rate).
     * World simulation ownership & chunk distribution queue.

3. **Client Network Engine (`voxels/networking/client.hpp`):**
   * `GameClient` class:
     * Connects to remote IP or local loopback (`127.0.0.1`).
     * Client-side prediction & interpolation buffer for smooth local player movement and remote player rendering.

4. **Integrated Loopback & `--server` Flag:**
   * When starting singleplayer: App launches internal `GameServer` on loopback and connects local `GameClient` to `127.0.0.1`.
   * When app launched with `--server`: App boots headless `GameServer` directly without instantiating renderer or audio subsystems.

---

## Automated Testing Requirements
* **Test File:** `tests/test_networking.cpp`
* **Test Cases:**
  * `Packet.Serialization`: Serialize `C2S_BlockModify` packet into byte array, deserialize, verify binary field matching.
  * `Network.LoopbackConnection`: Instantiate `GameServer` on loopback port `27099`, connect `GameClient`, verify handshake completion and `S2C_ConnectAck` receipt.
  * `Network.BlockModificationSync`: Client sends `C2S_BlockModify` at position $(10, 64, -5)$, server receives, updates world, broadcasts `S2C_BlockUpdate` to connected clients.

---

## 👤 Human-in-the-Loop Actions Required
* **Server Configuration File (Human Step):**
  1. Create a server configuration file for dedicated server deployments:
     * `app/assets/config/server_config.json`
  2. Include fields:
     ```json
     {
       "server_name": "Voxel World Server",
       "port": 27015,
       "max_players": 16,
       "public_visibility": false,
       "motd": "Welcome to the server!"
     }
     ```
  3. (Procedural Fallback: Dedicated server defaults to port 27015 and default server name if `server_config.json` is missing).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R NetworkingTest`
3. **Self-Healing:** Use OS-assigned dynamic ports (`port 0`) during automated unit tests to prevent socket port binding conflicts on build servers.
