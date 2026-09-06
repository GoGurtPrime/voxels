# Web UI Migration Roadmap

## Intended Outcome

The desktop game presents React/Tailwind player UI as a transparent final render pass above OpenGL and future renderer backends. The same system owns menus, overlays, and the in-game HUD, while gameplay continues rendering underneath it. Menu backgrounds are real rendered voxel-world camera sequences, not browser video or a separate native window.

## Binding Technical Decisions

- Use Chromium Embedded Framework (CEF) in windowless/off-screen rendering mode. It supports React and Tailwind, provides per-pixel alpha, and can be composited into the SDL/OpenGL framebuffer. Native child-window webviews are excluded because their compositor cannot reliably layer over the game's swapchain.
- CEF is a desktop runtime dependency after WI-08. Pin an official CEF binary distribution per supported platform in a version-and-SHA-256 manifest. Do not download a moving branch or rely on a machine-global CEF installation.
- Run CEF in its normal multi-process model. `voxels_app` executes CEF subprocess work before engine creation; the browser uses the external message pump on the main/render thread. Do not disable sandboxing as a convenience. A missing sandbox bootstrap is a documented configuration failure.
- Render CEF premultiplied-alpha BGRA paint buffers into a dynamic GL texture and draw a full-frame UI quad after world, gameplay HUD, and developer diagnostics. The compositor uses `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` and restores modified GL state.
- Build React with Vite and Tailwind into versioned static files under `app/assets/ui/`. The shipping browser may load only that local asset root via a restrictive CEF resource handler: no arbitrary file URLs, remote HTTP(S), navigation, popups, downloads, or devtools in retail builds.
- Use a typed, versioned JSON message envelope: `{ version, kind, requestId, payload }`. JavaScript requests explicit application actions and receives immutable UI view models; it receives no pointers, unrestricted filesystem access, raw platform handles, or direct game-state authority.
- ImGui remains the editor UI. In `voxels_app`, it is temporarily an opt-in developer diagnostics overlay during migration and is removed from player-facing menus/HUD at WI-08. `NullUIManager` remains test-only.

## Required End-State Contracts

- `IPlayerUI` replaces concrete `ImGuiUIManager` references in app composition, state context, and event routing. It owns initialization, per-frame browser pumping/submission, platform events, viewport/DPI updates, input policy, view-model publication, action draining, visible routes, and errors.
- `WebUIManager` is the only desktop player implementation after WI-08. It owns all CEF objects by RAII, uses them only on the main thread, and presents a transparent surface for every player-visible route.
- Unknown protocol versions/kinds, malformed JSON, payloads above 64 KiB, and duplicate/non-monotonic request IDs are rejected, logged, and leave game state unchanged. Engine messages include route, model revision, and payload.
- The native action dispatcher validates route/current state, save names, seed syntax, settings ranges, and duplicate actions before it invokes existing controllers or `AppContext` callbacks.
- Input policy is native-authoritative: `Gameplay`, `Overlay`, and `TextEntry`. Only the manager controls mouse capture, cursor visibility, event forwarding, and first-relative-delta discard. Gameplay input cannot fire from a focused text field or active overlay.
- UI assets have a source hash/version emitted by the frontend build and verified at startup. Missing, corrupt, or protocol-incompatible content produces a visible native error and nonzero exit after WI-08; it never silently reverts to player-facing ImGui.

## Delivery Order

| Order | Work item | Deliverable |
| --- | --- | --- |
| 01 | `01_web_ui_build_and_runtime_contract.md` | Pinned CEF runtime and deterministic React/Tailwind pipeline |
| 02 | `02_player_ui_contract_and_protocol.md` | Backend-neutral UI contract, schemas, dispatcher, input policy |
| 03 | `03_cef_offscreen_compositor.md` | CEF browser, transparent OpenGL compositor, event forwarding |
| 04 | `04_web_main_menu_and_world_flow.md` | Web main menu, world management, loading, errors |
| 05 | `05_cinematic_menu_background.md` | Rendered voxel-world cinematic behind web menus |
| 06 | `06_web_gameplay_hud_and_pause.md` | Web HUD, pause, settings, controls, notifications |
| 07 | `07_web_ui_hardening_and_distribution.md` | Security, accessibility, performance, and package hardening |
| 08 | `08_player_imgui_retirement.md` | Web UI required for the player; ImGui limited to editor/dev diagnostics |

Items are strictly sequential because each consumes the preceding contract or runtime proof.

## Non-Goals

- Replacing the editor's ImGui workspace.
- Rendering the voxel world in HTML canvas/WebGL or moving game rendering into the browser.
- Remote web content, mod-executed JavaScript, in-game browsing, devtools, or arbitrary browser navigation.
- Implementing Vulkan/DX12/Metal before the OpenGL reference path has passed the roadmap gates.

## Global Gates

Every item uses the single canonical build directory `build/` (never a per-item fork such as `build_workitemNN/` — see AGENT_RULES §2.5). Configure with `cmake -S . -B build -DVOXELS_BUILD_TESTS=ON` (add feature flags to the *same* tree when needed), build with `cmake --build build --config Debug`, run `ctest --test-dir build -C Debug --output-on-failure`, and include a non-headless desktop smoke observation when the item changes player-visible behavior. WI-08 also packages with `cpack -C Debug` and proves the packaged executable loads its UI without developer paths.

The default configuration (`cmake -S . -B build` with no extra flags) must stay green throughout. The web UI is behind `VOXELS_ENABLE_WEB_UI` (OFF by default until WI-08); land each item's work in green, committable slices — the seam and its OFF-gated scaffolding first, then the implementation — rather than one large red change (AGENT_RULES §7). If an item is blocked on an operator-supplied CEF archive or other external input, commit the green progress and report BLOCKED with the precise ask; do not leave the tree dirty.