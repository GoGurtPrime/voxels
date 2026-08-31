---
name: cpp-multiplayer-networking
description: 'Design, implement, debug, and ship production C++ multiplayer networking for games: authoritative dedicated servers, client prediction, replication, matchmaking, cross-platform and console compatibility, UDP/QUIC transports, protocol security, and network performance. Use when building client/server architecture, online gameplay, multiplayer synchronization, or platform-neutral networking.'
argument-hint: 'Describe the multiplayer feature, target platforms, player scale, and latency requirements'
---

# Production C++ Multiplayer Networking

Build complete, production-ready multiplayer systems for PC, consoles, and diverse client hardware. Favor an authoritative server, portable transport and platform seams, bandwidth-efficient replication, predictable simulation, and fully wired runtime behavior. Do not leave TODOs, placeholder handlers, or unimplemented failure paths.

## When to Use

- Design or implement dedicated server, listen server, client, or shared networking code
- Add multiplayer gameplay, matchmaking, lobbies, sessions, replication, or reconnection
- Implement client prediction, server reconciliation, interpolation, lag compensation, or anti-cheat boundaries
- Diagnose packet loss, jitter, latency, desynchronization, bandwidth, scalability, or security problems
- Prepare C++ online features for PC, console, or cross-platform certification and release

## Operating Principles

- Keep simulation authority on the server. Clients submit timestamped intent; they never decide protected gameplay outcomes.
- Separate platform services, transport, protocol, replication, simulation, and gameplay. Platform SDK code must not leak into shared gameplay or wire-protocol types.
- Treat every remote packet and platform callback as hostile input: authenticate sessions, enforce size/rate/state limits, validate fields before use, and fail closed.
- Prefer explicit binary serialization with fixed widths, bounds checks, protocol versions, endian conversion, and forward-compatible feature negotiation. Never serialize C++ object memory directly.
- Make reliability selective: use unreliable sequenced delivery for frequent state, reliable ordered delivery only for commands that truly require it, and independent channels to prevent head-of-line blocking.
- Use RAII, ownership clarity, bounded queues, backpressure, monotonic clocks, and allocation-aware hot paths. Keep network I/O separate from simulation/render threads.
- Build complete vertical slices, including connection lifecycle, errors, teardown, metrics, tests, configuration, and runtime wiring.

## Procedure

### 1. Establish the Online Contract

1. Identify game topology: dedicated authoritative server by default; use listen servers only when host migration, NAT, trust, and platform policy are explicitly handled.
2. Record target platforms, cross-play identity requirements, expected and peak concurrent players, regional deployment, tick rate, bandwidth budget, latency targets, and offline/degraded behavior.
3. Classify each message by direction, owner, delivery semantics, frequency, maximum payload, security sensitivity, and recovery behavior.
4. Choose a proven transport compatible with all targets and SDK constraints. Prefer UDP with a mature reliable transport layer or QUIC when its platform support and operational characteristics fit. Isolate it behind a small transport interface.
5. Define the server trust boundary and the source of truth for identity, entitlement, matchmaking, moderation, persistence, and telemetry.

### 2. Design the Architecture

1. Define interfaces for platform online services, transport connections, packet codec, session/lobby service, replication, and simulation bridge. Keep their dependencies one-way.
2. Use a fixed simulation tick and a network time model. Convert external timestamps into a validated server-relative timeline.
3. Run receive/decrypt/decode work away from the game thread; hand validated immutable commands to the simulation through bounded queues. Send snapshots from immutable or synchronized simulation views.
4. Use stable network entity identifiers and explicit spawn/despawn ownership. Never expose raw pointers, local entity addresses, or platform handles over the wire.
5. Model each connection as a state machine: disconnected, resolving/authenticating, connecting, handshaking, loading, active, reconnecting, disconnecting, and failed. Define allowed transitions, timeouts, and idempotent cleanup.

### 3. Specify Protocol and Replication

1. Write a message schema before implementation: message ID, version, channel, bounds, field encoding, validation rules, and backwards-compatibility behavior.
2. Negotiate protocol revision and features during the authenticated handshake. Reject incompatible clients with actionable diagnostics.
3. Assign sequence numbers and acknowledgements per stream. Track round-trip time, jitter, loss, delivery age, queue pressure, and bytes by message class.
4. Replicate only relevant entities using interest management. Send baselines plus deltas, quantize values intentionally, use bit/varint packing where justified by profiling, and prioritize gameplay-critical state under a strict packet budget.
5. Use client-side prediction only for locally owned, reversible actions. Keep input history, authoritative tick/state, and deterministic or explicitly reconciled correction paths. Interpolate remote entities from buffered snapshots.
6. For latency-sensitive hits or interactions, validate historical server state using bounded rewind windows; never accept client-reported hits without server-side validation.

### 4. Implement Secure, Complete Flows

1. Implement connect, handshake, authentication, session join, initial world sync, active replication, graceful leave, timeout, reconnect, and shutdown end to end.
2. Validate packet framing before allocation; impose per-message limits, per-connection queue limits, token-bucket rate limits, replay protection, and handshake deadlines.
3. Authenticate and encrypt connections using maintained cryptographic/transport libraries and platform services. Do not invent cryptography or embed credentials in client code.
4. Make retries and duplicate delivery safe. Ensure server commands carry enough identifiers/ticks to be deduplicated or rejected by state.
5. Surface typed errors to game/UI layers without exposing security-sensitive server detail to remote clients. Log structured diagnostics with connection/session correlation IDs.

### 5. Integrate Across Platforms

1. Put platform SDK adapters behind common interfaces; compile shared protocol and gameplay code without console-specific dependencies.
2. Respect platform identity, invite, presence, privacy, parental-control, suspend/resume, background-networking, NAT traversal, and certification requirements.
3. Verify byte order, alignment, packet sizing, threading, and clock behavior on every architecture. Do not assume desktop socket or filesystem behavior on consoles.
4. Use feature negotiation and compatibility tests so a range of supported client versions and capabilities can interoperate safely.

### 6. Validate Before Shipping

1. Add unit tests for codecs, malformed inputs, state transitions, delta/baseline behavior, relevance filtering, prediction/reconciliation, and cleanup.
2. Add integration tests with at least one server and multiple simulated clients covering join, gameplay, loss, reordering, disconnect, reconnect, and version mismatch.
3. Run deterministic network simulation for latency, jitter, packet loss, duplication, reordering, MTU constraints, bandwidth starvation, and long pauses.
4. Load test to the expected peak with representative message mixes. Profile server tick time, allocations, queue depth, CPU, memory, packets per second, bandwidth, and p50/p95/p99 latency.
5. Test abuse cases: invalid framing, oversized packets, flood attempts, stale/replayed messages, invalid state transitions, unauthorized entity operations, and malformed platform callbacks.
6. Confirm live observability: dashboards/metrics, structured logs, alert thresholds, feature flags, protocol rollout/rollback, and an actionable incident procedure.

## Decision Rules

- Use a dedicated authoritative server for competitive, persistent, or anti-cheat-sensitive gameplay. Consider peer-to-peer only when platform support, trust model, and host migration are fully designed and tested.
- Use snapshots plus interpolation for remote continuous state; use discrete reliable commands for infrequent durable events; avoid reliable delivery for high-rate transforms.
- Use shared deterministic simulation only when determinism can be maintained across all target compilers, CPUs, floating-point modes, and platform policies. Otherwise reconcile explicit server state.
- Introduce custom compression, bit packing, or lock-free structures only after profiling shows they address a measured limit. Preserve readability, bounds checks, and diagnostics.
- Reject a feature design that lacks an authority model, timeout/recovery flow, bandwidth budget, validation strategy, and automated adverse-network test.

## Completion Criteria

- Client, server, protocol, transport, and platform seams compile and run for the supported configurations.
- All lifecycle and failure paths are implemented, bounded, observable, and cleaned up idempotently.
- Server authority is enforced for protected state and every untrusted input is validated before mutation or allocation.
- The feature passes unit, multi-client integration, adverse-network, load, and security-abuse tests with metrics within its defined targets.
- No networking work item is considered complete until it is connected to the real runtime, exercised end to end, and documented with operational configuration and diagnostics.