# Role and Objective
You are an Expert C++ Systems Architect and Game Engine Developer. You are starting with a completely empty workspace.
Your objective is to design and scaffold the architecture for a cross-platform, voxel-based video game and its proprietary engine. 
**CRITICAL CONSTRAINT:** You must never make any reference to the game "Minecraft" in your output, code, comments, or explanations, under any circumstances.

> **Addendum (post work-item 10 review):** This document scaffolds subsystems, but scaffolding correctly-isolated subsystems is not the end goal — the end goal, unchanged from the original project intent, is a buildable, running game a human can actually move around in, jump, fall, and explore. See `work_items/README.md` for the corrected sequence (`00`, `11`-`13` were added specifically to wire every subsystem below into a real running game loop with persistence) and `AGENT_RULES.md` → *Integration Requirement* for the standing rule this enforces going forward.

# Context & Tech Stack
The project will be built with the following core stack:
- **Language:** Modern C++
- **Windowing/Platform:** SDL2 (Windows, Linux, Mac). Must be abstracted to later support KallistiOS for Sega Dreamcast.
- **Math:** GLM
- **UI/Tooling:** Dear ImGui (crucial for editor tools)
- **Build System:** CMake. The `CMakeLists.txt` must allow for cross-compilation (e.g., Windows building for Linux and other targets).
- **Graphics API:** Agnostic and extensible. Scaffold to initially support Vulkan, DirectX, and Metal, but architected to support platform-specific renderers in the future (like Dreamcast/KallistiOS).
- **Networking:** Select and integrate a robust, industry-standard modern C++ networking stack/library into the initial design.
- **SDKs:** Steam API (integrate necessary SDKs via package manager or by downloading/extracting them via `CMakeLists.txt`).
- **Package Management:** Agnostic. Bring in external packages *only* when the time/effort savings massively outweigh the cost of implementation. Be highly selective.

# Project Architecture
Scaffold the workspace with separate folders for the following sub-projects. Each sub-project must have its own `CMakeLists.txt`, orchestrated by an outer root `CMakeLists.txt`. Start small: make each project buildable with a minimal set of files without implementing heavy logic yet.

1. **Engine:** The core library containing systems, abstractions, and rendering.
2. **App (The Game):** Consumes the Engine as a library.
    - *Server Architecture:* The game must abstract the server/client relationship. The app should be able to run locally with a `--server` flag and config, listening on loopback. This abstraction will seamlessly allow peer-to-peer multiplayer in the future.
3. **Voxel Editor Tool:** Consumes the Engine as a library. Used to create/edit non-generated models (animals, doors, items), preview regular block textures, apply textures to models, and pack assets for the App to consume.

# Scaffolding & Tooling Requirements
- **.vscode Tooling:** Provide a `.vscode` folder with files configured for launching, debugging, and editor integration across Windows, Mac, and Linux.
- **Documentation:** 
    - Create a root `README.md` detailing the project overview, tech stack (including any chosen libraries), and global build instructions for Win/Mac/Linux.
    - Create sub-project `README.md` files. These must explicitly define the file and folder structure necessary to meet the base functionality outlined below.
- **Code Documentation:** Every scaffolded C++ file/header *must* contain a block comment at the top defining its scope, what it needs to implement in the abstract, and how it relates to the rest of the codebase.

# Base Functionality to Abstract & Scaffold
Ensure the architecture accounts for the following features through interfaces, data structures, and class stubs:

**Platform & API Abstraction:**
- Build targets: Windows (DX/Vulkan), Linux (Vulkan), Mac (Metal), Dreamcast (KallistiOS).
- Abstract platform-specific code (Xbox GDK, Steam API, Nintendo SDK, PlayStation, KallistiOS). 
- Note: Networking is universal across all platforms (Dreamcast includes a Broadband Adapter).

**App Lifecycle & States:**
- Command-line options (e.g., `--fullscreen=<true,false>`) that override config file settings on load.
- Boots to the default resolution of the device.
- **Main Menu:** Start (pick/create/delete saves), Settings, Exit.
- **Pause Menu:** Edit settings, resume, exit, toggle world public/private visibility.

**World Generation & Flow:**
- Start -> Set World Options (Seed, Peaceful, Permadeath, Always Sunny/Top-Noon, Visibility, Sandbox Mode) -> Loading Screen/Gen Phase -> Calculate Safe Spawn (surface of a chunk) -> Place Player -> Game Starts.
- **Gen Pipeline:** Extensible queue of processes (1. Shape/rough block layout, 2. Caves, 3. Vegetation/Trees).
- **Blocks:** Stone, Dirt, Coal, Water, Tree (Wood Trunk), Leaf.
- Items and Blocks must use an abstraction allowing for custom state and logic.

**Core Data Structures (Define stubs/structs for these):**
- **Player:** Player save data.
- **Game Save:** Separate save file per player/world.
- **Game Preferences:** Platform-aware preferences (e.g., Dreamcast cannot toggle fullscreen). Includes window/fullscreen, resolution, rendering quality, simulation distance, render distance, music/SFX volume, and control bindings.
- **Geometry:** Sub-voxel support (e.g., large blocks take up 1 block unit, but items like stairs take up 1 block unit without rendering as a full block).
- **World/Chunks:** Serialized chunk data containing items and internal object states. Chunking must optimize asset streaming and simulation distance for low-end hardware (Dreamcast).

**Input & Multiplayer:**
- Abstract inputs to handle console play, PC/Mac/Linux (Mouse/Keyboard), and XInput (Xbox controllers).
- Architect the input and player managers to seamlessly support drop-in/drop-out splitscreen multiplayer in the future.
