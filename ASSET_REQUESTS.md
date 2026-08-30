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
| _(none yet — populated from work item 04 onward)_ | | | | | |

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
