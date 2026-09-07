# DIAGRAMS.md — VoxelsEngine Process & Flow Reference

> Companion to [ARCHITECTURE.md](ARCHITECTURE.md). These diagrams are the canonical description of *how the game runs*. Agents must read the relevant diagram before touching a subsystem, and must update the diagram in the same commit if a work item changes a flow.
>
> Legend: boxes are runtime components. "test-only" marks a path reachable solely from tests or `--headless`; "unimplemented" marks a declared but intentionally unbuilt backend.

---

## 1. System Layering & Dependency Direction

Dependencies flow strictly downward. An arrow means "may include / may call".

```mermaid
flowchart TD
    subgraph EXE["Executables"]
        APP["voxels_app<br/>main.cpp — composition + frame loop"]
        EDT["voxels_editor<br/>model authoring + asset bundler"]
    end

    subgraph APPLAYER["engine/app — Application Layer"]
        ASM["AppStateMachine + States"]
        SESS["GameSession"]
        CLI["CliParser"]
        SAVE["SaveManager"]
    end

    subgraph FEATURE["Feature Layer"]
        GP["gameplay<br/>Player, Physics, BlockInteraction, Inventory"]
        UI["ui<br/>ImGuiUIManager, Screens, HUD"]
        NET["net<br/>GameServer, GameClient, Packets"]
    end

    subgraph SERVICE["Service Layer"]
        WORLD["world<br/>Chunk, World, BlockRegistry, Mesher"]
        WGEN["worldgen<br/>Noise, Phases, Pipeline, Spawn"]
        REND["render<br/>ChunkRenderer, TextureAtlas, Camera, Frustum"]
        AUD["audio<br/>Mixer, Decoders, SoundEvents"]
        INP["input<br/>InputManager, ActionMaps"]
        AST["assets<br/>AssetManager, VpkArchive, VmdlCodec"]
    end

    subgraph HAL["Hardware Abstraction Layer"]
        PLAT["platform<br/>IPlatform → SDL2Platform, Headless is test-only"]
        GFX["graphics<br/>IRenderer + RHI → GLRenderer, Mock is test-only,<br/>Vulkan/DX12/Metal declared but unimplemented"]
        PSVC["platform_services<br/>IPlatformServices → NullPlatformServices (default),<br/>SteamPlatformServices (VOXELS_ENABLE_STEAM, optional)"]
    end

    subgraph CORE["core"]
        CR["Logger, Preferences, Math, Paths, JobSystem, Memory"]
    end

    APP --> ASM
    APP --> CLI
    EDT --> AST
    EDT --> UI
    EDT --> REND
    ASM --> SESS
    ASM --> UI
    ASM --> SAVE
    SESS --> GP
    SESS --> NET
    SESS --> WORLD
    SESS --> REND
    SESS --> SAVE
    GP --> WORLD
    GP --> INP
    NET --> WORLD
    UI --> GFX
    UI --> INP
    WORLD --> WGEN
    REND --> WORLD
    REND --> GFX
    REND --> AST
    AUD --> AST
    AUD --> PLAT
    INP --> PLAT
    SAVE --> WORLD
    SESS --> PSVC
    PLAT --> CR
    GFX --> CR
    PSVC --> CR
    WORLD --> CR
    AST --> CR
```

**Forbidden edges:** `world → graphics`, `worldgen → render`, `core → anything`. The mesher emits plain vertex data; only `render` knows about GPU buffers.

---

## 2. Boot & Shutdown Sequence

```mermaid
sequenceDiagram
    participant OS
    participant Main as main.cpp
    participant Paths
    participant Prefs as Preferences
    participant Plat as SDL2Platform
    participant Rend as GLRenderer
    participant UI as ImGuiUIManager
    participant Aud as SDLAudioDevice
    participant Assets as AssetManager
    participant SM as AppStateMachine

    OS->>Main: argc / argv
    opt VOXELS_HAS_CEF
        Main->>Main: WebUIManager.ExecuteSubprocess(argc, argv)<br/>CEF renderer/GPU/utility subprocesses return here and exit;<br/>only the browser process falls through
    end
    Main->>Main: CliParser.Parse
    Main->>Paths: Resolve user data dir + assets dir
    Main->>Prefs: Load settings.json, apply CLI overrides
    Main->>Plat: Initialize window + GL 3.3 Core context
    alt window or GL context fails
        Plat-->>Main: error
        Main->>OS: log + message box + exit(1)
    end
    Main->>Rend: Initialize with platform GL context
    Main->>Assets: Mount assets/ then packs/*.vpk
    Main->>Rend: Build texture atlas from block definitions
    Main->>UI: Initialize with platform + renderer
    Main->>Main: Construct WebUIManager as appContext.ui for<br/>menu/world-management routes; ImGui stays initialized for HUD,<br/>debug, and pause presentation only
    Main->>Aud: Open audio device, start mixer
    Main->>Main: Start JobSystem worker pool
    Main->>SM: Start BootState → transition MainMenuState
    loop every frame until quit
        Main->>Main: Frame Loop (see diagram 4)
    end
    Main->>SM: Shutdown active state (autosave if in-game)
    Main->>Aud: Stop and close device
    Main->>UI: Shutdown
    Main->>Main: WebUIManager.Shutdown()<br/>close browser, pump loop to completion, THEN CefShutdown()<br/>— before Rend teardown, or the process crashes on exit
    Main->>Rend: Release GPU resources, shutdown
    Main->>Plat: Destroy context + window
    Main->>OS: exit(0)
```

---

## 3. Application State Machine

Each state owns a full-screen UI and its own update/render behavior. No state is an empty class.

```mermaid
stateDiagram-v2
    [*] --> Boot

    Boot --> MainMenu: services initialized

    MainMenu --> WorldSelect: "Play"
    MainMenu --> JoinGame: "Join Game"
    MainMenu --> Settings: "Settings"
    MainMenu --> [*]: "Quit"

    JoinGame --> JoinLoading: "Connect — host address + UDP port"
    JoinGame --> MainMenu: "Back"

    JoinLoading --> InGame: world info received + spawn columns streamed
    JoinLoading --> ErrorScreen: reject, timeout, or download stall

    WorldSelect --> WorldCreation: "New World"
    WorldSelect --> Loading: "Load selected save"
    WorldSelect --> WorldSelect: "Delete save (confirm)"
    WorldSelect --> MainMenu: "Back"

    WorldCreation --> Loading: "Create — name, seed, options"
    WorldCreation --> WorldSelect: "Back"

    Loading --> InGame: generation complete + safe spawn found
    Loading --> ErrorScreen: generation or load failure

    InGame --> Pause: "Escape"
    InGame --> ErrorScreen: fatal runtime error

    Pause --> InGame: "Resume"
    Pause --> Settings: "Settings"
    Pause --> MainMenu: "Save and Quit" or "Leave Server"
    Pause --> [*]: "Save and Exit to Desktop"

    Settings --> MainMenu: "Back (from main menu)"
    Settings --> Pause: "Back (from pause)"

    ErrorScreen --> MainMenu: "Acknowledge"

    note right of Loading
        Drives GenerationPipeline phases
        on job workers and reports real
        progress: Shape → Caves → Ore →
        Vegetation → Lighting → Spawn
    end note

    note right of InGame
        Owns GameSession:
        server + client + world +
        player + chunk streaming +
        chunk renderer + autosave
    end note
```

---

## 4. Frame Loop — Fixed Simulation, Variable Render

```mermaid
flowchart TD
    START([Frame start]) --> DT["Measure delta time<br/>clamp to 0.25 s max"]
    DT --> POLL["Platform.PollEvents<br/>→ InputManager, UIManager, window/resize/quit"]
    POLL --> NETRX["Drain network receive queue"]
    NETRX --> ACC["accumulator += deltaTime"]

    ACC --> CHECK{"accumulator >= 1/60 s?"}
    CHECK -- yes --> SIM["Fixed simulation step — see diagram 5"]
    SIM --> DEC["accumulator -= 1/60"]
    DEC --> CHECK
    CHECK -- no --> ALPHA["alpha = accumulator / (1/60)"]

    ALPHA --> STREAM["ChunkStreamer: nearest-ring generate/load to configured radius;<br/>2 jobs max, unload clean chunks beyond radius + 2"]
    STREAM --> MESHQ["Drain completed mesh jobs → upload<br/>capped N chunks per frame"]
    MESHQ --> RENDER["Render pass chain — see diagram 6"]
    RENDER --> UIP["UI frame: state screen or HUD + debug overlay"]
    UIP --> PRESENT["Renderer.Present + vsync"]
    PRESENT --> AUDIO["Update audio listener, fire queued sound events"]
    AUDIO --> AUTOSAVE{"autosave interval elapsed?"}
    AUTOSAVE -- yes --> SAVEJOB["Enqueue async save of dirty chunks + player"]
    AUTOSAVE -- no --> END
    SAVEJOB --> END([Frame end])
```

`maxTicks` bounded loops are a **test-only** affordance behind `--headless`; the shipped app runs until the player quits.

---

## 5. Fixed Simulation Step (in-game)

```mermaid
sequenceDiagram
    participant Inp as InputManager
    participant Cam as CameraController
    participant Cli as GameClient
    participant Srv as GameServer
    participant Phy as PhysicsSystem
    participant BI as BlockInteraction
    participant W as World
    participant CR as ChunkRenderer
    participant Snd as AudioMixer

    Inp->>Cam: mouse delta → yaw / pitch
    Inp->>Cli: action states → PlayerIntent {move, jump, sneak, break, place, hotbar}
    Cli->>Srv: C2S_Input (sequence numbered)
    Cli->>Phy: predict locally with same intent

    Srv->>Phy: apply intent → velocity
    Phy->>Phy: gravity, drag, jump impulse
    Phy->>W: query solid AABBs in swept region
    Phy->>Phy: resolve per axis X, Z, Y; set onGround
    Srv->>BI: if break/place requested
    BI->>W: DDA raycast from eye, max 5 blocks
    alt hit and cooldown elapsed
        BI->>W: SetBlock (break → Air, place → held block)
        W->>W: refresh edited skylight column across resident sections
        W->>CR: mark edited chunk and touched boundaries dirty
        W->>Snd: queue block break/place sound event
        Srv->>Cli: S2C_BlockEdit broadcast
    end
    Srv->>Cli: S2C_EntityState (authoritative transform)
    Cli->>Cli: reconcile prediction against authoritative state
    Phy->>Snd: footstep event on ground-contact cadence
```

---

## 6. Render Pipeline (one frame)

```mermaid
flowchart LR
    BEGIN["Renderer.BeginFrame<br/>clear color + depth"] --> CAMU["Update camera matrices<br/>view, projection, interpolated eye"]
    CAMU --> FRUS["Build frustum, cull chunk meshes"]
    FRUS --> OPAQUE["Pass 1: opaque chunk meshes<br/>atlas texture, depth write on"]
    OPAQUE --> MODELS["Pass 2: sub-voxel models + entities<br/>.vmdl instances, players"]
    MODELS --> WATER["Pass 3: transparent<br/>water, leaves, glass — sorted back to front, depth write off"]
    WATER --> DEBUG["Pass 4: debug lines<br/>targeted block outline, chunk bounds if enabled"]
    DEBUG --> HUD["Pass 5: HUD primitives<br/>crosshair, hotbar, held item"]
    HUD --> IMGUI["Pass 6: ImGui draw data<br/>menus, settings, F3 overlay"]
    IMGUI --> WEBUI["Pass 7 (VOXELS_ENABLE_WEB_UI only): CEF React menu layer<br/>WebUiOpenGLCompositor draws the browser's premultiplied-BGRA<br/>paint as a transparent full-frame quad; menu routes replace ImGui,<br/>while pause/HUD remain native"]
    WEBUI --> PRES["Renderer.EndFrame → Present"]
```

**Chunk mesh path (data only reaches the GPU here):**

```mermaid
flowchart LR
    CHUNK["Chunk block data"] --> NEIGH["Gather 6 neighbour chunks"]
    NEIGH --> MESH["Greedy mesher — job worker<br/>emits ChunkMeshData: vertices, indices, opaque/transparent ranges"]
    MESH --> QUEUE["Completed-mesh queue"]
    QUEUE --> UPLOAD["Main thread: create/refresh VBO + IBO + VAO"]
    UPLOAD --> CACHE["ChunkRenderer mesh cache keyed by ChunkCoordinate"]
    CACHE --> DRAW["Submit DrawCall per visible chunk"]
```

---

## 7. Chunk Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Requested: player entered radius
    Requested --> LoadingFromDisk: region file has this chunk
    Requested --> Generating: no saved data
    LoadingFromDisk --> Decoded
    Generating --> PhaseShape
    PhaseShape --> PhaseCaves
    PhaseCaves --> PhaseOre
    PhaseOre --> PhaseVegetation
    PhaseVegetation --> PhaseLighting
    PhaseLighting --> Decoded
    Decoded --> Meshing: neighbours resident
    Meshing --> Uploaded: mesh job complete
    Uploaded --> Visible: passes frustum cull
    Visible --> Dirty: block edited
    Dirty --> Meshing: re-mesh queued
    Visible --> Unloading: player left radius + hysteresis
    Uploaded --> Unloading: player left radius + hysteresis
    Unloading --> Saved: dirty chunks flushed to region file
    Saved --> [*]
```

Hysteresis: load radius = render distance, unload radius = render distance + 2, so a player standing on a boundary does not thrash.

---

## 8. World Generation Pipeline

```mermaid
flowchart TD
    SEED["World seed (u64) + WorldOptions"] --> HASH["Deterministic per-chunk hash<br/>seed ⊕ chunkX ⊕ chunkY ⊕ chunkZ"]
    HASH --> P1["Phase 1 — Shape<br/>global-domain warped noise + climate biome sample → terrain and sea fill"]
    P1 --> P2["Phase 2 — Caves<br/>global 3D ridged tunnel/cavern fields, protected below ocean floors"]
    P2 --> P3["Phase 3 — Ore<br/>global 3D depth-banded coal/iron cluster fields"]
    P3 --> P4["Phase 4 — Vegetation<br/>world-coordinate tree anchors sampled by every affected chunk"]
    P4 --> P5["Phase 5 — Lighting<br/>skylight assigned to detached chunk data"]
    P5 --> DONE["Chunk ready → Decoded"]

    DONE --> SPAWN{"Is this the spawn search?"}
    SPAWN -- yes --> SC["SpawnCalculator: scan spiral outward from origin<br/>find highest solid non-water column<br/>verify player AABB clearance"]
    SC --> PLACE["Player spawn position"]
    SPAWN -- no --> IDLE([done])
```

**Determinism contract:** running the pipeline twice with the same seed and chunk coordinate must produce byte-identical chunk data, regardless of thread count or generation order. Worker jobs return detached chunks; only the main thread inserts ready chunks into `World`. This is enforced by automated test.

---

## 9. Input Flow

```mermaid
flowchart TD
    SDL["SDL2 events: key, mouse, wheel, controller"] --> PLATE["IPlatform.PollEvents"]
    PLATE --> LISTEN["Dispatch to IPlatformEventListener list"]
    LISTEN --> UIL["IPlayerUI backend<br/>React/CEF menus when enabled;<br/>ImGui HUD/pause"]
    LISTEN --> IM["InputManager"]
    IM --> MAP["Action map from Preferences<br/>MoveForward, Jump, Break, Place, Hotbar1-9, Pause"]
    MAP --> CTX{"PlayerUIInputPolicy"}
    CTX -- Overlay or TextEntry --> UIACT["UI navigation, cursor visible, gameplay events blocked"]
    CTX -- Gameplay --> GACT["PlayerIntent, cursor hidden, relative mouse mode"]
    GACT --> SIMSTEP["Fixed simulation step"]
    UIACT --> SCREEN["Active state screen"]
```

`IPlayerUI` accepts immutable route snapshots and emits bounded, versioned envelopes containing only
`{version, kind, requestId, payload}`. The native adapter consumes one mouse transition delta when
returning to `Gameplay`; `Overlay` and `TextEntry` block every gameplay input event. Mouse capture
is owned by this policy: `Gameplay` enables relative mode and hides the cursor, while the other
policies release it. Alt-Tab must never leave the cursor trapped.

---

## 10. Persistence Flow

```mermaid
flowchart TD
    subgraph SAVEP["Save"]
        S1["Trigger: autosave timer, Save and Quit, or world unload"] --> S2["Collect dirty chunk sections"]
        S2 --> S3["Serialize chunks: RLE per section"]
        S3 --> S4["Group into 32×32 region files"]
        S4 --> S5["Write temp file → flush → atomic rename"]
        S5 --> S6["Write player.dat and level.json the same way"]
        S6 --> S7["Clear dirty flags"]
    end

    subgraph LOADP["Load"]
        L1["WorldSelect: enumerate userdata/saves/*"] --> L2["Read level.json, check schema version"]
        L2 --> L3{"Version supported?"}
        L3 -- no --> L4["Migrate or reject with a clear message"]
        L3 -- yes --> L5["Restore seed + WorldOptions into GenerationPipeline"]
        L5 --> L6["Load player.dat → spawn player"]
        L6 --> L7["ChunkStreamer requests chunks; region hit → decode, miss → generate"]
    end

    ROOT["User data root<br/>Windows %LOCALAPPDATA%/VoxelsEngine<br/>Linux $XDG_DATA_HOME/VoxelsEngine<br/>macOS ~/Library/Application Support/VoxelsEngine"] --> VERSION["saves/v1 format namespace"]
    VERSION --> SAVEP
    VERSION --> LOADP
    ROOT --> CFG["settings.json — preferences and key bindings"]
    ROOT --> LOG["logs/voxels_TIMESTAMP.log"]
```

---

## 11. Content Pipeline — Editor to Game

```mermaid
flowchart LR
    subgraph AUTHOR["voxels_editor — authoring"]
        A1["Sub-voxel model editor<br/>16×16×16 micro-voxel grid + slice editing<br/>undo/redo and recovery"] --> A2["Palette + texture assignment per face/voxel"]
        A2 --> A3["Pivot, collision bounds, attachment points"]
        A3 --> A4["Export .vmdl"]
        T1["Texture import: 16×16 or 32×32 PNG"] --> T2["Atlas preview + UV assignment"]
        A4 --> B0
        T2 --> B0
        D1["Block definition editor<br/>block_id → faces, model_id, hardness, sounds"] --> B0
        S1["Audio import: WAV/OGG"] --> B0
    end

    B0["Bundler stage<br/>validate → deduplicate → deterministic order → CRC32"] --> VPK["core.vpk<br/>validated VPK1 TOC + blobs + manifest.json"]

    VPK --> APPDIR["app/assets/packs/core.vpk"]

    subgraph RUNTIME["voxels_app — runtime"]
        APPDIR --> M1["AssetManager mounts packs at boot"]
        M1 --> M2["Resolution order:<br/>1. loose assets/ (dev override)<br/>2. mounted .vpk<br/>3. procedural placeholder + WARN + ASSET_REQUESTS entry"]
        M2 --> M3["TextureAtlas built from block face textures"]
        M2 --> M4["VmdlCodec → ModelRegistry → cached culled micro-voxel meshes"]
        M2 --> M5["Sound bank → AudioMixer"]
        M3 --> M6["ChunkRenderer + model renderer"]
        M4 --> M6
    end
```

**Association rule:** a block type gains custom geometry purely through data — `blocks.json` sets `model_id`, and the runtime looks up the `.vmdl` in the mounted pack. No code change is required to add a block shape.

---

## 12. Client / Server Topology

```mermaid
flowchart TD
    subgraph SOLO["Singleplayer — one process"]
        SC1["GameClient"] <-->|"UDP 127.0.0.1"| SS1["GameServer (authoritative)"]
        SS1 --> SW1["World + GenerationPipeline + SaveManager"]
    end

    subgraph LAN["Hosted LAN — visibility set to public"]
        HC["Host GameClient"] <-->|loopback| HS["GameServer bound to 0.0.0.0"]
        RC1["Remote GameClient"] <-->|UDP| HS
        RC2["Remote GameClient"] <-->|UDP| HS
        HS --> HW["World"]
    end

    subgraph DED["Dedicated — voxels_app --server"]
        DS["Headless GameServer"] --> DW["World"]
        DC1["GameClient"] <-->|UDP| DS
    end
```

Packet flow per connection:

```mermaid
sequenceDiagram
    participant C as GameClient
    participant S as GameServer
    C->>S: C2S_Connect {protocolVersion, clientKind}
    alt full, not ready, or private (non-loopback)
        S-->>C: S2C_Reject {reason}
    else accepted
        S-->>C: S2C_ConnectAck {playerId, worldReady, worldInfo: seed/version/flags/spawn}
    end
    loop world download (remote clients)
        S-->>C: S2C_ChunkData {coord, fragment i/n, totalBytes, RLE slice}
        C->>S: C2S_ChunkAck {coord}
        Note over S: unacked chunks retransmit after 350 ms
    end
    loop each simulation tick
        C->>S: C2S_PlayerMove {position, rotation, velocity}
        S-->>C: S2C_EntityState (broadcast, delta-validated)
    end
    C->>S: C2S_BlockModify {position, blockId}
    S-->>C: S2C_BlockUpdate (broadcast after reach validation)
    C->>S: C2S_KeepAlive (every 2 s)
    S-->>C: S2C_KeepAliveAck
    C->>S: C2S_Disconnect
    S-->>C: S2C_PlayerLeft (broadcast) / S2C_Disconnect {reason}
```

---

## 13. Audio Flow

```mermaid
flowchart LR
    EV["Gameplay events<br/>break, place, footstep, jump, ambient"] --> Q["Sound event queue (main thread)"]
    Q --> MIX["Mixer: resolve preloaded WAV clip,<br/>apply 3D attenuation vs listener, volume from Preferences"]
    MIX --> RING["Lock-free ring buffer"]
    RING --> CB["SDL audio callback thread<br/>mix voices → device buffer"]
    CB --> OUT([Speakers])
    LIS["Camera transform → listener position/orientation"] --> MIX
    MUS["Music loop — preloaded PCM; OGG streaming pending"] --> MIX
```

The audio callback never allocates, never locks, and never touches game state directly.

---

## 14. Packaging, Versioning & Platform Services

```mermaid
flowchart TD
    GIT["git rev-parse --short HEAD<br/>+ working-tree dirty check"] --> GEN["cmake/GenerateVersionHeader.cmake<br/>(custom target, reruns every build)"]
    PVER["project(VoxelsEngine VERSION X.Y.Z)"] --> GEN
    GEN --> VH["generated_include/voxels/core/version.hpp<br/>kEngineVersion, kEngineGitCommit, kEngineBuildTimestamp"]
    VH --> TITLE["Window title (main.cpp)"]
    VH --> MENU["Main-menu corner text"]
    VH --> LOG["Startup log banner"]
    VH --> SAVE["GameSave.engineVersion default"]
    VH --> MANIFEST["VPK manifest.json engine_compatibility<br/>(AssetBundler)"]

    subgraph BUILD["cmake --build"]
        APPTGT["voxels_app"]
        WEBASSET["voxels_web_ui_assets<br/>npm ci → Vite hashed bundle → ui-manifest.json"]
        CEFPIN["Verified CEF archive<br/>SHA-256 checked before extraction"]
    end
    subgraph PACKAGE["cmake --install / cpack"]
        LAYOUT["voxels_app(.exe) + SDL2/Steam runtime + assets/<br/>+ README.txt + LICENSE.txt + THIRD_PARTY_LICENSES.txt"]
    end
    APPTGT --> LAYOUT
    WEBASSET --> APPTGT
    CEFPIN --> LAYOUT
    TPL["packaging/README.txt.in<br/>packaging/THIRD_PARTY_LICENSES.txt.in"] --> LAYOUT

    FR{"settings.json exists?"} -- no: first run --> DEFAULTS["Write default GamePreferences<br/>create user-data directory tree"]
    DEFAULTS --> CARD["MainMenuState pushes ControlsCardState overlay"]
    CARD -- "Got it" --> SEEN["controlsCardSeen = true, persisted"]
    FR -- yes --> MENUSTATE["MainMenuState (no card unless controlsCardSeen is false)"]

    subgraph PSVC["IPlatformServices"]
        NPS["NullPlatformServices (default)"]
        SPS["SteamPlatformServices<br/>(VOXELS_ENABLE_STEAM=ON + SDK present)"]
    end
    GS["GameSession"] -- "break/place/cave events" --> PSVC
    PSVC --> ACH["Achievements: FirstBlockBroken, FirstWorldCreated,<br/>FirstCaveEntered, FirstStructureBuilt"]
    SPS --> CLOUD["SaveManager save directory → Steam Cloud<br/>(level.json, player.dat mirrored on save/load)"]
```

Achievement triggers are real gameplay signals, not direct calls from menu code: `GameSession`
unlocks `FirstBlockBroken` when a break completes, `FirstCaveEntered` when the player's block
position has zero sky light with a solid roof two blocks overhead, and `FirstStructureBuilt`
when a placement lands with three or more solid orthogonal neighbours already present.
`FirstWorldCreated` fires from `WorldCreationState` when a new save is actually persisted (not
when loading an existing one).
