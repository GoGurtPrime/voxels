# Work Item 08: Audio Subsystem & Asset Pipeline

## 🎯 Objective & Overview
Construct the audio device engine (sound effects, ambient music streaming, 3D spatial audio positional attenuation) and an asset management pipeline (file loader, package index reader/writer, texture decoder, sound decoder) capable of running across desktop and Dreamcast targets.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `02_core_engine_foundation`
* **Target Subsystems:** `engine/include/voxels/audio/`, `engine/src/audio/`, `engine/include/voxels/assets/`

---

## 📋 Detailed Task Breakdown
1. **Audio Engine Interface (`voxels/audio/audio_engine.hpp`):**
   * Abstract interface `IAudioEngine`:
     * `bool initialize()`
     * `SoundHandle loadSound(const std::string& path)`
     * `MusicHandle loadMusic(const std::string& path)`
     * `void playSound(SoundHandle handle, float volume, float pitch, glm::vec3 spatialPos)`
     * `void playMusic(MusicHandle handle, bool loop, float fadeTimeSeconds)`
     * `void setMasterVolume(float volume)`
     * `void setMusicVolume(float volume)`
     * `void setSFXVolume(float volume)`
     * `void shutdown()`
   * Implementation using miniaudio / SDL_mixer or lightweight PCM mixing buffer.
   * Procedural tone generator for headless environments or missing audio drivers.

2. **Asset Management & Resource Cache (`voxels/assets/asset_manager.hpp`):**
   * Asynchronous resource loading queue (`std::future` / thread pool worker).
   * Resource handles for Textures, Shaders, Sounds, Models, and Fonts.
   * Asset Archive (`.vpk` / packed asset archive format): Reads and extracts files from packed single-file binary archives for production distribution.

3. **Image & Font Decoding Utility:**
   * STB Image (`stb_image.h`) integration or raw PNG decoder wrapper.
   * Basic font bitmap / TrueType decoder (`stb_truetype.h`) for UI rendering.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_audio_assets.cpp`
* **Test Cases:**
  * `Audio.ProceduralToneGeneration`: Generate 440Hz sine wave PCM buffer, verify non-zero samples and expected frequency zero-crossings.
  * `Audio.3DSpatialAttenuation`: Calculate 3D positional volume attenuation at distance 0 vs. distance 50 units, verify Inverse Square Law falloff calculation.
  * `AssetManager.PackingAndUnpacking`: Write 3 mock asset files to a binary `.vpk` archive, reload archive with `AssetManager`, verify binary payload integrity.

---

## 👤 Human-in-the-Loop Actions Required
* **Audio & Music Sound Assets (Human Step):**
  1. Prepare audio files in WAV or OGG format:
     * Sound Effects: `app/assets/audio/sfx/footstep_stone.wav`, `app/assets/audio/sfx/block_place.wav`, `app/assets/audio/sfx/block_break.wav`
     * UI Sounds: `app/assets/audio/sfx/ui_click.wav`
     * Background Music: `app/assets/audio/music/ambient_day.ogg`
  2. Prepare font asset:
     * `app/assets/fonts/main_font.ttf`
  3. (Procedural Fallback: The audio engine will synthesize synthetic square-wave clicks/beeps for missing WAV files, and the asset manager will load an embedded 8x8 ASCII font bitmap if `main_font.ttf` is missing).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R AudioAssetsTest`
3. **Self-Healing:** Ensure audio device initialization handles systems without physical sound hardware (e.g. headless CI servers) gracefully without crashing.
