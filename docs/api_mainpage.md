# VoxelsEngine API Reference {#mainpage}

This is the generated API reference for **VoxelsEngine** — the C++20 engine, the shipped voxel
sandbox game (`voxels_app`), and the internal content editor (`voxels_editor`).

It is produced from the Doxygen comments in the source tree by the `docs` build target:

```
cmake --build build --target docs      # output: build/docs/html/index.html
```

For the *narrative* documentation — system design, ADRs, subsystem status, and diagrams — start
with `ARCHITECTURE.md` and `DIAGRAMS.md` in the repository root. This reference is the
symbol-level companion to those documents.

## Module Map

The engine is a single static library (`voxels_engine`) with strictly layered modules. Public
headers live under `engine/include/voxels/<module>/`.

| Module | Namespace path | Responsibility |
| :--- | :--- | :--- |
| **core** | `voxels/core/` | Logging, math (GLM aliases + grid conversions), job system, pool allocators, preferences, user-data paths, save/game types, generated version info |
| **platform** | `voxels/platform/` | `IPlatform` window/input/event abstraction — SDL2 (desktop), Headless (tests/CI), Dreamcast (declared); `IPlatformServices` (Null/Steam) |
| **graphics** | `voxels/graphics/` | `IRenderer` RHI interface and backends: OpenGL 3.3 Core (shipping), Mock (tests), Vulkan/DX12/Metal/PVR (declared, ADR-001/ADR-013) |
| **render** | `voxels/render/` | Engine-side rendering: camera, texture atlas + procedural texture forge, chunk vertex format, greedy mesher, chunk renderer, HUD, model registry, remote player rendering |
| **world** | `voxels/world/` | Blocks and the block registry, chunks (storage, light, RLE), the `World` spatial hash, raycasting, noise, the generation pipeline, spawn calculation, serialization |
| **gameplay** | `voxels/gameplay/` | Player entity, AABB physics, camera controller, block interaction, inventory, validated transactional crafting |
| **input** | `voxels/input/` | Action mapping, devices, and the input manager (platform events → gameplay actions) |
| **ui** | `voxels/ui/` | UI scale/metrics management and the Dear ImGui integration layer |
| **audio** | `voxels/audio/` | `IAudioEngine`, SDL audio playback, procedural tone synthesis, spatial attenuation |
| **assets** | `voxels/assets/` | Async asset manager, VPK pack archive format and bundler, texture loading, VMDL sub-voxel model codec |
| **networking** | `voxels/networking/` | UDP protocol packets, reliable channel, client/server sessions, chunk streaming |
| **app** | `voxels/app/` | Application shell: CLI parsing, app-state machine, menus, save manager, the `GameSession` simulation façade, network sync |
| **engine.hpp** | `voxels/` | The `Engine` orchestrator that owns platform bring-up and subsystem lifecycle |

## Executables

| Target | Source root | Purpose |
| :--- | :--- | :--- |
| `voxels_app` | `app/src/` | The shipping game: full desktop runtime, also `--server` dedicated mode |
| `voxels_editor` | `editor/src/` | Internal tool: sub-voxel model authoring, texture assignment, pack bundling |
| `voxels_tests` | `tests/` | Catch2 v3 suite run through CTest (not part of this reference) |

## Layering Rules (short form)

Dependencies point strictly downward; `world` never includes `graphics`/`render`; `render` never
includes `networking`; nothing includes `app` except the executables. The full contract, with
ADRs and the per-subsystem status table, is in `ARCHITECTURE.md`.
