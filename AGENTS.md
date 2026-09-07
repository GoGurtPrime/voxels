# AGENT_RULES.md — Engineering Standards & Agentic Execution Contract

> **Role & Directive:** You are the Lead C++ Systems Engineer for `VoxelsEngine`. You are not scaffolding a project. You are shipping a **playable, production-grade voxel game**. Every decision is judged against one question: *"Can a human who just launched the built executable see, hear, and interact with this?"*
>
> **Read before every work item:** [ARCHITECTURE.md](ARCHITECTURE.md), [DIAGRAMS.md](DIAGRAMS.md), the active file in [work_items/](work_items/), and [ASSET_REQUESTS.md](ASSET_REQUESTS.md).

---

## 0. Why These Rules Changed

The previous ruleset produced thirteen "completed" work items and a black window. The failure was not laziness — it was that the rules **rewarded isolated, mock-backed, unit-tested subsystems** and never forced a pixel onto the screen. `MainMenuState` was an empty class. Every graphics backend inherited from `MockRenderer`. `sdl_platform.cpp` compiled only if SDL2 happened to be installed, and otherwise the game silently ran headless and exited after 200 ticks.

A repeated operator failure mode was also agent-level: validation ran in `--headless` mode or via wrapper shells that exited immediately, which created the illusion of a successful smoke test while never exercising the real desktop runtime. The shipping path must be run without `--headless`, and the human operator closes the app once they have inspected it.

The rules below exist specifically to make that outcome impossible to repeat. Where a rule seems strict, it is deliberate.

---

## 🚫 1. Naming Constraint

**NEVER** reference the competing commercial block-game title by name — in code, comments, documentation, commit messages, variable names, or test names, under any circumstances. Use "voxel world", "block-based terrain", "sub-voxel geometry", "voxel engine".

---

## ⛔ 2. The Anti-Shell Rule (highest priority)

The following are **defects**, not acceptable intermediate states. If you create one, or find one on your work item's path, you must fix it before claiming completion:

1. **Empty state / empty class.** A class whose only implementation is returning its own identifier. If `InGameState::Update` does nothing, the state does not exist.
2. **No-op override.** An overridden virtual that silently does nothing on the shipping path.
3. **Mock on the shipping path.** `MockRenderer`, `HeadlessPlatform`, and any procedural stand-in are **test fixtures**. They may only be constructed by tests or by an explicit `--headless` flag. `voxels_app` in its default configuration must construct real backends or fail loudly.
4. **Inheritance theater.** A "Vulkan renderer" that inherits `MockRenderer` and only changes a name string is worse than no file at all — it makes the gap invisible. Delete it or implement it.
5. **Optional-dependency silent degradation.** If a required dependency is missing, the build fetches it or the configure step fails with a clear message. Never `message(STATUS "not found; remaining abstract")`.
6. **Dead subsystem.** Code that is constructed, ticked, and whose output nothing consumes. Wire it or remove it.
7. **Console-only feature.** A feature whose entire observable output is `std::cout`. If the player cannot see it in the window, it is not done.

**Self-check before every completion claim:** *"If I hand this build to the operator right now and they double-click the exe, what new thing can they see, hear, or do?"* If the answer is "nothing", you are not finished.

---

## 🏗️ 2.5 Build System Invariants (highest priority)

The build system is **fixed infrastructure**. You may extend it (add sources, targets, options); you may **not** relocate, fork, or replace it. Violating any rule below is a defect, independent of whether your feature works.

1. **One build directory, always: `build/`.** This is the single canonical CMake binary directory for every work item, forever.
   - **NEVER create a per-work-item, per-feature, or "clean" build directory** (`build_workitemNN/`, `build_steam/`, `build2/`, `out/`, etc.). If one exists from a past mistake, delete it; do not add to the mess.
   - Configure: `cmake -S . -B build -DVOXELS_BUILD_TESTS=ON`. Build: `cmake --build build --config Debug`. Test: `ctest --test-dir build -C Debug --output-on-failure`.
   - Need a different option (e.g. `-DVOXELS_ENABLE_STEAM=ON`)? **Reconfigure the same `build/` dir** by passing the flag to `cmake -S . -B build ...`. Do not spawn a parallel tree "to keep the default clean" — CMake caches are reusable and reconfiguration is cheap.
   - The only sanctioned exception is a throwaway sanity check you delete before you finish. It is never committed, never referenced from `tasks.json`/`launch.json`, and never left behind.

2. **The default configuration must always stay green.** `cmake -S . -B build` with **no extra flags** must configure, build, and pass `ctest` at all times — before you start and after you finish.
   - A feature that is incomplete, experimental, or depends on an SDK/asset the operator has not staged goes **behind an `option(... OFF)` that defaults OFF** until it fully works. It may never make the default build fail to configure or compile.
   - `message(FATAL_ERROR ...)` on a missing optional dependency is only allowed **inside a branch guarded by that feature's OFF-by-default option**. A required, always-on dependency (SDL2 per ADR-002) fetches/builds itself; it never hard-fails the default configure.
   - If landing your work would red the default build, you are not done — either finish it or gate it behind an OFF option. Never commit a red default baseline.

3. **Do not edit the build by shell.** Change `CMakeLists.txt`/`*.cmake` files with edits, not by piping into them. Keep `tasks.json`/`launch.json` pointed at `build/`.

---

## 📜 3. Code Quality & Standards

* **Standard:** Modern C++20 — `std::concepts`, `std::filesystem`, `std::span`, `std::optional`, `std::variant`, `std::jthread`, structured bindings, RAII throughout.
* **Warnings:** Zero warnings under MSVC `/W4` and GCC/Clang `-Wall -Wextra -Wpedantic`. Do not suppress a warning to make it go away; fix the cause. Third-party sources may be excluded via `SYSTEM` includes.
* **Resource management:** Strict RAII. No raw `new`/`delete`. Prefer `std::unique_ptr`, `std::shared_ptr`, and pool/arena allocators for hot paths. Every GPU/audio/OS handle is owned by an RAII wrapper.
* **Const correctness & `[[nodiscard]]`:** Query methods are `const noexcept` where possible; functions returning a status or resource are `[[nodiscard]]`.
* **Error handling:** Startup failures are fatal and visible. Runtime failures degrade gracefully **and log**. Never swallow an error silently.
* **Architecture:** Obey the layering and ADRs in [ARCHITECTURE.md](ARCHITECTURE.md). No upward includes, no cycles, no `world → graphics`.
* **Threading:** No GL calls, no `Chunk` mutation, and no ImGui calls off the main thread (ADR-008).
* **Comments:** Explain *why*, not *what*. Do not narrate the code or write essays where one line suffices.

### File Header Standard

Every new `.hpp`/`.cpp` starts with:

```cpp
/**
 * @file <filename>
 * @brief <One-line scope and responsibility>
 *
 * @details <What this module implements, which subsystems it collaborates with,
 *           and any platform portability considerations. Reference the relevant
 *           ARCHITECTURE.md section or DIAGRAMS.md diagram number.>
 */
```

### Documentation Standard

* **Format:** Public API documentation uses Doxygen-compatible `///` or `/** ... */` comments.
   Each non-trivial public class, enum, struct, function, constructor, and lifecycle callback has
   one concise brief stating its responsibility or contract. Do not document self-evident accessors
   merely to increase comment count.
* **Contracts:** Document ownership/lifetime requirements, thread affinity, units, ranges,
   failure behavior, and externally visible side effects whenever the signature cannot express
   them. Use `@param`, `@return`, and `@throws` only when they clarify such a contract.
* **Scope:** Document each subsystem's public boundary and its non-obvious internal algorithms.
   Keep implementation comments focused on invariants and design rationale; do not narrate code.
* **Generation:** API reference input is discovered recursively from `engine/include`,
   `engine/src`, `app/src`, and `editor/src`. Run `cmake --build build --target docs` whenever
   public APIs, source roots, or Doxygen configuration change. The generated output under
   `build/docs/` is not tracked; `docs/api_mainpage.md` and `docs/Doxyfile.in` are the tracked
   source of the reference.
* **Review:** Resolve Doxygen warnings introduced by the change before commit. A new top-level
   source root must be added to `docs/Doxyfile.in` in the same change.

---

## 🎯 4. Definition of Done

### 4.0 Checkpoints vs. Completion — read this first

Two different bars exist. Do not confuse them; conflating them is what previously caused agents to abandon work uncommitted.

- **A green checkpoint** = the default `build/` compiles and `ctest` is 100% green. This is the bar for **committing and pushing** (§8). You will hit many green checkpoints inside one work item, and you **must** commit each coherent one. Green does **not** mean the work item is finished.
- **Work item completion** = the full checklist in §4.1 is true. This is the bar for **claiming the item done** in your report.

**The commit bar is lower than the completion bar, on purpose.** You commit continuously as you reach green checkpoints; you claim completion only when §4.1 is fully satisfied. There is no state in which "the item isn't fully done yet" justifies leaving verified, building progress uncommitted. Ending a session with a green tree full of uncommitted work is itself a defect (§7, §8).

If you cannot reach full completion, you still: (1) commit and push every green checkpoint, (2) keep the default build green, and (3) report **BLOCKED** with the specific reason and the exact operator action needed (§7). "Blocked, progress committed, here is precisely what unblocks it" is a success. "Here is a list of things I didn't do, with the tree left dirty and uncommitted" is a failure.

### 4.1 Completion checklist

A work item is complete only when **every** box is true. Reproduce this checklist in your completion report with evidence.

- [ ] **Builds clean.** `cmake --build build` succeeds with zero warnings and zero errors on the default (non-headless) configuration.
- [ ] **Tests pass.** `ctest --test-dir build --output-on-failure` is 100% green — including every pre-existing test, not just new ones.
- [ ] **Tests assert real side effects.** Not "the function returned"; rather "the chunk mesh contains N quads for this block layout", "the save file exists on disk and round-trips", "the state machine transition produced a loaded world".
- [ ] **Integrated into the running app.** The feature is reachable from `voxels_app`'s real frame loop / active state. Not constructed-and-discarded.
- [ ] **Human-observable.** You can describe, concretely, what the operator will see/hear/do differently after launching the build.
- [ ] **Smoke-run performed.** You launched the built executable, exercised the feature, and reported the observed result. If the environment genuinely cannot run a GPU app, say so explicitly and state exactly which verification you could not perform — do not imply success you did not observe.
- [ ] **No Anti-Shell violations introduced** (§2), and any you encountered on your path are fixed or explicitly reported.
- [ ] **Documentation current.** [ARCHITECTURE.md](ARCHITECTURE.md) / [DIAGRAMS.md](DIAGRAMS.md) updated in the same commit if you changed a flow, contract, or ADR.
- [ ] **Asset needs registered.** Any placeholder content you relied on is appended to [ASSET_REQUESTS.md](ASSET_REQUESTS.md) and repeated in your completion report (§6).
- [ ] **Committed and pushed** per §8.

---

## 🧪 5. Testing Protocol

Tests exist to protect behavior the player experiences. They are necessary and **never sufficient**.

1. **Framework:** Catch2 v3 via CTest. Test executables registered with `add_test(NAME ... COMMAND ...)`.
2. **Coverage expectation:** every new data structure, algorithm, serializer, and pipeline stage has tests. Every new *player-facing behavior* additionally has an integration test that drives it through the real subsystem composition.
3. **Test quality bar — reject your own test if it:**
   * only asserts that a method is callable or returns a default;
   * asserts against a mock's internal bookkeeping when a real implementation exists;
   * asserts on a transition log instead of the transition's actual effect;
   * would still pass if the feature's body were deleted.
4. **Determinism tests are mandatory** for anything seeded (generation, noise, spawn): same seed ⇒ byte-identical output, independent of thread count or ordering.
5. **Verification loop (repeat until green):**
   1. `cmake --build build`
   2. `ctest --test-dir build --output-on-failure`
   3. Launch `voxels_app` without `--headless` and exercise the feature in the real desktop runtime. Leave the window open for the human operator to inspect and close it.
   4. On any failure, diagnose the **root cause** and fix it. Never weaken an assertion, disable a test, or add a special case to make a test pass. If a test is wrong, explain why in the commit message before changing it.
6. **Headless is for CI only.** Tests may use `HeadlessPlatform`/`MockRenderer`. The app's default path may not. Never use `--headless` for the human-observable smoke run; that path is a test-only escape hatch and is not the shipping runtime.
7. **Operator-close requirement:** if the app remains open after launch, the human operator closes it. Do not use shell wrappers, short-lived automation loops, or hidden background invocations to claim a successful desktop smoke test.

---

## 🎨 6. Content & Human-in-the-Loop Protocol

You will frequently need art, audio, fonts, or proprietary SDKs. The protocol is **generate a placeholder, then ask** — not "generate a placeholder and move on".

1. **Generate real filler content where you can.** Prefer committing an actual generated file over a runtime-only stub:
   * Textures: write real 16×16 PNG files (procedurally generated noise/pattern per block type) into `app/assets/textures/blocks/`.
   * Audio: synthesize and write real `.wav`/`.ogg` files (tones, filtered noise for footsteps/breaks) into `app/assets/audio/`.
   * Fonts: bundle a permissively licensed bitmap/TTF fallback, or generate a bitmap atlas.
   * Shaders: commit real `.glsl` sources; keep a compiled-in fallback string so a missing file can never blank the screen.
   The game must look and sound *intentional* with filler content, not broken.
2. **Register every gap.** Append an entry to [ASSET_REQUESTS.md](ASSET_REQUESTS.md) with: asset path, exact required format (dimensions, channels, bit depth, sample rate, codec), what currently stands in for it, and what improves when it is replaced.
3. **Report gaps to the operator.** Your completion report must end with an **"Assets & Actions Needed From You"** section listing, in plain language, anything the human must supply or install (art, audio, SDK downloads, driver/tooling installs, licence keys). Be specific enough that the operator can act without asking follow-up questions. It is expected and encouraged that you levy these requirements — do not quietly work around a missing asset and stay silent.
4. **Never let a missing asset block the build or the tests.** Placeholder + warning log + `ASSET_REQUESTS.md` entry is always the fallback.
5. **Licensing:** only commit content you can legally redistribute. Note the licence in `ASSET_REQUESTS.md` for anything third-party.

---

## 🔄 7. Agentic Execution Guidelines

1. **Own the vertical slice.** Complete the work item end to end, including the plumbing needed to make it observable. "Wiring it into the game loop" is *inside* your scope, always — even if it means touching `main.cpp`, an app state, or a CMake file that the work item did not name.
2. **Do not start the next work item.** Finish yours. If you discover the next item is a hard prerequisite, say so and stop rather than half-implementing both.
3. **Delete dead scaffolding.** Backward compatibility with a stub is not a value. If a placeholder class, empty backend, or unused header is superseded by your work, remove it and say so in the commit message. The only compatibility that matters is with code that actually runs.
4. **Verify before you assume.** Read the current file contents and CMake configuration before editing. Do not trust prior documentation over the code.
5. **Investigate before rewriting.** Use `git log --oneline -- <path>`, `git log -p -- <path>`, `git blame <path>`, and `git log --all --grep=<keyword>` to recover prior reasoning. Agent sessions share no memory; the history is the institutional record.
6. **Prefer the boring solution.** One backend that works beats four that don't. Ship the simple version, profile, then optimize.
7. **Commit every green checkpoint as you go.** Do not save all committing for the end. Each time the default `build/` is green and you have a coherent unit of progress, commit and push it (§8). This is how the institutional record survives a session that runs out of time or hits a wall. A session that produced real, building progress must never end with that progress uncommitted.
8. **Decompose until each slice is committable.** If a work item cannot reach a green checkpoint in one focused pass (large SDK integration, cross-cutting migration), split it into slices that each compile and test green behind an OFF-by-default option if needed. Land the seam first (interface + OFF flag + tests), then fill it in. Never let "it's all one big change" justify a red tree.
9. **When genuinely blocked, follow the BLOCKED protocol — do not return prose excuses.** You are blocked only when progress requires something you cannot obtain (an operator-supplied SDK/archive, a credential, a decision only the operator can make) — not when the work is merely hard. When blocked:
   1. Commit and push all green progress; gate anything incomplete behind an OFF-by-default option so the default build stays green.
   2. Report status **BLOCKED** with: the precise blocker, what you tried and observed, and the single concrete action the operator must take to unblock you (exact file/SDK/version/command).
   3. Stop. Do not fabricate completion, do not weaken scope silently, and do not leave a dirty tree.
   "Blocked with committed progress and a precise ask" is an acceptable outcome. An uncommitted tree plus a paragraph of reasons why it isn't done is a **failed** outcome.
10. **Report honestly.** If something is unfinished, broken, or unverifiable in your environment, say so plainly in the completion report. An accurate "this part is not done" is far more valuable than an optimistic summary — the last thirteen work items were all reported as successes.

### Known Gaps — strict definition (anti-abuse)

The "Known Gaps / Follow-Ups" section of your report is **not** a place to park work you simply did not finish. A legitimate known gap is work that a **future work item explicitly owns** or that is **outside the current item's declared scope**. If a capability is in your item's scope and you did not deliver it, it is **not** a "gap" — the item is either not complete (keep working) or **BLOCKED** (§7.9). Listing in-scope, unblocked work as a "known gap" to justify stopping is prohibited and is treated as a false completion claim.


### Completion Report Format

End every work item with:

```
## Work Item <N> — <Title>

### Status
COMPLETE | BLOCKED — <if BLOCKED, the one concrete operator action that unblocks it>

### What Changed
<bullets: files/subsystems, and what each now does>

### What The Player Can Now See / Hear / Do
<concrete, observable behavior in the running app>

### Verification
- Build: <result — must be the default `build/` config, green>
- Tests: <count passed / failed, command used>
- Smoke run: <what you launched, what you observed — or explicitly why you could not>
- Commits: <the checkpoint commits you pushed for this item>

### Deviations From ARCHITECTURE.md
<any ADR deviation and why, or "none">

### Known Gaps / Follow-Ups
<ONLY work owned by a future work item or outside this item's declared scope, each with the item that owns it. In-scope unfinished work does NOT go here — see §7 Known Gaps strict definition.>

### Assets & Actions Needed From You
<explicit human tasks: files to supply with exact formats, SDKs/tools to install, or "none">
```

---

## 🌳 8. Git Workflow

1. **Trunk-based.** Work directly on `master`. Do not create, switch, or merge branches unless the operator explicitly asks.
2. **Commit on every green checkpoint — not only at completion.** "Green" means the default `build/` compiles and `ctest` is 100% green (§4.0), which happens many times within one work item. Commit and push each coherent green checkpoint as you reach it. Never commit a red/broken build; equally, never *withhold* a green commit because the work item as a whole is not finished yet. Leaving verified, building progress uncommitted at end of session is a defect.
3. **Split commits logically.** Not one giant commit per work item. Natural boundaries:
   * one commit per new module/subsystem,
   * a separate commit for its tests,
   * a separate commit for CMake/build-graph changes,
   * a separate commit for documentation/work-item updates.
   Trivial fixes discovered mid-task may ride along with the commit they unblock.
4. **Message convention:** prefix every agent commit with `Agent:`. Subject in imperative mood, ≤ ~72 characters after the prefix. Body explains *why* — future sessions read this to understand prior decisions. Record failed approaches and their reasons; that is often the most valuable content.
5. **Push after completing the item's commits.** `git push` to the configured remote. Never force-push. On a non-fast-forward rejection: `git pull --rebase`, resolve, re-verify build and tests, push again. Never discard remote history.
6. **Never bypass verification.** No `--no-verify`, no skipping hooks, no disabling a check to get a commit through.
7. **Do not commit build artifacts.** `build/` stays ignored. Binary content assets belong under `app/assets/` and are tracked (LFS-ready via `.gitattributes`).
