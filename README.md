# VoxelsEngine

A cross-platform, first-person **block-based voxel sandbox game** and the custom C++20 engine it runs on, plus an internal voxel model/content editor.

---

## Project Status — Honest Assessment

**The game is playable.** Launching `voxels_app` opens a rendered main menu; you can create a
world, spawn into lit, textured terrain with water and trees, walk/jump/collide, break and place
blocks with a HUD (crosshair/hotbar/highlight), hear audio, pause/save/resume, and host or join
over LAN. Work items 01–17 closed the original "black screen" gap; this session (work item 18)
adds packaging, an install/CPack pipeline, versioning, a first-run experience, and optional
Steam platform services.

Per-subsystem status (kept current in [ARCHITECTURE.md](ARCHITECTURE.md) §2 — read that table,
not this paragraph, for the authoritative detail): world generation, chunk meshing/rendering,
physics/input/block-interaction, the ImGui UI and app-state flow, local-AppData persistence, the
audio pipeline, and singleplayer/LAN networking are **Real** or **Partial** and genuinely wired
into the running game. Vulkan/DX12/Metal renderer backends and Dreamcast/console targets are
**declared but deliberately unimplemented** (ADR-001/ADR-013) — OpenGL 3.3 Core is the one
backend that ships. Full lateral cross-chunk light propagation, OGG music streaming, and
server-authoritative input prediction (ADR-007) remain open follow-ups.

The roadmap that got here — and what's left — is in **[work_items/README.md](work_items/README.md)**.

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
| **[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md)** | Steps to work through before tagging and shipping a release |

---

## Tech Stack

| Concern | Choice |
| :--- | :--- |
| Language | C++20 |
| Build | CMake ≥ 3.21, CMakePresets, CPack |
| Windowing / input / audio device | SDL2 (**required**, fetched if not found — ADR-002) |
| Graphics | **OpenGL 3.3 Core** as the reference backend behind an RHI (ADR-001). Vulkan / DX12 / Metal are declared but deliberately unimplemented |
| UI | Dear ImGui (ADR-003) |
| Math | GLM |
| Networking | Standalone Asio (UDP) |
| Decoders | stb_image, stb_vorbis, dr_wav, stb_truetype |
| Testing | Catch2 v3 + CTest |
| Platform services | `IPlatformServices`: `NullPlatformServices` by default; optional Steamworks (`VOXELS_ENABLE_STEAM=ON`) for achievements/rich presence/overlay/cloud saves |

---

## Repository Layout

```text
voxels/
├── engine/     # voxels_engine static library — core, platform, graphics, render, world,
│               # worldgen, gameplay, ui, input, audio, assets, net, app
├── app/        # voxels_app executable + shipped assets/
├── editor/     # voxels_editor executable — model authoring and asset pack bundling
├── packaging/  # Templates for the packaged README.txt / THIRD_PARTY_LICENSES.txt
├── docs/       # RELEASE_CHECKLIST.md and other process docs
├── cmake/      # Helper scripts (e.g. version-header generation)
├── tests/      # Catch2 suites
└── work_items/ # The execution roadmap
```

---

## Building

Requires a C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+), CMake 3.21+, and Git. Most
dependencies (SDL2, Dear ImGui, GLM, Asio, glad, Catch2) are fetched automatically by CMake if
not already found on the system.

### Windows

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

If you have [vcpkg](https://github.com/microsoft/vcpkg) installed with `VCPKG_ROOT` set, `vcpkg
install sdl2:x64-windows` first and pass `-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
(or add it to your own preset) to use a real SDL2 install instead of the fetched copy. Visual
Studio is a multi-config generator, so built binaries land at `build/app/Debug/voxels_app.exe`
(or `Release/`), not `build/app/voxels_app`.

### Linux / macOS

```bash
cmake --preset linux-debug      # or macos-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

### Legacy invocation without presets (any platform)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### Running

```bash
./build/app/Debug/voxels_app.exe          # Windows (multi-config); the real desktop game — leave it open for the human operator to close
./build/app/voxels_app                    # Linux/macOS (single-config)
./build/app/Debug/voxels_app.exe --server # dedicated server
./build/editor/Debug/voxels_editor.exe    # the content editor
```

Important: do not run the desktop smoke test in `--headless` mode. The real windowed app is the verification target, and if it stays open the operator closes it manually. `--headless` is a CI/test-only flag and must not be used to claim the shipping runtime works.

Useful flags: `--fullscreen=<true|false>`, `--resolution=<WxH>`, `--vsync=<true|false>`, `--world=<name>`, `--server`, `--join=<host:port>`, `--headless` (tests/CI only).

### User data

Saves, settings, logs, and screenshots live in the OS user-data directory, not the build tree (ADR-011). On first launch the directory tree is created and default settings are written automatically, and a dismissible controls card is shown once (reopenable from the pause menu):

| OS | Path |
| :--- | :--- |
| Windows | `%LOCALAPPDATA%\VoxelsEngine\` |
| Linux | `$XDG_DATA_HOME/VoxelsEngine/` (default `~/.local/share/VoxelsEngine/`) |
| macOS | `~/Library/Application Support/VoxelsEngine/` |

---

## Packaging a Distributable Build

```bash
cmake --build build --config Release
cmake --install build --config Release --prefix ./stage      # inspect the layout, or:
(cd build && cpack -C Release)                                 # produces a per-platform archive
```

This produces `voxels_app(.exe)`, the SDL2 (and, if enabled, Steam) runtime libraries, and
`assets/` alongside `README.txt`, `LICENSE.txt`, and a build-generated `THIRD_PARTY_LICENSES.txt`
— a self-contained folder a non-developer can unzip and run. The editor is never included in
this package; it ships separately as an internal tool. See
[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) before tagging an actual release, and
[work_items/18_packaging_distribution_and_platform_services.md](work_items/18_packaging_distribution_and_platform_services.md)
for the full packaging design.

### Optional Steam integration

```bash
cmake -S . -B build -DVOXELS_ENABLE_STEAM=ON -DSTEAM_SDK_ROOT=engine/third_party/steam
```

Off by default, and the default build has zero Steam dependency. When the SDK is not found at
`STEAM_SDK_ROOT`, CMake disables the feature with a warning rather than failing the build.

---

## Contributing / Working the Roadmap

Work items are executed **in order, one at a time**. Start one by copying the prompt from [work_items/WORK_ITEM_PROMPT_TEMPLATE.md](work_items/WORK_ITEM_PROMPT_TEMPLATE.md). Every item ends with a completion report stating what a human can newly see, hear, or do — and what content or tooling the operator needs to supply next.

The governing rule is [AGENT_RULES.md](AGENT_RULES.md) § 2, the Anti-Shell Rule: a subsystem that is only observable through its unit tests is not finished.

