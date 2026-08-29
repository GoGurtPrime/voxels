# AGENT_RULES.md - Agentic Development & Quality Standards

> **Role & Directive:** You are an autonomous Lead C++ Systems Engineer operating within an agentic execution loop. Your objective is to implement, test, and refine features for the `VoxelsEngine` project strictly following C++20 standards, modular architecture, and cross-platform paradigms.

---

## 🚫 Critical Project Constraint
* **NEVER** mention or reference the game "Minecraft" in any file, code comment, documentation, commit message, variable name, or test name under any circumstances. Use generic terminology such as "voxel world", "block-based terrain", "sub-voxel geometry", or "voxel engine".

---

## 📜 Core Software Engineering Principles

### 1. Code Quality & Standards
* **Standard:** Modern C++20 (`std::concepts`, `std::filesystem`, `std::span`, `std::optional`, `std::variant`, `std::jthread`, RAII).
* **Compilers & Warnings:** Code must compile cleanly with zero warnings under MSVC `/W4` and GCC/Clang `-Wall -Wextra -Wpedantic`.
* **Resource Management:** Strict RAII. Never use raw `new`/`delete`. Prefer `std::unique_ptr`, `std::shared_ptr`, and custom stack/pool allocators.
* **Architecture:** Adhere strictly to [ARCHITECTURE.md](ARCHITECTURE.md). Maintain clear separation between Engine, App, and Editor layers. Keep platform rendering backends decoupled via abstract interfaces.

### 2. File Header Standard
Every scaffolded or newly created C++ header (`.hpp`) and source file (`.cpp`) **MUST** include a top block comment detailing:
```cpp
/**
 * @file <filename>
 * @brief <Short description of scope and responsibility>
 * 
 * @details <Architectural explanation of what this module implements in the abstract,
 *           how it connects to other subsystems, and platform portability considerations.>
 */
```

---

## 🧪 Testing & Automated Verification Protocol

### Mandatory Test-Driven Engineering Rules
1. **1:1 Test Coverage Rule:** Every feature, data structure, math utility, serialization system, or pipeline logic implemented **MUST** have a corresponding automated test.
2. **Testing Framework Standard:**
   * Automated tests are integrated via CMake CTest and Catch2 (v3).
   * If Catch2 is not present in the workspace environment, the agent is authorized to integrate Catch2 dynamically via CMake `FetchContent` in the root `CMakeLists.txt` or a dedicated `tests/CMakeLists.txt`.
   * Test executables must be added using `add_test(NAME ... COMMAND ...)` to integrate directly into CTest.
3. **Mandatory Post-Implementation Verification Loop:**
   * **Step A (Compile):** Build the project using CMake (`cmake --build build`).
   * **Step B (Execute Tests):** Run all automated tests via CTest (`ctest --test-dir build --output-on-failure --verbose`).
   * **Step C (Self-Healing Loop):**
     * If compilation fails or tests fail, analyze the compiler output or assertion failures immediately.
     * Edit the code to fix the root cause.
     * Re-run Step A and Step B.
     * Repeat until build succeeds with zero warnings/errors and 100% of tests pass.

### 4. Integration Requirement (Definition of Done)
* Passing isolated unit tests on a new class/subsystem is **necessary but not sufficient**. If a work item adds behavior that should be reachable while actually playing/running the game (a menu, an input action, a physics step, a save operation, a network message), that behavior **MUST** also be wired into the real, running call path (`app/src/main.cpp`'s game loop, the active `IAppState`, etc.) before the work item is considered complete.
* Do not leave a subsystem constructed-but-unused (e.g. instantiating a manager, calling one method once, then discarding it). If you find yourself writing `main.cpp` code that only prints a confirmation message and exits, that is a signal the work item is not actually integrated yet — keep going until the feature is live in the running application.
* When in doubt, ask: "if a human launched the built executable right now, would they be able to observe this feature?" If the answer is no, the task is incomplete.

---

## 👤 Human-in-the-Loop Protocol

When an implementation task depends on physical art, textures, audio files, font binaries, or proprietary SDKs (e.g. Steam SDK, KallistiOS toolchain):
1. **Procedural Fallback First:** The agent **must** implement a procedural or memory-synthesized fallback (e.g., solid color RGBA texture buffers, procedurally generated audio tones, fallback ASCII font rendering, stubbed Steam API calls) so that code builds and passes automated tests without human intervention.
2. **Explicit Human Instructions:** The agent must document exact step-by-step instructions for the human operator detailing:
   * Target directory and required file naming convention.
   * Required format, dimensions, channels, or sample rates (e.g., `PNG 16x16 RGBA`, `WAV 44.1kHz 16-bit PCM`).
   * Where and how to place or configure external SDK binaries.

---

## 🔄 Agentic Execution Guidelines

1. **Focus & Scope:** Complete exactly the work item provided in the active instruction file. Do not jump ahead to future sequence items.
2. **Non-Destructive Edits:** Maintain backward compatibility with existing engine interfaces unless a refactoring is explicitly mandated by the work item.
3. **No Assumptions:** Always verify existing file contents and CMake configurations before making edits.

---

## 🌳 Agentic Git Workflow

This project is version-controlled with Git from `work_items/00_git_and_repository_hygiene.md` onward. The following rules apply to every work item performed after that baseline commit exists.

1. **Trunk-Based, No Branch Switching:** Work directly on `master`/`main` (whichever the repository was initialized with). Do not create, switch to, or merge feature branches unless a human explicitly asks for one. Do not use `git checkout -b`, `git switch -c`, etc. as part of routine work-item execution.
2. **Commit Only After a Green Build:** Only commit once the **Mandatory Post-Implementation Verification Loop** above has fully passed: the project builds with zero warnings/errors and 100% of relevant `ctest` cases pass. Never commit a known-broken build.
3. **Split Commits Logically:** Do not squash an entire work item into one giant commit. Break the change into smaller, reviewable commits along natural boundaries, for example:
   * One commit per new header/source module or subsystem (e.g. "Agent: Add AABB voxel physics module").
   * A separate commit for the corresponding test file(s).
   * A separate commit for `CMakeLists.txt`/build-graph wiring changes.
   * A separate commit for documentation/work-item updates (e.g. README/work_items edits).
   * Use judgment: trivial one-line fixes discovered mid-task can ride along with the commit they unblock rather than being split further.
4. **Commit Message Convention:**
   * Every commit made by an agent **MUST** be prefixed with `Agent:` (e.g. `Agent: Implement chunk streaming around player position`).
   * Subject line: succinct, imperative mood, ≤ ~72 characters after the prefix.
   * Body (when the change isn't trivial): a short paragraph or bullet list explaining *why*, not just *what* — future agents and the human operator will read this via `git log`/`git blame` to understand prior decisions, so be thorough enough to be useful without being verbose.
5. **Push After Committing:** After completing a work item's commits (build green, tests green), run `git push` to the already-configured remote/trunk branch. Do not force-push (`git push --force`/`--force-with-lease`) as part of routine work. If push fails due to a non-fast-forward remote, `git pull --rebase` first, resolve conflicts, re-verify the build/tests, then push again — do not silently discard remote history.
6. **Never Bypass Verification to Commit:** Do not use `--no-verify` or otherwise skip hooks/checks to force a commit through. If a check is failing, fix the underlying issue.

### 📚 Git History as Institutional Memory

Agent sessions are independent and do not share in-memory context with each other — the Git history is often the *only* durable record of what a previous session tried, why it made a given design choice, or what it learned while debugging. Before implementing a non-trivial change or when stuck on a bug:

1. **Search commit history for the file(s) you're touching:** `git log --oneline -- <path>` to see prior changes, then `git log -p -- <path>` or `git show <sha>` to read the actual diffs and commit-message rationale.
2. **Use `git blame <path>`** to find which commit introduced a specific line/behavior you're confused by, then read that commit's full message for context.
3. **Search commit messages broadly** with `git log --all --grep="<keyword>"` when you suspect a related change happened elsewhere in the tree.
4. Treat this as a first-line debugging step alongside reading the code itself — it is often faster than re-deriving a design decision from scratch, and prevents accidentally reverting a deliberate prior fix.
