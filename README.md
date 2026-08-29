# Voxels Engine and Game

This repository contains the initial architecture for a cross-platform voxel engine, game client, and editor tool. The design is intentionally layered so the engine can support Windows, Linux, macOS, and a future Dreamcast/KallistiOS platform without coupling gameplay logic to platform-specific code.

## Tech stack

- C++20 for the engine and game runtime
- SDL2 for windowing, events, and platform abstraction
- GLM for math and transform calculations
- Dear ImGui for editor tooling and debug overlays
- Standalone Asio for networking and socket abstraction
- CMake for build orchestration and cross-compilation support
- Vulkan, DirectX 12, and Metal renderer backends are scaffolded behind a shared renderer interface
- Steamworks SDK is prepared through CMake download or package-manager integration for later shipping support

## Package strategy

The initial dependency strategy is deliberately conservative:

- SDL2 is the platform foundation and event source.
- GLM is the core math layer for world transforms, camera matrices, and collision logic.
- Dear ImGui is reserved for the editor and debug tools.
- Standalone Asio is the networking library of choice because it is mature, cross-platform, and integrates cleanly with modern C++ without the overhead of a large application framework.
- Steamworks is included as an optional dependency path for platform features and online services later in the project.

## Build instructions

### Configure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

### Build

```bash
cmake --build build
```

### Run the game

```bash
./build/app/voxels_app --fullscreen=false
```

### Run the editor

```bash
./build/editor/voxels_editor
```

## Platform notes

The platform layer is abstracted to allow the same engine structure to support:

- Windows with DirectX 12 or Vulkan
- Linux with Vulkan
- macOS with Metal
- Dreamcast with KallistiOS in a later port

The networking layer is not restricted to a single transport path and should remain usable across desktop and console targets.

## Project layout

```text
voxels/
├── CMakeLists.txt
├── README.md
├── .vscode/
├── engine/
├── app/
├── editor/
└── build/
```
