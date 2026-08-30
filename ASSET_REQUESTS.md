# ASSET_REQUESTS.md — Content Needed From the Human Operator

> **Purpose:** a single, always-current ledger of every asset, SDK, or tool the project needs from a human. Agents append to this file whenever they ship a placeholder, and repeat the request in their completion report (see [AGENT_RULES.md](AGENT_RULES.md) § 6).
>
> **Agent rules for this file:**
> 1. Never remove an entry — move it to **Fulfilled** with the date and what was supplied.
> 2. Always specify the exact path, format, dimensions/sample rate, and channel layout. The operator must be able to act without asking a follow-up question.
> 3. Always state what currently stands in for it, and what visibly/audibly improves once it is replaced.
> 4. Generating real filler content and committing it is preferred over a runtime-only stub. Filler counts as "placeholder present", not "fulfilled".

---

## Status Key

| Status | Meaning |
| :--- | :--- |
| **BLOCKING** | The build, tests, or a core feature cannot proceed without this. Should be rare. |
| **QUALITY** | A procedural/filler placeholder is shipping; real content improves fidelity. |
| **OPTIONAL** | Nice to have; no functional impact. |

---

## Outstanding Requests

### ENV-001 — SDL2 development libraries *(status: resolved by work item 01)*
* **Need:** SDL2 2.28+ development libraries available to CMake.
* **Resolution:** Work item 01 makes CMake fetch and build SDL2 automatically when `find_package(SDL2)` fails, so no operator action is required. If you prefer a system SDL2, install it (`vcpkg install sdl2`, `apt install libsdl2-dev`, `brew install sdl2`) and configure with the vcpkg toolchain file; CMake will use it in preference to the fetched copy.
* **Operator action:** none required.

### TOOL-001 — Graphics driver capable of OpenGL 3.3 Core *(status: OPTIONAL, verify only)*
* **Need:** the machine running `voxels_app` must expose an OpenGL 3.3 Core profile context (any GPU from ~2010 onward, with vendor drivers installed — not the Microsoft Basic Display Adapter).
* **Operator action:** if the app reports "failed to create GL 3.3 Core context", install/update your GPU vendor drivers.

---

## Placeholder Inventory (filler content currently shipping)

Agents add rows here as they introduce filler. Each row is a standing invitation for the operator to upgrade the content at any time; none of them block the build.

| ID | Asset Path | Required Format | Current Placeholder | What Improves When Replaced | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TEX-001** | `app/assets/textures/blocks/stone.png` | 16×16 RGBA8 PNG | Procedural value-noise granite | Hand-authored stone tile art | QUALITY |
| **TEX-002** | `app/assets/textures/blocks/dirt.png` | 16×16 RGBA8 PNG | Procedural speckled earth | Hand-authored rich dirt texture | QUALITY |
| **TEX-003** | `app/assets/textures/blocks/grass_top.png` | 16×16 RGBA8 PNG | Procedural grass blades | Hand-authored grass canopy texture | QUALITY |
| **TEX-004** | `app/assets/textures/blocks/grass_side.png` | 16×16 RGBA8 PNG | Procedural grass-fringe dirt | Hand-authored side grass transition | QUALITY |
| **TEX-005** | `app/assets/textures/blocks/sand.png` | 16×16 RGBA8 PNG | Procedural ripple sand | Hand-authored fine desert sand texture | QUALITY |
| **TEX-006** | `app/assets/textures/blocks/gravel.png` | 16×16 RGBA8 PNG | Procedural pebble noise | Hand-authored gravel pebble texture | QUALITY |
| **TEX-007** | `app/assets/textures/blocks/water.png` | 16×16 RGBA8 PNG (alpha: ~200) | Procedural translucent wave | Hand-authored water surface texture | QUALITY |
| **TEX-008** | `app/assets/textures/blocks/coal_ore.png` | 16×16 RGBA8 PNG | Procedural coal flecks in stone | Hand-authored coal ore deposit texture | QUALITY |
| **TEX-009** | `app/assets/textures/blocks/iron_ore.png` | 16×16 RGBA8 PNG | Procedural iron flecks in stone | Hand-authored iron ore deposit texture | QUALITY |
| **TEX-010** | `app/assets/textures/blocks/wood_log_top.png` | 16×16 RGBA8 PNG | Procedural concentric tree rings | Hand-authored log cross-section texture | QUALITY |
| **TEX-011** | `app/assets/textures/blocks/wood_log_side.png` | 16×16 RGBA8 PNG | Procedural bark vertical grain | Hand-authored tree trunk bark texture | QUALITY |
| **TEX-012** | `app/assets/textures/blocks/leaves.png` | 16×16 RGBA8 PNG (alpha cutout) | Procedural foliage with alpha | Hand-authored lush leaf canopy | QUALITY |
| **TEX-013** | `app/assets/textures/blocks/planks.png` | 16×16 RGBA8 PNG | Procedural oak planks with seams | Hand-authored wooden plank tiles | QUALITY |
| **TEX-014** | `app/assets/textures/blocks/glass.png` | 16×16 RGBA8 PNG (alpha: 35/255) | Procedural border + glint | Hand-authored clean glass panel texture | QUALITY |
| **TEX-015** | `app/assets/textures/blocks/bedrock.png` | 16×16 RGBA8 PNG | Procedural dark charcoal mottling | Hand-authored unbreakable bedrock art | QUALITY |
| **UI-001** | `app/assets/ui/crosshair.png` | 16×16 RGBA8 PNG | Generated white pixel crosshair | Styled, themed aiming reticle | QUALITY |
| **UI-002** | `app/assets/ui/hotbar.png`, `app/assets/ui/hotbar_selection.png` | 182×22 and 24×24 RGBA8 PNG | Generated dark slot frame and gold selection outline | Textured hotbar frame and selected-slot treatment | QUALITY |
| **UI-003** | `app/assets/textures/misc/crack_0.png` through `crack_9.png` | Ten 16×16 RGBA8 PNG | Generated progressive dark fracture overlays rendered on the targeted face | Detailed per-face mining crack animation | QUALITY |
| **FONT-001** | `app/assets/fonts/AtkinsonHyperlegible-Regular.ttf` | TrueType, regular weight, Latin glyph coverage | Bundled Atkinson Hyperlegible Regular from Google Fonts under SIL Open Font License 1.1 (`app/assets/fonts/OFL.txt`) | A branded gameplay typeface and expanded localization coverage | QUALITY |
| **FONT-002** | `app/assets/ui/fonts/ui_font.ttf` | TrueType, regular weight, Latin glyph coverage, permissive redistribution licence | Dear ImGui's compiled-in fallback font, with `FONT-001` used when available | A distinct branded menu typeface at 18 px base size and all DPI scales | QUALITY |
| **AUDIO-001** | `app/assets/audio/{break_stone,break_dirt,break_grass,break_sand,break_gravel,break_wood,break_glass,place_stone,place_dirt,place_grass,place_sand,place_gravel,place_wood,place_glass,step_stone,step_dirt,step_grass,step_sand,step_gravel,step_wood,step_glass,step_water,jump,land,splash,ui_click,ui_hover}.wav` | 44.1 kHz, 16-bit signed PCM WAV, mono, under 2 s each | Shipping synthesized tonal/noise WAVs | Recorded material-specific interaction and movement sounds | QUALITY |
| **AUDIO-002** | `app/assets/audio/{menu_theme,ambient_day,ambient_cave}.ogg` | 44.1 kHz stereo Ogg Vorbis, 60-180 s, seamless loop points | Shipping `menu_theme.wav` is a generated 8 s loop; no ambient tracks yet | Intentional long-form menu and world ambience once OGG streaming is implemented | QUALITY |
| **MODEL-001** | `app/assets/models/{stairs,slab,fence,door,torch,chest,pickaxe,player}.vmdl` | VMDL v1, 16×16×16 micro-voxel grid, RGBA8 palette entries with atlas layer assignments | Shipping generated monochrome geometry | Authored silhouettes, material assignment, and meaningful attachment points | QUALITY |

**Expected entries as the roadmap progresses** (agents will fill in exact rows when the corresponding work item lands):

* `app/assets/textures/blocks/*.png` — 16×16 RGBA8 PNG, one per block face variant (`stone`, `dirt`, `grass_top`, `grass_side`, `sand`, `water`, `coal_ore`, `wood_top`, `wood_side`, `leaves`). Placeholder: procedurally generated noise/pattern textures.
* `app/assets/ui/fonts/ui_font.ttf` — TrueType, permissively licensed, good at 16–32 px. Placeholder: bundled fallback bitmap font.
* `app/assets/ui/menu_background.png` — 1920×1080 RGBA8. Placeholder: gradient + rotating world render.
* `app/assets/ui/logo.png` — 512×256 RGBA8 with transparency. Placeholder: text title.
* `app/assets/audio/sfx/*.wav` — 44.1 kHz 16-bit PCM mono (`break_stone`, `break_dirt`, `break_wood`, `place_generic`, `step_stone`, `step_dirt`, `step_grass`, `jump`, `splash`, `ui_click`, `ui_hover`). Placeholder: synthesized noise/tone bursts.
* `app/assets/audio/music/*.ogg` — 44.1 kHz stereo Vorbis, 60–180 s, loopable (`menu_theme`, `ambient_day`, `ambient_cave`). Placeholder: synthesized ambient pad.
* `app/assets/textures/items/*.png` — 16×16 RGBA8 per item icon. Placeholder: derived from block face texture.
* `app/assets/models/*.vmdl` — authored in `voxels_editor`. Placeholder: procedurally generated sample models.

---

## Fulfilled

| ID | Supplied | Date | Notes |
| :--- | :--- | :--- | :--- |
| _(empty)_ | | | |
