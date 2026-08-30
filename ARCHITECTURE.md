# ARCHITECTURE.md — VoxelsEngine Technical Architecture

> **Status:** Authoritative. This document describes the system as it **must** be built, and honestly marks what is currently real versus unimplemented. It replaces the original scaffolding brief, which described a greenfield prompt rather than a system.
>
> **Companion documents:** [DIAGRAMS.md](DIAGRAMS.md) (process/state flow diagrams), [AGENT_RULES.md](AGENT_RULES.md) (engineering standards), [work_items/README.md](work_items/README.md) (execution roadmap), [ASSET_REQUESTS.md](ASSET_REQUESTS.md) (content the human operator must supply).

---

## 🚫 Naming Constraint

Never reference the competing commercial block-game title by name in code, comments, docs, commit messages, identifiers, or tests. Use "voxel world", "block-based terrain", "sub-voxel geometry", "voxel engine".

---

## 1. Product Definition (what "done" means)

`voxels_app` is a **first-person, block-based survival-sandbox game**. The delivered product, launched from a built executable by a human with no developer tooling, must:

1. Open a real, resizable, GPU-accelerated window at the configured resolution.
2. Present a **rendered main menu**: Play, Settings, Quit.
3. Let the player **create a new world** (name, seed, options) or **load/delete an existing one**.
4. Show a **loading screen with real generation progress**, then spawn the player standing safely on the terrain surface.
5. Render a **textured, lit voxel world** with terrain, caves, water, and trees, streaming chunks around the player at an interactive frame rate.
6. Accept **mouse-look + WASD + jump**, apply **gravity and AABB collision**, and allow **breaking and placing blocks** with a highlighted target block, crosshair, and hotbar.
7. Play **audio**: ambient music, block break/place, footsteps, with volumes driven by settings.
8. Support **Escape → pause menu** → Resume / Settings / Save & Quit, with settings applied live.
9. **Persist the world and player** to the OS user-data directory and resume exactly where the player left off.
10. Exit cleanly with zero leaks, zero crashes, and no console-only behavior.

Anything less than the above is an unfinished product, regardless of unit test count.

`voxels_editor` is an **internal content-authoring tool**: it authors sub-voxel models (`.vmdl`), paints/assigns textures, associates models with block/item types, and **bundles** the whole content set into `.vpk` asset packs that `voxels_app` consumes.

---

## 2. Current Reality Check (as of this realignment)

| Layer | State | Notes |
| :--- | :--- | :--- |
| Core (logger, prefs, math, paths) | Real | Usable as-is. |
| World storage (chunk, RLE serialization, raycast, block registry) | Real | 16³ chunk sections in a sparse map; data-driven catalogue (`blocks.json`) with 14 launch blocks. |
| World generation (Perlin 2D/3D, shape → caves → vegetation → skylight) | Real | Deterministic, seeded, phase-pluggable. Vertical chunk sections beyond the one generated at load are not yet stitched together (item 12). |
| Textures & Atlas | Real | STB decoders, `TextureLoader`, `TextureForge`, `TextureAtlas` (GL_TEXTURE_2D_ARRAY), `--dump-atlas`. |
| Chunk mesher (`render/chunk_mesher.cpp`) | Real | Neighbour-aware, greedy-merged, AO + sky/block light + transparent-range split; consumed by `ChunkRenderer` and **reaches the GPU every frame**. |
| `ChunkRenderer` (`render/chunk_renderer.cpp`) | Real | Job-scheduled meshing, budgeted upload, frustum-culled opaque/transparent draw. Wired into `InGameState`. |
| Physics / block interaction / camera math | Partial | Player physics and first-person camera control run in the fixed gameplay step; block interaction remains for item 07. |
| Input manager | Real | SDL keyboard and relative mouse events feed the player action map in the desktop runtime. |
| Networking (Asio UDP client/server) | Real | Ticks in `main.cpp` but carries no gameplay traffic. |
| Audio | Partial | Generates PCM; **no output device, nothing is audible**. |
| Asset manager / `.vpk` | Partial | Archive I/O + procedural placeholders; image decoders active. |
| Platform / SDL2 | Real | Desktop build with real SDL2 window and GL 3.3 Core context. |
| **Renderer** | Real | GL 3.3 Core renderer with textured block rendering via 2D array texture atlas. |
| **UI** | **Absent** | `UIManager` computes a DPI scale and nothing else. Dear ImGui is not a dependency of this project. |
| **App states** | Partial | `BootState`, `MainMenuState` render a clear sky (no menu UI yet - item 08/09). `InGameState` owns a `GameSession` with first-person WASD/mouse-look, jumping, collision, generated terrain, and `ChunkRenderer` rendering. |
| Editor | Stub | Prints one line and exits. |
| App assets | Real | 15 launch block textures (16×16 PNG), `blocks.json`, server/default configs, shaders. |

**Root cause diagnosis:** the project optimized for *testable isolation* and treated "mock/procedural fallback" as an acceptable terminal state. Every subsystem got a `Mock*`/`Headless*`/procedural implementation that satisfied its unit tests, so no work item was ever forced to produce pixels, sound, or interaction. The remediation is structural, not cosmetic: **mock implementations become test-only fixtures, and the shipped desktop configuration must use real backends or fail the build.**

---

## 3. Architectural Decisions (ADRs)

These are binding. Do not silently deviate; if a work item requires deviating, say so explicitly in the completion report.

| # | Decision | Rationale |
| :--- | :--- | :--- |
| **ADR-001** | **OpenGL 3.3 Core is the reference/primary renderer backend.** Vulkan, DX12, Metal, and PowerVR2 remain declared behind the RHI but are **not** implemented until the game ships and profiles. | One backend that actually works on Windows/Linux/macOS beats four that don't. GL 3.3 Core covers all desktop targets and maps cleanly onto the existing `IRenderer`/RHI contracts. |
| **ADR-002** | **SDL2 is a hard dependency of the desktop build.** If `find_package` fails, CMake **fetches and builds it**. `voxels_app` must never silently fall back to `HeadlessPlatform`. | The current silent fallback is the reason a "successful build" produces a black window. Headless is a *test fixture*, selected only by an explicit `--headless` flag. |
| **ADR-003** | **Dear ImGui is the UI system** for menus, settings, debug overlays, and the entire editor. Gameplay HUD elements that must not depend on ImGui (crosshair, hotbar, block highlight) are drawn with engine render primitives. | ImGui is already assumed by the architecture, is trivially vendorable, and unblocks the editor quickly. |
| **ADR-004** | **Mock/Headless/procedural implementations are test-only.** They sit behind `IRenderer`/`IPlatform`/`IAudioDevice` and may only be selected by tests or an explicit `--headless` flag. | Prevents the exact failure mode that produced this realignment. |
| **ADR-005** | **Fixed 60 Hz simulation, decoupled variable-rate rendering** via an accumulator plus render interpolation. | Deterministic physics and networking, smooth visuals on any refresh rate. |
| **ADR-006** | **Singleplayer is a hosted server.** The client always talks to a `GameServer`; solo play binds it to loopback. There is no separate "offline" gameplay path. | Already the intent; keeps multiplayer from becoming a rewrite. |
| **ADR-007** | **Simulation state is server-authoritative.** The client owns camera, input prediction, and rendering only. | One source of truth; cheat resistance. |
| **ADR-008** | **Generation and meshing run on a worker thread pool**; results return to the main thread through guarded result queues. GPU uploads happen only on the render thread. | Chunk streaming must never stall a frame. |
| **ADR-009** | **World geometry:** chunk sections are `16×16×16`. World height is `Y ∈ [0, 256)` (16 sections per column). Sections live in a sparse map keyed by `ChunkCoordinate`. | Matches existing code; bounded height simplifies lighting, meshing, and save layout. |
| **ADR-010** | **Sub-voxel models are `16×16×16` micro-voxels per block unit**, serialized as `.vmdl`, bundled into `.vpk`. Block definitions reference models by stable string id. | One consistent authoring resolution for stairs, doors, items, tools, and creatures. |
| **ADR-011** | **User data lives in the OS user-data directory**, never beside the executable: Windows `%LOCALAPPDATA%\VoxelsEngine\`, Linux `$XDG_DATA_HOME/VoxelsEngine/`, macOS `~/Library/Application Support/VoxelsEngine/`. Shipped read-only content lives beside the executable in `assets/`. | Required by the product brief and by OS install conventions. |
| **ADR-012** | **Content resolution order:** loose files under `assets/` (developer override) → mounted `.vpk` packs → procedural placeholder. A placeholder used at runtime **must** log a warning and be recorded in [ASSET_REQUESTS.md](ASSET_REQUESTS.md). | Keeps builds green without letting missing content go unnoticed. |
| **ADR-013** | **Dreamcast/KallistiOS and console SDKs are aspirational.** Preserve the abstraction seams; spend no work-item budget on them before the desktop game ships. | Scope discipline. |

---

## 4. Module Map

```
voxels/
├── engine/            # voxels_engine static library — everything reusable
│   ├── core/          # Logger, Preferences, Math, Memory, GameTypes, JobSystem, Paths
│   ├── platform/      # IPlatform: SDL2 (desktop, required), Headless (tests), Dreamcast (stub)
│   ├── graphics/      # IRenderer + RHI; gl/ (real GL 3.3 backend); mock/ (tests); vulkan|dx12|metal (declared, unimplemented)
│   ├── render/        # Higher level: ChunkRenderer, TextureAtlas, MaterialSystem, Camera, Frustum, DebugDraw
│   ├── ui/            # IUIManager + ImGui backend, screens, HUD primitives
│   ├── input/         # InputManager, action maps, device backends, mouse capture
│   ├── world/         # Block registry, Chunk, World, mesher, serialization
│   ├── worldgen/      # Noise, generation phases, pipeline, spawn calculation
│   ├── gameplay/      # Player, physics, block interaction, inventory, camera controller
│   ├── audio/         # IAudioDevice (SDL backend), mixer, decoders, sound events
│   ├── assets/        # AssetManager, .vpk pack reader/writer, .vmdl model codec
│   ├── net/           # Packets, GameServer, GameClient, replication
│   └── app/           # CLI parsing, AppStateMachine + states, SaveManager, GameSession
├── app/               # voxels_app executable — thin orchestration + shipped assets/
├── editor/            # voxels_editor executable — model authoring + asset bundling
└── tests/             # Catch2 suites, one per subsystem plus integration suites
```

**Dependency rule (enforced):** `app` and `editor` depend on `engine`. Engine layers depend downward only:

`app-state → gameplay | ui | net → world | render | audio | input → platform | graphics-RHI → core`

No upward includes, no cycles. `world/` and `worldgen/` must never include `graphics/` — the mesher emits plain data, the renderer consumes it.

---

## 5. Runtime Composition

`main.cpp` owns nothing except composition and the frame loop:

```
Engine
 ├── Paths            (user data dir, assets dir)
 ├── Preferences      (loaded from user dir, overridden by CLI)
 ├── IPlatform        (SDL2Platform: window + GL context + events)
 ├── IRenderer        (GLRenderer bound to the platform's GL context)
 ├── IUIManager       (ImGuiUIManager bound to platform + renderer)
 ├── IAudioDevice     (SDLAudioDevice + Mixer)
 ├── InputManager     (bound to platform events, action maps from Preferences)
 ├── AssetManager     (mounts assets/ then *.vpk)
 ├── JobSystem        (worker pool for generation + meshing)
 └── AppStateMachine  (Boot → MainMenu → … → InGame → …)
       └── GameSession (owned by InGameState)
             ├── GameServer  (loopback or dedicated)
             ├── GameClient
             ├── World + ChunkStreamer + GenerationPipeline
             ├── Player + PhysicsSystem + BlockInteraction
             ├── ChunkRenderer (mesh cache + GPU buffers)
             └── SaveManager
```

Every state receives an `AppContext&` holding non-owning references to the services above. **No state may be an empty class** — see [AGENT_RULES.md](AGENT_RULES.md) § Anti-Shell Rule.

---

## 6. Key Subsystem Contracts

### 6.1 Graphics / RHI
- `IRenderer`: `Initialize(IPlatform&)`, `BeginFrame`, `EndFrame`/`Present`, resource creation (`CreateBuffer`, `CreateTexture`, `CreateShader`, `CreatePipeline`), `Submit(DrawCall)`, `SetViewport`, `Clear`.
- `GLRenderer` implements this against GL 3.3 Core. Shaders live in `app/assets/shaders/*.glsl` with a compiled-in fallback source string so a missing file never blanks the screen.
- Passes per frame, in order: **opaque chunks → sub-voxel models/entities → transparent (water) → debug lines → UI**.

### 6.2 World & Meshing
- `World` owns sparse `Chunk` sections plus the block registry.
- `render/chunk_mesher.cpp` consumes a chunk and its six neighbours and emits `ChunkMeshData { vertices, indices, opaqueIndexCount, transparentIndexCount, provisional }`. It lives under `render/` (not `world/`) because it also depends on `TextureAtlas`; `world/` itself still never includes `graphics/`.
- `ChunkRenderer` owns the `chunkCoord → GpuChunkMesh` cache, the dirty rebuild queue (mesh jobs run on `JobSystem` workers per ADR-008; only GPU upload/draw happen on the render thread), frustum culling, and draw submission (opaque front-to-back, then transparent back-to-front).
- A neighbour that is not yet resident is treated as occluding (no face drawn) rather than exposing a face, and the mesh is marked `provisional` so it is automatically re-queued once the neighbour loads - this avoids ever drawing a "wall of faces" at an unloaded seam.
- `ChunkRenderer::MarkBlockEdited` marks the owning chunk dirty plus any neighbour whose boundary the edit touched (interior edits dirty 1 chunk, corner edits dirty at most 4); not yet called by gameplay code since block breaking/placing is work item 07.

### 6.3 Generation
- `GenerationPipeline` is an ordered list of `IGenerationPhase`: **Shape → Caves → Ore → Vegetation → Lighting**. Phases are pure functions of `(seed, chunkCoord, chunkData)` so generation is deterministic and parallelizable.
- `SpawnCalculator` finds the highest non-water solid surface within a search radius and returns a position where the player AABB is unobstructed.

### 6.4 Gameplay
- Fixed 60 Hz step: sample input → build intent → server applies (movement, physics, block edits) → replicate → client interpolates → render.
- The player is an AABB (`0.6 × 1.8 × 0.6`) with gravity, jump impulse, per-axis swept-AABB resolution, and ground/step detection.
- `BlockInteraction` raycasts from the eye (max 5 blocks) using DDA voxel traversal, returning the hit block plus face for break/place.

### 6.5 Persistence
Save root: `<userdata>/saves/<world_name>/`

| File | Contents |
| :--- | :--- |
| `level.json` | Schema version, display name, seed, world options, created/last-played timestamps, spawn point. |
| `player.dat` | Position, velocity, yaw/pitch, health, hotbar/inventory. |
| `regions/r.<rx>.<rz>.bin` | RLE-compressed chunk sections grouped into 32×32-chunk regions. |

All writes are **atomic** (write temp → flush → rename). Every file carries a schema version; loaders migrate or refuse cleanly, and never crash on malformed input.

### 6.6 Assets & Content Pipeline
Authoring (editor) → `.vmdl` models + textures + audio → **bundler** → `.vpk` pack + manifest → shipped in `app/assets/packs/` → mounted by `AssetManager` at boot.

- `.vpk` layout: magic `VPK1`, header (version, entry count, TOC offset), TOC entries (`path`, `offset`, `size`, `uncompressedSize`, `crc32`, `type`), then blobs. Entry ordering is deterministic so packs are byte-reproducible.
- Block definitions (`blocks.json` inside the pack) map `block_id → { display_name, solid, transparent, atlas faces, optional model_id, hardness, sounds }`. This is the seam that associates an editor-authored model with a block type.

### 6.7 Networking
- UDP over Asio. Packet types: `C2S_Handshake`/`Input`/`BlockEdit`, `S2C_Accept`/`ChunkData`/`EntityState`/`BlockEdit`/`Disconnect`.
- Loopback in solo play; the same server accepts LAN clients when world visibility is set to public.

---

## 7. Threading Model

| Thread | Responsibilities |
| :--- | :--- |
| **Main / render** | Platform events, input, simulation step, GPU resource creation and upload, draw submission, present, UI. |
| **Job workers** (`hardware_concurrency - 1`) | Chunk generation phases, chunk meshing, save serialization, asset decode. |
| **Network** | Asio `io_context`; delivers packets into a thread-safe queue drained on the main thread. |
| **Audio** | SDL audio callback; pulls from a lock-free mixer ring buffer. Never allocates. |

Rule: **no GL call, no `Chunk` mutation, and no ImGui call outside the main thread.** Workers operate on snapshots and return results through queues.

---

## 8. Performance Budget

Target: 60 FPS at 1080p on a mid-range GPU with render distance 8.

| Item | Budget |
| :--- | :--- |
| Frame total | 16.6 ms |
| Simulation + physics | ≤ 2 ms |
| Chunk mesh uploads | ≤ 2 ms/frame (rate-limited, capped chunks per frame) |
| Draw submission + GPU | ≤ 10 ms |
| UI | ≤ 1.5 ms |
| Chunk generation | Off-thread; never blocks a frame |
| Memory | ≤ 1 GB resident at render distance 8 |

Chunk streaming must be **rate-limited**, not burst-loaded — a hitch is a bug.

---

## 9. Error Handling & Observability

- **Fail loudly at startup:** missing GL context, unusable shader, missing audio device → visible error surface (message box or on-screen error state), a log line, and a non-zero exit. Never a silent degraded mode.
- **Fail softly at runtime:** a missing texture or sound resolves to a placeholder, logs a warning, and is recorded in [ASSET_REQUESTS.md](ASSET_REQUESTS.md).
- Logs write to `<userdata>/logs/voxels_<timestamp>.log` and mirror to stdout in debug builds.
- Debug overlay (F3): FPS/frame time, position and chunk coordinate, loaded/meshed chunk counts, draw calls, triangle count, memory, generation queue depth.

---

## 10. Platform Matrix

| Platform | Window | Renderer | Status |
| :--- | :--- | :--- | :--- |
| Windows x64 | SDL2 | OpenGL 3.3 Core | **Primary — must work** |
| Linux x64 | SDL2 | OpenGL 3.3 Core | Supported |
| macOS | SDL2 | OpenGL 4.1 Core (3.3 feature subset) | Supported |
| Headless (CI/tests) | HeadlessPlatform | MockRenderer | Test-only |
| Dreamcast / consoles | KallistiOS / vendor SDK | PowerVR2 / native | Abstraction seams only, unimplemented |

---

## 11. Definition of "Production Grade"

A subsystem is production grade when **all** of the following hold:

1. It is reachable and observable by a human running the shipped executable.
2. It has automated tests asserting real side effects, not merely that a function was callable.
3. It has no `Mock`/`Headless`/`TODO`/placeholder on its shipping code path.
4. It handles its own failure modes explicitly (missing file, allocation failure, disconnected device).
5. It is documented here or in [DIAGRAMS.md](DIAGRAMS.md), with any ADR deviation called out.
6. It builds with zero warnings under MSVC `/W4` and GCC/Clang `-Wall -Wextra -Wpedantic`.
