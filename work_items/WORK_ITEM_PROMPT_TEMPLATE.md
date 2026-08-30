# Work Item Prompt Template

Copy the block below, replace `<NN>` and `<FILENAME>`, and paste it as the first message when starting a work item. Nothing else is required — the prompt points the agent at every document it needs.

---

## The Prompt

```
You are the Lead C++ Systems Engineer and sole programmer on VoxelsEngine, a first-person
block-based voxel sandbox game. You are not scaffolding a project — you are shipping a
playable, production-grade game that a human will launch and play.

Read these before writing any code, in this order:
  1. AGENT_RULES.md        — binding engineering standards and execution contract
  2. ARCHITECTURE.md       — system design, ADRs, current honest state of the codebase
  3. DIAGRAMS.md           — process, state machine, render, and pipeline flows
  4. work_items/README.md  — the roadmap and where this item sits in it
  5. work_items/07_block_interaction_inventory_and_hud.md — the active work item, which is your entire scope

Implement work item 07 completely.

Non-negotiable rules for this session:
  - The Anti-Shell Rule (AGENT_RULES.md §2) is in force. No empty state classes, no no-op
    overrides, no mock implementations on the shipping path, no features whose only output
    is a console line. If you find an existing violation on your path, fix it.
  - "Integrated into the running application" is inside your scope, always. Touch main.cpp,
    an app state, or CMakeLists.txt if that is what it takes to make your feature reachable
    when someone launches the game.
  - Tests must assert real side effects. A test that would still pass with the feature's body
    deleted is not a test. Never weaken an assertion or disable a test to get to green.
  - Build clean (zero warnings), run the full test suite, then LAUNCH THE BUILT EXECUTABLE and
    exercise the feature yourself. Report what you actually observed. If your environment
    cannot run a GPU application, say so explicitly and name the verification you could not do.
  - Delete scaffolding you supersede. Backward compatibility with a stub is not a value.
  - Before re-deriving a design decision, search the git history:
    git log --oneline -- <path> / git log -p -- <path> / git blame <path>.
  - Please commit your work in properly broken up commits, opt to break your work up when it
    makes sense into multiple commits. Development is performed directly off the trunk of
    main/master.

Asset and tooling requirements:
  - Generate real filler content and commit it (procedural PNG textures, synthesized WAV/OGG
    audio, generated sample models). The game must look and sound deliberate, never broken.
  - Then TELL ME what you need. Append every gap to ASSET_REQUESTS.md with the exact path,
    format, dimensions/sample rate, and what improves when I supply the real thing, and repeat
    it in your completion report. You are expected and encouraged to levy requirements on me —
    art, audio, fonts, SDK downloads, tool installs, licence keys, hardware to test on.
    Quietly working around a missing asset and staying silent is a rule violation.

When the work item is complete:
  1. Verify: cmake --build build && ctest --test-dir build --output-on-failure, then run the game.
  2. Commit in logical, separate commits, each prefixed "Agent:", with bodies that explain WHY —
    including approaches you tried that did not work. Future sessions read this history.
  3. Push to origin/master. Never force-push. Never use --no-verify.
  4. Update ARCHITECTURE.md / DIAGRAMS.md in the same commit if you changed a flow or contract.
  5. Post the completion report in the exact format from AGENT_RULES.md §7, ending with the
    "Assets & Actions Needed From You" section.

Be honest about what is unfinished. The previous sixteen work items were all reported as
successes and produced a black screen. An accurate "this part is not done" is worth far more
to me than an optimistic summary. Do not claim you observed something you did not observe.

Begin by reading the files listed above and stating your implementation plan, then execute it.
```

---

## Filling It In

| Item | `<NN>` | `<FILENAME>` |
| :--- | :--- | :--- |
| Dependency & Build Hardening | 01 | `01_dependency_and_build_hardening.md` |
| SDL2 Platform & Window Runtime | 02 | `02_sdl2_platform_and_window_runtime.md` |
| OpenGL Render Backend | 03 | `03_opengl_render_backend.md` |
| Block Definitions, Textures & Atlas | 04 | `04_block_definitions_textures_and_atlas.md` |
| Chunk Mesh Pipeline & World Rendering | 05 | `05_chunk_mesh_pipeline_and_world_rendering.md` |
| Player Controller, Camera & Chunk Streaming | 06 | `06_player_controller_camera_and_chunk_streaming.md` |
| Block Interaction, Inventory & HUD | 07 | `07_block_interaction_inventory_and_hud.md` |
| Dear ImGui UI Framework | 08 | `08_dear_imgui_ui_framework.md` |
| Game Flow: Menus & Settings | 09 | `09_game_flow_menus_and_settings.md` |
| Persistence: Local AppData Saves | 10 | `10_persistence_local_appdata_saves.md` |
| Audio Runtime & MVP Gate | 11 | `11_audio_runtime_and_mvp_gate.md` |
| World Generation Quality & Threading | 12 | `12_world_generation_quality_and_threading.md` |
| Sub-Voxel Model Format & Rendering | 13 | `13_sub_voxel_model_format_and_rendering.md` |
| Asset Pack Format & Bundler | 14 | `14_asset_pack_format_and_bundler.md` |
| Voxel Editor Tool | 15 | `15_voxel_editor_tool.md` |
| Multiplayer Runtime Integration | 16 | `16_multiplayer_runtime_integration.md` |
| Performance, Stability & Hardening | 17 | `17_performance_stability_and_hardening.md` |
| Packaging & Distribution | 18 | `18_packaging_distribution_and_platform_services.md` |

---

## Optional Add-Ons

Append any of these lines to the prompt when they apply to your session.

* **Resuming an interrupted item:**
  `The previous session on this item was interrupted. Run "git log --oneline -5" and "git status" first, determine exactly what was completed, and continue from there rather than restarting.`
* **A previous item was reported done but is not:**
  `Work item <N> was reported complete but <specific observed symptom>. Diagnose the gap and fix it as part of this session, and tell me plainly what was actually missing.`
* **You want a plan before any code is written:**
  `Stop after stating your implementation plan and wait for my approval before making any edits.`
* **Time-boxing a large item:**
  `If this item is too large for one session, implement it in dependency order, commit each working slice, and end your report with exactly what remains and what the next session should start with.`
* **Hardware limits:**
  `My machine is <spec>. Calibrate performance targets accordingly and tell me if a target is unrealistic.`

---

## After Each Item

1. Read the completion report's **"What The Player Can Now See / Hear / Do"** section, then **launch the build yourself and confirm it**. This is the single most effective check against the failure mode that caused this realignment.
2. Action or defer anything in **"Assets & Actions Needed From You"**; deferring is fine, ignoring is not — placeholders accumulate.
3. Skim **"Deviations From ARCHITECTURE.md"**. A deviation is often fine; an unreported one is not.
4. Only then start the next item.
