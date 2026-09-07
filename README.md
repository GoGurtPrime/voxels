# VoxelsEngine

A cross-platform, first-person **block-based voxel sandbox game** and the custom C++20 engine it runs on, plus an internal voxel model/content editor.

---

## Project Status

`voxels_app` provides a rendered main menu, world creation, lit and textured terrain with water
and trees, player movement and collision, block interaction, a HUD, audio, save and resume
support, and LAN hosting and joining. The project also includes packaging, an install/CPack
pipeline, versioning, a first-run experience, and optional Steam platform services.

For subsystem status and architectural decisions, see [ARCHITECTURE.md](ARCHITECTURE.md).
The project currently ships with an OpenGL 3.3 Core renderer. Vulkan, DirectX 12, Metal, and
Dreamcast/console targets are documented future backends. Cross-chunk lateral light propagation,
OGG music streaming, and server-authoritative input prediction remain planned work.

The development roadmap is maintained in [work_items/README.md](work_items/README.md).

---

## Documentation Map

| Document | Purpose |
| :--- | :--- |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | System design, architectural decision records, subsystem status, contracts, performance budget |
| **[DIAGRAMS.md](DIAGRAMS.md)** | Mermaid diagrams: layering, boot, state machine, frame loop, simulation tick, render pipeline, chunk lifecycle, generation, input, persistence, content pipeline, networking, audio |
| **[AGENT_RULES.md](AGENT_RULES.md)** | Engineering standards, completion requirements, testing, content protocol, and project workflow |
| **[work_items/README.md](work_items/README.md)** | The 18-item roadmap with milestones and execution rules |
| **[work_items/WORK_ITEM_PROMPT_TEMPLATE.md](work_items/WORK_ITEM_PROMPT_TEMPLATE.md)** | The prompt used to start each work item |
| **[ASSET_REQUESTS.md](ASSET_REQUESTS.md)** | Art, audio, fonts, and SDKs required for development |
| **[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md)** | Steps to work through before tagging and shipping a release |
| **[docs/PLAYER_UI_BRIDGE_API.md](docs/PLAYER_UI_BRIDGE_API.md)** | Versioned JS↔CEF↔C++ bridge contract, payload schemas, and extension rules for web developers |

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

If you have [vcpkg](https://github.com/microsoft/vcpkg) installed with `VCPKG_ROOT` set, run `vcpkg
install sdl2:x64-windows` and pass `-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
(or add it to a local preset) to use an installed SDL2 package instead of the fetched copy. Visual
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
./build/app/Debug/voxels_app.exe          # Windows (multi-config)
./build/app/voxels_app                    # Linux/macOS (single-config)
./build/app/Debug/voxels_app.exe --server # dedicated server
./build/editor/Debug/voxels_editor.exe    # the content editor
```

Use `--headless` only in CI or automated tests. Run the windowed application to verify desktop
runtime behavior.

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

## Contributing

Follow the ordered work items in [work_items/README.md](work_items/README.md). Use
[work_items/WORK_ITEM_PROMPT_TEMPLATE.md](work_items/WORK_ITEM_PROMPT_TEMPLATE.md) when starting
a work item, and record its completion criteria and any required content or tooling.

See [AGENT_RULES.md](AGENT_RULES.md) for engineering standards, completion requirements, and the
project workflow.

