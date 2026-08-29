# Work Items Sequence Index & Execution Strategy

> **Role & Directive:** This sequence index guides the principal solutions architect, software lead, and LLM coding agents through the step-by-step implementation of the `VoxelsEngine` project.

---

## ⚠️ Sequencing History (read this first)

Work items `01`-`10` (formerly `00`-`09`) were completed as isolated, unit-tested subsystems, and that is exactly the problem this re-sequencing fixes: nothing required those subsystems to be wired together into a running, playable application. Running `voxels_app` produced no visible gameplay because `app/src/main.cpp` only constructed each subsystem once, printed a line, and exited — no game loop, no player entity, no physics, no persistence.

Items `11`, `12`, and `13` are **new** and exist specifically to close that gap:
* `11_gameplay_player_and_physics` — the missing player/physics/camera/block-interaction layer.
* `12_persistence_and_runtime_integration` — the missing real game loop + save/load + chunk streaming that ties every prior subsystem together at runtime.
* `13_minimum_viable_playable_build` — the capstone: an actual end-to-end playable Main Menu → World Creation → Loading → Spawn → Play → Pause → Save & Quit experience, verified by integration tests that assert on real side effects, not just state-transition logs.

Former items `10` (Steam SDK) and `11` (Voxel Editor) have been renumbered to `14` and `15` and pushed to run **after** `13`, since platform-SDK integration and content-authoring tooling are not prerequisites for a playable base game and must not be prioritized ahead of it.

Item `00_git_and_repository_hygiene` is also new: it initializes Git, `.gitignore`, `.gitattributes` (LFS-ready for future art/audio/model assets), and produces the baseline commit for everything already implemented. See `AGENT_RULES.md` → *Agentic Git Workflow* for the ongoing commit/push convention every subsequent item must follow.

---

## 🗂 Sequence Roadmap & Dependencies

| Sequence ID | Work Item File | Key Deliverable | Co-dependencies & Prerequisites |
| :--- | :--- | :--- | :--- |
| **00** | [00_git_and_repository_hygiene.md](00_git_and_repository_hygiene.md) | Git init, `.gitignore`/`.gitattributes`, baseline commit | *None (First Step)* |
| **01** | [01_testing_and_build_infrastructure.md](01_testing_and_build_infrastructure.md) | Catch2 & CTest integration | `00` |
| **02** | [02_core_engine_foundation.md](02_core_engine_foundation.md) | Logger, Config/Preferences, Math | `01` |
| **03** | [03_platform_windowing_sdl2.md](03_platform_windowing_sdl2.md) | SDL2 Windowing & Event Loop | `02` |
| **04** | [04_input_and_multiplayer_abstraction.md](04_input_and_multiplayer_abstraction.md) | Input Action Mapping & Drop-in Splitscreen | `03` |
| **05** | [05_graphics_renderer_abstraction.md](05_graphics_renderer_abstraction.md) | Multi-Backend RHI & Mock Renderer | `03` |
| **06** | [06_world_voxel_core.md](06_world_voxel_core.md) | Blocks, Chunks, Sub-Voxels, Serialization | `02`, `05` |
| **07** | [07_world_generation_pipeline.md](07_world_generation_pipeline.md) | Multi-Phase Noise Terrain & Safe Spawn | `06` |
| **08** | [08_audio_and_asset_pipeline.md](08_audio_and_asset_pipeline.md) | Audio Device, 3D Spatial Sound, Asset Packs | `02` |
| **09** | [09_app_lifecycle_ui_and_menus.md](09_app_lifecycle_ui_and_menus.md) | CLI Parser, Menus, State Machine, ImGui | `02`, `03`, `04`, `05`, `07` |
| **10** | [10_networking_and_server_client.md](10_networking_and_server_client.md) | Asio Packets, Server/Client, Loopback | `02`, `06`, `09` |
| **11** | [11_gameplay_player_and_physics.md](11_gameplay_player_and_physics.md) | Player entity, AABB physics, block break/place | `04`, `05`, `06`, `07` |
| **12** | [12_persistence_and_runtime_integration.md](12_persistence_and_runtime_integration.md) | Real game loop, world/player save-load, chunk streaming | `03`, `05`, `06`, `07`, `09`, `10`, `11` |
| **13** | [13_minimum_viable_playable_build.md](13_minimum_viable_playable_build.md) | End-to-end playable MVP (menus → spawn → play → save) | `09`, `10`, `11`, `12` |
| **14** | [14_steam_sdk_and_platform_integration.md](14_steam_sdk_and_platform_integration.md) | Steam API Wrapper & Platform Services | `02`, `09`, `10`, `13` |
| **15** | [15_voxel_editor_tool.md](15_voxel_editor_tool.md) | Sub-Voxel Model Editor & Asset Packer | `02`, `05`, `06`, `08`, `09`, `13` |

---

## 💡 Co-Dependency & Grouping Considerations

When prompting an LLM coding agent, some work items can either be executed individually or in combined feature pairs if higher throughput is desired:

* **Foundation Phase (00, 01, 02):** Must be completed first to establish version control, CTest testing, and logging/config infrastructure.
* **Platform & Hardware Group (03, 04, 05):** These three form the core platform engine. If working in a single large session, `03` + `04` or `03` + `05` can be fed together.
* **Voxel Core & Generation Group (06, 07):** `06` establishes block/chunk memory layouts, and `07` builds procedural generation directly on top of `06`.
* **App & Networking Integration (09, 10):** `09` creates menus and command line options (`--server`), which directly trigger `10`'s internal loopback server or dedicated server mode.
* **Playable-Game Group (11, 12, 13):** These three must be worked *in order* and are not safe to parallelize — `12` depends on `11`'s `PlayerState` shape, and `13`'s tests depend on `12`'s real game loop existing. Do not skip ahead to `13` without `11`/`12` merged and passing.
* **Post-MVP Group (14, 15):** Steam/platform SDK integration and the editor tool are valuable but strictly secondary to having a playable base game; only pick these up once `13` is green.

---

## 🤖 Instructions for Feeding Work Items to Coding Agents

1. **Prompt Template:** Provide the LLM coding agent with:
   * The active Work Item file (e.g. `work_items/02_core_engine_foundation.md`).
   * The master rules file (`AGENT_RULES.md`).
   * The architectural context file (`ARCHITECTURE.md`).
2. **Execution Prompt:**
   > *"You are acting as the lead developer for VoxelsEngine. Please implement Work Item `02_core_engine_foundation.md` strictly following the standards in `AGENT_RULES.md` and `ARCHITECTURE.md`. Ensure all tests in `tests/` pass cleanly via `cmake --build build` and `ctest --test-dir build` before completing your turn, then follow the Agentic Git Workflow in `AGENT_RULES.md` to commit and push your work."*
3. **Review Human-in-the-Loop Section:** Check if the active work item lists human steps (e.g., placing art/textures/audio files, installing SDL2 dev libraries). If human assets are missing, verify that the agent created procedural fallbacks so automated testing passes.
4. **Definition of Done includes integration, not just unit tests:** for any work item that adds runtime behavior reachable from a running session (gameplay, menus, networking, persistence), passing isolated unit tests is necessary but **not sufficient** — the behavior must also be wired into `app/src/main.cpp`'s real game loop (from `12` onward) so it is actually reachable when the game runs. See `AGENT_RULES.md` → *Integration Requirement*.
5. **When stuck, search history first:** before re-deriving a design decision from scratch, use `git log --oneline -- <path>` / `git log -p -- <path>` / `git blame <path>` to see what a previous agent session already tried and why — see `AGENT_RULES.md` → *Git History as Institutional Memory*.
