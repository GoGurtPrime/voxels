# Work Item 17 — Performance, Stability & Production Hardening

**Phase:** F — Production Hardening · **Prerequisites:** 16

---

## 1. Problem Statement

Nothing in this project has ever been profiled, stress-tested, fuzzed, or run under a sanitizer. There is no frame-time instrumentation, no memory accounting, no crash handling, no leak verification, and no long-session soak testing. A game that runs for five minutes on the developer's machine is not a product.

## 2. Objective

Make the game hold the performance budget in [ARCHITECTURE.md](../ARCHITECTURE.md) §8, survive long sessions and hostile input, and fail informatively when it fails at all.

## 3. Scope

**In scope:** profiling instrumentation, optimization of measured hot paths, memory management, crash/exception handling, logging, sanitizer and fuzz coverage, soak and stress testing, CI.

**Out of scope:** new features. If a fix requires a feature, note it and scope it separately.

## 4. Implementation Tasks

1. **Profiling instrumentation:** scoped CPU timers (`VOXELS_PROFILE_SCOPE`) around frame phases — events, simulation, streaming, meshing, upload, culling, draw submission, UI, present — aggregated into rolling statistics, surfaced in the F3 overlay and dumpable to a trace file (Chrome trace JSON) with `--profile-trace`. GPU timer queries around each render pass.
2. **Measure first, then optimize.** Produce a baseline report before changing anything. Likely targets, to be confirmed by measurement:
   * mesher allocation churn (arena/scratch buffers per worker, reused across jobs);
   * chunk vertex memory (packed format, indexed quads, per-chunk buffer pooling);
   * draw call count (batch adjacent chunks or use a shared buffer with per-chunk offsets);
   * culling cost (hierarchical/column-level rejection before per-chunk tests);
   * world block lookups (chunk pointer caching, avoiding repeated map lookups in physics and meshing);
   * GPU upload stalls (orphaned/persistent-mapped buffers, staging ring).
3. **Memory accounting:** category-tagged allocation tracking (chunks, meshes, textures, audio, network) reported in the debug overlay. Hard caps with eviction for the chunk mesh cache. Prove no growth over a soak run.
4. **Crash & exception handling:** a top-level handler that logs the exception/signal with a stack trace where available, flushes the log, attempts an emergency world save, and shows the player a readable crash dialog pointing at the log file. Never a silent disappearance.
5. **Logging hardening:** rotating log files in `<userdata>/logs/`, retention cap, levels configurable at runtime, no logging in hot loops, thread-safe sinks, and a startup banner recording engine version, OS, CPU, GPU, driver version, and resolved asset sources — the first thing needed to diagnose an operator's bug report.
6. **Graceful degradation:** if the frame budget is missed persistently, automatically reduce effective render distance (with a toast) rather than stuttering indefinitely. Low-memory conditions evict mesh cache entries before failing.
7. **Sanitizers & static analysis:** CI configurations with ASan+UBSan (Clang/GCC) and MSVC `/analyze` or clang-tidy. Fix every finding. Run the full test suite under sanitizers.
8. **Fuzzing:** fuzz harnesses for every untrusted-input parser — `.vpk` TOC, `.vmdl`, region files, `player.dat`, `level.json`, `blocks.json`, and network packets. Assert no crash, no out-of-bounds, no unbounded allocation.
9. **Stress & soak tests:**
   * 60-minute automated soak: continuous movement across a large area, block edits, autosaves, pause/resume cycles. Assert bounded memory and no frame-time drift.
   * Rapid world create/load/quit cycling (100 iterations) — assert no handle or memory leak.
   * Extreme render distance and rapid teleport-style movement to stress streaming.
   * Filling a chunk with edits then re-meshing repeatedly.
10. **Robustness matrix:** disk full during save, read-only user directory, save file deleted mid-session, audio device removed, window minimized for an extended period, system sleep/resume, GPU driver reset, unplugged controller. Each must degrade gracefully with a log entry and, where relevant, a player-visible message.
11. **CI:** GitHub Actions building Windows/Linux/macOS in Debug and Release, running the full headless test suite, the sanitizer job, and the fuzz corpus smoke run on every push. Warnings-as-errors enforced. A red build blocks further work items.

## 5. Acceptance Criteria

* 60 FPS held at 1080p, render distance 8, on the reference machine — with the profiler trace to prove where time goes.
* A 60-minute soak run finishes with flat memory and no frame-time drift.
* Zero sanitizer findings across the full test suite.
* Every fuzz harness survives its corpus with no crash.
* Every scenario in the robustness matrix produces a graceful, logged outcome.
* CI is green on all three platforms.

## 6. Automated Tests

`tests/test_performance_and_stability.cpp` plus dedicated harnesses:
* `Perf.FrameBudgetIsMetForSyntheticWorkload` — asserts against the §8 budget with generous CI headroom.
* `Perf.MesherThroughputMeetsThreshold` — chunks/second on the test machine, guarding against regression.
* `Perf.MeshCacheRespectsMemoryCapAndEvictsLeastRecentlyUsed`.
* `Stability.SoakRunKeepsMemoryBoundedAndFrameTimeStable` (long-running, tagged `[.soak]`).
* `Stability.RepeatedWorldLoadUnloadLeaksNothing` — allocation counters return to baseline.
* `Stability.DiskFullDuringSaveLeavesPriorSaveIntactAndReportsError`.
* `Stability.ReadOnlyUserDirectoryProducesReadableErrorNotCrash`.
* `Stability.AudioDeviceRemovalIsHandled`.
* `Fuzz.VpkVmdlRegionAndPacketParsersSurviveCorpus`.
* `Crash.HandlerWritesLogAndAttemptsEmergencySave` — simulated fault.

## 7. Anti-Shell Checks

- [ ] Optimizations are justified by before/after measurements, not guesses.
- [ ] The profiler reports real timings, not fabricated values.
- [ ] No fuzz or sanitizer finding is suppressed rather than fixed.

## 8. Assets & Human Actions

* Ask the operator for their target hardware spec so the budget is calibrated to reality.
* Ask them to run a long play session and report any hitch, leak, or crash, with the log file path.

## 9. Verification

Build, run the full suite including sanitizers, execute the soak test, and report the before/after profiling numbers with the specific optimizations that produced them.
