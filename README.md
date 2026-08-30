# VoxelsEngine

A cross-platform, first-person **block-based voxel sandbox game** and the custom C++20 engine it runs on, plus an internal voxel model/content editor.

---

## ⚠️ Project Status — Honest Assessment

**The game is not currently playable.** Launching `voxels_app` opens an SDL window and renders a black screen: no menu, no world, no player, no audio.

A full audit was performed and the project was realigned. In short:

| Area | State |
| :--- | :--- |
| World generation, chunk storage, greedy mesher, physics, input, networking | **Real and reusable** — roughly half the engine works |
| Renderer | **Does not exist.** Every backend was an empty subclass of a CPU mock; no graphics API call has ever been issued |
| UI | **Does not exist.** Dear ImGui was never a dependency |
| App states (main menu, in-game, pause) | **Empty classes** |
| Integration | Working subsystems are never called from the running game loop |
| Editor | A stub that prints one line and exits |
| Content | Two JSON files; no textures, audio, fonts, shaders, or models |

The corrected plan is in **[work_items/README.md](work_items/README.md)** — 18 sequenced work items that connect the existing engine to a screen, a mouse, and a speaker, then take it to a shippable build. The playable milestone is work item 11.

---

## Documentation Map

| Document | Purpose |
| :--- | :--- |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | System design, architectural decision records, honest subsystem status, contracts, performance budget |
| **[DIAGRAMS.md](DIAGRAMS.md)** | Mermaid diagrams: layering, boot, state machine, frame loop, simulation tick, render pipeline, chunk lifecycle, generation, input, persistence, content pipeline, networking, audio |
| **[AGENT_RULES.md](AGENT_RULES.md)** | Engineering standards, the Anti-Shell Rule, definition of done, testing bar, content protocol, git workflow |
| **[work_items/README.md](work_items/README.md)** | The 18-item roadmap with milestones and execution rules |
| **[work_items/WORK_ITEM_PROMPT_TEMPLATE.md](work_items/WORK_ITEM_PROMPT_TEMPLATE.md)** | The prompt used to start each work item |
| **[ASSET_REQUESTS.md](ASSET_REQUESTS.md)** | Art, audio, fonts, and SDKs the human operator needs to supply |

---

## Tech Stack

| Concern | Choice |
| :--- | :--- |
| Language | C++20 |
| Build | CMake ≥ 3.21, CMakePresets |
| Windowing / input / audio device | SDL2 (**required**, fetched if not found — ADR-002) |
| Graphics | **OpenGL 3.3 Core** as the reference backend behind an RHI (ADR-001). Vulkan / DX12 / Metal are declared but deliberately unimplemented |
| UI | Dear ImGui (ADR-003) |
| Math | GLM |
| Networking | Standalone Asio (UDP) |
| Decoders | stb_image, stb_vorbis, dr_wav, stb_truetype |
| Testing | Catch2 v3 + CTest |
| Platform services | Steamworks, optional and disabled by default |

---

## Repository Layout

```text
voxels/
├── engine/     # voxels_engine static library — core, platform, graphics, render, world,
│               # worldgen, gameplay, ui, input, audio, assets, net, app
├── app/        # voxels_app executable + shipped assets/
├── editor/     # voxels_editor executable — model authoring and asset pack bundling
├── tests/      # Catch2 suites
└── work_items/ # The execution roadmap
```

---

## Building

Requires a C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+), CMake 3.21+, and Git. All other dependencies are fetched automatically by CMake.

```bash
cmake --preset windows-debug      # or linux-debug / macos-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Legacy invocation without presets:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### Running

```bash
./build/app/voxels_app                    # the real desktop game; leave it open for the human operator to close
./build/app/voxels_app --server           # dedicated server
./build/editor/voxels_editor              # the content editor
```

Important: do not run the desktop smoke test in `--headless` mode. The real windowed app is the verification target, and if it stays open the operator closes it manually. `--headless` is a CI/test-only flag and must not be used to claim the shipping runtime works.

Useful flags: `--fullscreen=<true|false>`, `--resolution=<WxH>`, `--vsync=<true|false>`, `--world=<name>`, `--headless` (tests/CI only), `--dev` (asset hot-reload).

### User data

Saves, settings, logs, and screenshots live in the OS user-data directory, not the build tree (ADR-011):

| OS | Path |
| :--- | :--- |
| Windows | `%LOCALAPPDATA%\VoxelsEngine\` |
| Linux | `$XDG_DATA_HOME/VoxelsEngine/` (default `~/.local/share/VoxelsEngine/`) |
| macOS | `~/Library/Application Support/VoxelsEngine/` |

---

## Contributing / Working the Roadmap

Work items are executed **in order, one at a time**. Start one by copying the prompt from [work_items/WORK_ITEM_PROMPT_TEMPLATE.md](work_items/WORK_ITEM_PROMPT_TEMPLATE.md). Every item ends with a completion report stating what a human can newly see, hear, or do — and what content or tooling the operator needs to supply next.

The governing rule is [AGENT_RULES.md](AGENT_RULES.md) § 2, the Anti-Shell Rule: a subsystem that is only observable through its unit tests is not finished.
