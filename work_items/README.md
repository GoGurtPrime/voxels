# Work Items — Execution Roadmap

> **Read first:** [ARCHITECTURE.md](../ARCHITECTURE.md), [DIAGRAMS.md](../DIAGRAMS.md), [AGENT_RULES.md](../AGENT_RULES.md).
> **Prompt to start an item:** [WORK_ITEM_PROMPT_TEMPLATE.md](WORK_ITEM_PROMPT_TEMPLATE.md).
> **Content the operator owes us:** [ASSET_REQUESTS.md](../ASSET_REQUESTS.md).

---

## Why This Sequence Was Rewritten

The previous roadmap ran sixteen work items and was reported complete through "Minimum Viable Playable Build". The result launches an SDL window and renders a black screen. There is no menu, no world on screen, no player, no sound, and no way to start a game.

The audit found the cause, and it was systemic rather than incidental:

* **No renderer exists.** Every graphics backend was an empty subclass of `MockRenderer`. Nothing in the project has ever issued a graphics API call.
* **SDL2 was optional.** When CMake could not find it, the platform layer was excluded from the build and the game silently ran headless for 200 ticks and exited — while still reporting a successful build.
* **No UI exists.** Dear ImGui was never a dependency. `UIManager` computes a DPI scale and draws nothing.
* **Every app state was an empty class.** `MainMenuState`, `InGameState`, and `PauseMenuState` had one member each: a function returning their own enum value.
* **Working subsystems were never connected.** Physics, input, the greedy mesher, block interaction, and networking are all genuinely implemented — and none of them are called from the running game loop.
* **The editor was never started.** It prints one line and exits.
* **There is no content.** `app/assets/` contains two JSON files. No textures, audio, fonts, shaders, or models.

The old sequence rewarded isolated, mock-backed subsystems with passing unit tests. The new sequence is ordered so that **something new is visible or audible in the running game after almost every item**, and [AGENT_RULES.md](../AGENT_RULES.md) has been rewritten to make "mock-backed and unit-tested" an explicit failure state rather than a definition of done.

Work already done is not wasted — roughly half the engine is real and reusable. Items 01–11 connect it to a screen, a mouse, and a speaker.

---

## Completed Roadmap

All 18 items below are complete. Their individual documents remain in this directory as the
implementation and acceptance-record history; do not reopen or modify them for unrelated work.
New work begins in a separately numbered item after the candidate has been reviewed against the
current architecture and tested desktop build.

| # | Work Item | Phase | Status | What the player can do afterwards |
| :--- | :--- | :--- | :--- | :--- |
| **01** | [Dependency & Build Hardening](01_dependency_and_build_hardening.md) | A · Foundation Repair | Complete | (build only) SDL2/ImGui/decoders are mandatory; no silent headless fallback |
| **02** | [SDL2 Platform & Window Runtime](02_sdl2_platform_and_window_runtime.md) | A | Complete | A real, resizable window with a GL context and a proper frame loop |
| **03** | [OpenGL Render Backend](03_opengl_render_backend.md) | A | Complete | **The screen is no longer black** — real 3D rendering |
| **04** | [Block Definitions, Textures & Atlas](04_block_definitions_textures_and_atlas.md) | B · Making the World Visible | Complete | Blocks have real, data-driven textures |
| **05** | [Chunk Mesh Pipeline & World Rendering](05_chunk_mesh_pipeline_and_world_rendering.md) | B | Complete | **The voxel world is on screen** — lit, textured terrain with water |
| **06** | [Player Controller, Camera & Chunk Streaming](06_player_controller_camera_and_chunk_streaming.md) | B | Complete | Walk, look, jump, fall, and explore streaming terrain |
| **07** | [Block Interaction, Inventory & HUD](07_block_interaction_inventory_and_hud.md) | B | Complete | **Break and place blocks** with crosshair, hotbar, and highlight |
| **08** | [Dear ImGui UI Framework](08_dear_imgui_ui_framework.md) | C · The Application Shell | Complete | Themed, DPI-aware UI with correct input arbitration; F3 overlay |
| **09** | [Game Flow: Menus & Settings](09_game_flow_menus_and_settings.md) | C | Complete | **Main menu, world creation, loading, pause, settings** — a real game shell |
| **10** | [Persistence: Local AppData Saves](10_persistence_local_appdata_saves.md) | C | Complete | **Worlds and settings persist** — quit and resume exactly where you left off |
| **11** | [Audio Runtime & MVP Gate](11_audio_runtime_and_mvp_gate.md) | C | Complete | **The game is fully playable, and audible.** Hard gate. |
| **12** | [World Generation Quality & Threading](12_world_generation_quality_and_threading.md) | D · Depth & Polish | Complete | Biomes, mountains, connected caves, ores — a world worth exploring |
| **13** | [Sub-Voxel Model Format & Rendering](13_sub_voxel_model_format_and_rendering.md) | E · Content Tooling | Complete | Stairs, slabs, torches, doors, items — shapes beyond cubes |
| **14** | [Asset Pack Format & Bundler](14_asset_pack_format_and_bundler.md) | E | Complete | The game ships and runs from a validated `core.vpk` content pack |
| **15** | [Voxel Editor Tool](15_voxel_editor_tool.md) | E | Complete | Author models, assign textures, define blocks, build packs |
| **16** | [Multiplayer Runtime Integration](16_multiplayer_runtime_integration.md) | F · Production Hardening | Complete | Host and join over LAN; singleplayer is genuinely server-authoritative |
| **17** | [Performance, Stability & Hardening](17_performance_stability_and_hardening.md) | F | Complete | 60 FPS, no leaks, no crashes, survives hostile input and long sessions |
| **18** | [Packaging, Distribution & Platform Services](18_packaging_distribution_and_platform_services.md) | F | Complete | A redistributable build a non-developer can unzip and play |
| **19** | [World Light Propagation & Edit Relighting](19_world_light_propagation_and_edit_relighting.md) | G · Lighting Foundation | Planned | Opened terrain, caves, and emissive blocks receive correct, seamless voxel light |
| **20** | [Celestial Cycle & Renderer-Neutral Lighting](20_celestial_cycle_and_renderer_neutral_lighting.md) | G | Planned | Daylight, twilight, and moonlight progress consistently in local and joined worlds |
| **21** | [Cascaded Soft Directional Shadows](21_cascaded_soft_directional_shadows.md) | G | Planned | Terrain and structures cast stable, soft sun and moon shadows |

## Next-Round Candidates

Candidates are intentionally not work items until scoped and approved:

1. Server-authoritative input simulation with prediction and correction, closing ADR-007's multiplayer gap.
2. Automated clean-machine package smoke tests on Windows, Linux, and macOS in CI.
3. Release-grade audio: streamed music, mixer groups, and replacement licensed sound assets.

---

## Completed Milestones

| Gate | After | Meaning |
| :--- | :--- | :--- |
| **Pixels** | 03 | The renderer is real. The reported black-screen symptom is gone. |
| **World** | 05 | The generated voxel world is visible on screen. |
| **Sandbox loop** | 07 | Move, look, jump, break, place — the core loop exists. |
| **Game shell** | 10 | Menus, settings, and persistence — it behaves like a game, not a demo. |
| **🚩 MVP** | 11 | **Playable end to end.** Do not begin 12 until the item 11 playability sweep passes. |
| **Content pipeline** | 15 | Content can be authored and shipped without touching C++. |
| **Shippable** | 18 | A stranger can download it and play it. |

---

## Execution Rules

1. **In order, one at a time.** Prerequisites are real; skipping produces the exact failure this rewrite is correcting.
2. **Do not start the next item.** If the current item cannot be completed without the next one, stop and say so rather than half-implementing both.
3. **The MVP gate at item 11 is hard.** If any step of its playability sweep fails, item 11 is not complete. Items 12–18 are polish, depth, and distribution on top of a game that already works.
4. **Every item ends with a completion report** in the format defined in [AGENT_RULES.md](../AGENT_RULES.md) § 7 — including the **"Assets & Actions Needed From You"** section. Agents are expected to levy asset and tooling requirements on the operator; working around a missing asset in silence is a rule violation.
5. **Update the documentation in the same commit** when an item changes a flow, contract, or ADR. [DIAGRAMS.md](../DIAGRAMS.md) is meant to stay accurate, not become archaeology.
6. **Delete what you replace.** Superseded stubs, dead backends, and unused headers go away. Compatibility with a placeholder is not a value.

---

## Adding Work Items

New items go at the end unless a prerequisite genuinely forces an earlier slot. When inserting, renumber the files, update this table, and explain the reordering in the commit message. Every item must state, in one sentence, **what a human can newly see, hear, or do** once it lands. If it cannot, it is probably not a work item — it is a task inside one.
