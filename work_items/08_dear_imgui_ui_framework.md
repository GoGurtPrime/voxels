# Work Item 08 — Dear ImGui UI Framework Integration

**Phase:** C — The Application Shell · **Prerequisites:** 02, 03

---

## 1. Problem Statement

`UIManager` computes a DPI scale factor and does nothing else — it has never drawn a pixel. Dear ImGui is referenced throughout the architecture but is not a dependency. There is no font, no theme, no widget vocabulary, no input routing between UI and gameplay, and therefore no menus are possible.

## 2. Objective

A real, themed, DPI-aware UI layer implementing `IUIManager` on Dear ImGui + SDL2 + OpenGL 3, with correct input arbitration against gameplay — the substrate every screen in work item 09 needs.

## 3. Scope

**In scope:** `engine/ui/` ImGui backend, font loading, theme, layout scaling, input routing/context switching, reusable widget vocabulary, the F3 debug overlay, an on-screen error/notification surface.

**Out of scope:** the actual menu screens and their behavior (09), editor-specific panels (14).

## 4. Implementation Tasks

1. **`ImGuiUIManager` implementing `IUIManager`:** create the ImGui context, install `ImGui_ImplSDL2_InitForOpenGL` + `ImGui_ImplOpenGL3_Init("#version 330")`, `BeginFrame`/`EndFrame` wrapping `NewFrame`/`Render` + `RenderDrawData`, and clean shutdown. Draws last in the frame, after all 3D passes ([DIAGRAMS.md](../DIAGRAMS.md) §6).
2. **Event routing.** `ImGuiUIManager` is the **first** platform event listener. It forwards events to ImGui and reports `WantCaptureMouse`/`WantCaptureKeyboard`; when either is true the gameplay listener does not receive the event. This is the arbitration rule — no ad-hoc `if (menuOpen)` checks scattered through gameplay code.
3. **Input contexts.** A small `InputContext` enum (`Menu`, `Gameplay`, `TextEntry`) owned by the state machine determines mouse capture and which listeners are active. Entering a menu releases the cursor; returning to gameplay re-captures it and discards the first mouse delta.
4. **Fonts.** Load `app/assets/ui/fonts/ui_font.ttf` at sizes derived from DPI scale (base 18 px × scale), with an embedded fallback font compiled in so a missing TTF never leaves the UI unreadable. Rebuild the font atlas on DPI/scale change.
5. **Theme.** A single `ApplyVoxelsTheme()` defining the game's look — muted stone-grey panels, warm accent, generous padding, rounded-but-chunky frames, consistent sizing tokens. Menus must not look like default ImGui debug windows. Extract spacing/color tokens into named constants so the look is tunable in one place.
6. **Layout scaling.** Screens are authored against a reference resolution and scaled by `ComputeUIScale` so they are usable from 640×480 to 4K. Menus are centered, anchored, and never clipped at extreme aspect ratios.
7. **Widget vocabulary** (`engine/ui/widgets.hpp`): `MenuButton`, `MenuTitle`, `SettingSlider`, `SettingToggle`, `SettingDropdown`, `KeyBindRow`, `TextField`, `ConfirmDialog`, `ProgressBar`, `SaveListEntry`, `Toast`. These are what work item 09 assembles into screens.
8. **Debug overlay (F3):** frame time / FPS graph, player position and chunk, facing, loaded/meshed/visible chunks, draw calls, triangles, mesh queue depth, resident memory, GL vendor/renderer. Toggled by a bound action, off by default.
9. **Error & notification surface:** `UIManager::ShowError(title, detail)` renders a modal the player can actually read, used by startup failures and by the `ErrorScreen` state; `ShowToast(message)` for transient notices (autosave complete, screenshot taken).
10. **Retire the procedural `UIManager`** as the shipping implementation; keep a `NullUIManager` only for headless tests.

## 5. Acceptance Criteria

* A themed ImGui window renders on top of the 3D scene at the correct DPI scale and is legible at 1080p and at 4K.
* Clicking a UI element does not also break/place a block; moving the mouse over a menu does not turn the camera.
* Opening a menu releases the cursor; closing it re-captures it without a camera jump.
* F3 toggles a debug overlay with live, correct values.
* Deleting the font file still produces readable UI (embedded fallback) plus a warning.

## 6. Automated Tests

`tests/test_ui_framework.cpp`:
* `UIScale.ComputesExpectedFactorsAcrossResolutions` — 640×480, 1280×720, 1920×1080, 3840×2160.
* `InputRouting.UiCaptureSuppressesGameplayListener`.
* `InputRouting.ContextSwitchTogglesMouseCaptureAndDiscardsFirstDelta`.
* `Fonts.MissingTtfFallsBackToEmbeddedFontAndWarns`.
* `Theme.AppliesConsistentTokenSetAndIsIdempotent`.
* `DebugOverlay.ReportsMetricsFromTheRealFrameStatsSource` — not fabricated values.
* `[.gpu] ImGuiUIManager.RendersDrawDataToOffscreenTarget` — a known-colored panel appears at the expected pixels.

## 7. Anti-Shell Checks

- [ ] `ImGuiUIManager` actually issues ImGui and GL draw calls.
- [ ] The procedural `UIManager` is no longer the app's default.
- [ ] No screen depends on a widget that renders nothing.

## 8. Assets & Human Actions

* `app/assets/ui/fonts/ui_font.ttf` — request a permissively licensed TrueType font from the operator; ship an embedded/bundled fallback and register the request in [ASSET_REQUESTS.md](../ASSET_REQUESTS.md) with the licence note.
* Optional: `app/assets/ui/panel_bg.png` and accent art. Generate placeholders.

## 9. Verification

Build, test, launch, and **describe the UI you saw**: theme, font legibility, DPI behavior, cursor/capture behavior, and the F3 overlay contents.
