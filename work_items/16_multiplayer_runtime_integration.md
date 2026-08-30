# Work Item 16 — Multiplayer Runtime Integration

**Phase:** F — Production Hardening · **Prerequisites:** 10, 12

---

## 1. Problem Statement

An Asio UDP `GameServer`/`GameClient` pair exists and ticks every frame while carrying no gameplay traffic. Nothing is replicated: no player movement, no chunk data, no block edits. The architecture states that singleplayer *is* a hosted server (ADR-006), but gameplay currently runs entirely client-side, so multiplayer would be a rewrite rather than a switch.

## 2. Objective

Make the existing loopback server genuinely authoritative for singleplayer, then let a second player join over LAN with no separate code path.

## 3. Scope

**In scope:** protocol definition, connection lifecycle, chunk streaming over the wire, entity replication, client prediction and reconciliation, block-edit authority, join/host UI, dedicated server mode, network robustness and security.

**Out of scope:** matchmaking, NAT punch-through, dedicated server management tooling, anti-cheat beyond authority.

## 4. Implementation Tasks

1. **Protocol (versioned):** `C2S_Handshake`, `C2S_Input`, `C2S_BlockEdit`, `C2S_ChatMessage`, `C2S_Disconnect`, `C2S_KeepAlive`; `S2C_Accept`, `S2C_Reject{reason}`, `S2C_ChunkData`, `S2C_ChunkUnload`, `S2C_EntityState`, `S2C_BlockEdit`, `S2C_PlayerJoinedLeft`, `S2C_ChatMessage`, `S2C_Disconnect`. Every packet carries a protocol version; a mismatch is rejected with a readable reason.
2. **Reliability layer over UDP:** sequence numbers, acks + ack bitfield, retransmission for the reliable channel (block edits, chunk data, join/leave), unreliable for the high-frequency channel (input, entity state). Ordered delivery only where required. Fragmentation and reassembly for chunk payloads exceeding the MTU.
3. **Authority migration:** move world mutation, physics, and block edits behind the server. The local client sends intents and renders replicated state. In singleplayer both live in-process over loopback, so there is exactly one gameplay code path.
4. **Client-side prediction & reconciliation:** the client predicts its own movement immediately with the same deterministic physics, buffers unacknowledged inputs, and on receiving an authoritative state replays the buffered inputs from that point. Correction is smoothed, not snapped. Remote players are interpolated with a small delay buffer.
5. **Chunk streaming over the network:** the server tracks each client's loaded set and streams compressed chunk data in nearest-first order with a per-client bandwidth cap; unload messages when a client moves away. Singleplayer short-circuits the serialization for in-process delivery while keeping the identical logical flow.
6. **Block-edit authority:** clients request edits; the server validates reach, cooldown, permissions, and block state before applying and broadcasting. A rejected edit rolls back the client's optimistic prediction.
7. **Remote player rendering:** replicated players render as `.vmdl` player models with position, yaw/pitch, and a held item.
8. **UI integration:** pause menu **Toggle World Visibility** binds/unbinds the server to `0.0.0.0` and displays the joinable LAN address and port; the main menu gains **Join Game** (direct IP entry plus LAN discovery broadcast) and a connection-progress/failure screen with real error text.
9. **Dedicated server:** `voxels_app --server --world=<name> --port=<n>` runs headless with no renderer, loading and saving the same world format, with a console log and graceful `Ctrl+C` shutdown. Reads `app/assets/config/server_config.json`.
10. **Robustness & security:** timeouts with keep-alives, clean disconnect on quit, a max-player cap, connection rate limiting, per-packet size bounds, and strict validation of every field from the wire. **Never trust a client packet** — malformed or hostile input must be dropped, not parsed into an out-of-bounds access.

## 5. Acceptance Criteria

* Singleplayer plays identically to before, but all world mutation now flows through the server — verified by instrumentation, not assumption.
* Two instances on one LAN: the second joins the first's world, both see each other move in real time, and blocks broken by one appear broken for the other within a frame or two.
* Movement feels responsive on the client despite server authority (prediction working, no rubber-banding on a healthy connection).
* Simulated 100 ms latency and 5% packet loss remain playable.
* A client that quits, crashes, or is unplugged is cleaned up server-side without affecting other players.
* The dedicated server runs for an extended session with clients joining/leaving and saves the world correctly.

## 6. Automated Tests

`tests/test_multiplayer.cpp`:
* `Protocol.EveryPacketTypeSerializesAndDeserializesExactly`.
* `Protocol.VersionMismatchIsRejectedWithReason`.
* `Protocol.MalformedAndOversizedPacketsAreRejectedSafely` — fuzzed buffers, assert no out-of-bounds access.
* `Reliability.LostReliablePacketsAreRetransmittedAndDeliveredOnce`.
* `Reliability.UnreliableChannelToleratesLossWithoutStalling`.
* `Fragmentation.LargeChunkPayloadReassemblesExactly`.
* `Connection.HandshakeAcceptAssignsUniquePlayerIdsUpToMaxPlayers`.
* `Connection.TimeoutDisconnectsSilentClientAndFreesResources`.
* `Replication.SecondClientReceivesFirstClientsPositionUpdates`.
* `Replication.BlockEditByOneClientIsAppliedServerSideAndBroadcastToAll`.
* `Authority.ServerRejectsOutOfReachEditAndClientRollsBackPrediction`.
* `Prediction.ReconciliationConvergesWithoutOscillation` — inject corrections, assert bounded error and no divergence.
* `Streaming.ServerStreamsNearestChunksFirstWithinBandwidthCap`.
* `Singleplayer.LoopbackPathUsesTheSameGameplayCodeAsRemote` — a shared-path assertion, not a parallel implementation.

## 7. Anti-Shell Checks

- [ ] There is exactly one gameplay code path for solo and multiplayer.
- [ ] The server is not "ticking but idle" — real traffic is asserted.
- [ ] No packet field from the network is used without validation.

## 8. Assets & Human Actions

* Ask the operator to test a real two-machine LAN session and report latency/feel — this cannot be fully validated by a single agent.
* Note any firewall rule the operator must allow for the chosen UDP port.

## 9. Verification

Build, test, then launch **two instances**, host from one, join from the other, and report exactly what each instance observed.
