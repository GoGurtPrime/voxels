# Work Item 11 — Audio Runtime & Sound Design Pass

**Phase:** C — The Application Shell · **Prerequisites:** 07, 09 · **🚩 MVP GATE — the game must be fully playable after this item**

---

## 1. Problem Statement

The audio engine synthesizes PCM into a buffer that is never sent to an output device. There is no SDL audio device, no mixer, no WAV/OGG decoding, no sound bank, no music, and no connection to gameplay events. The game is silent.

## 2. Objective

Audible, spatialized, settings-driven sound: block interaction, footsteps, ambience, and music — and, as the MVP gate, a full verification that the game is genuinely playable end to end.

## 3. Scope

**In scope:** `IAudioDevice` + SDL backend, mixer, WAV/OGG decoding, sound bank, 3D attenuation, music streaming, gameplay event wiring, generated filler audio, **MVP acceptance sweep**.

**Out of scope:** reverb zones, occlusion, dynamic music systems.

## 4. Implementation Tasks

1. **`IAudioDevice` + `SDLAudioDevice`:** open an SDL audio device (44.1 kHz, stereo, 16-bit or float32, ~1024-sample buffer), run the SDL audio callback, and shut down cleanly. Device open failure logs a warning and installs a `NullAudioDevice` — the game stays playable, silently, and the failure is surfaced to the player once via a toast.
2. **Mixer:** fixed voice pool (e.g. 32), per-voice clip cursor, gain, pitch, pan, and looping; sample-rate conversion for clips that don't match the device; soft clipping on the master bus. **The callback never allocates, never locks, and never touches game state** — commands arrive through a lock-free ring buffer.
3. **Decoders:** `dr_wav` for WAV (fully decoded into memory for short SFX) and `stb_vorbis` for OGG (streamed on a job worker for music, double-buffered). Decode failures fall back to a synthesized placeholder plus a warning.
4. **Sound bank:** `app/assets/data/sounds.json` maps logical sound ids (`sfx/break_stone`, `sfx/step_grass`, `music/ambient_day`) to one or more clip files with random pitch/gain variation ranges, category (SFX/Music/Ambience/UI), and 3D vs 2D flag. Block definitions from work item 04 already reference these ids.
5. **3D audio:** listener position/orientation from the camera each frame; per-voice distance attenuation (inverse-square with a max radius) and stereo panning from the listener-relative direction. Sounds beyond max radius are not allocated a voice.
6. **Gameplay wiring:** block break (per material), block place, footsteps (cadence driven by horizontal speed and the block being stood on), jump, land, splash/underwater enter-exit with a low-pass feel, UI hover/click, menu music, ambient day loop, cave ambience triggered by depth + enclosure.
7. **Settings integration:** master / music / SFX / ambience volume sliders apply **immediately and audibly**; muting when the window loses focus is a setting.
8. **Generated filler audio (committed to disk):** synthesize and write real files with `stb_image_write`-equivalent WAV/OGG writers — filtered noise bursts for breaking (spectrally distinct per material), short clicks for placement, soft noise thumps for footsteps, a gentle sine-pad ambient loop, and a simple menu theme. It must sound *deliberate*, not like a test tone.
9. **Mixing sanity:** no clipping at max volume with 16 simultaneous voices; music ducks slightly under UI sounds.

## 5. Acceptance Criteria — Audio

* Breaking stone and breaking wood sound audibly different, and both come from the correct direction and distance.
* Footsteps play at a natural cadence while walking and change with the block underfoot.
* Menu music plays on the main menu; ambient audio plays in-game.
* Volume sliders take effect while the sound is playing.
* Unplugging the audio device mid-session does not crash the game.

## 6. 🚩 MVP GATE — Full Playability Sweep

This item does not pass until the following complete, human-observed run succeeds. Report each step explicitly.

1. Launch `voxels_app` from a clean user-data directory. → Main menu appears, with music.
2. Play → Create World → enter a name and seed → Create. → Loading screen shows real phase progress.
3. Spawn. → The player stands on the terrain surface in a textured, lit, streaming world.
4. Look around with the mouse, walk with WASD, sprint, jump, fall off a ledge and land. → Smooth, correct, audible.
5. Break at least four different block types with visible cracking and material-specific sound; the items enter the hotbar.
6. Place blocks from the hotbar to build a recognizable structure; select slots with `1`–`9` and the wheel.
7. Find and enter a cave; confirm underground terrain, lighting, and cave ambience.
8. Enter water; confirm translucency and the splash/underwater cue.
9. `F3` → the debug overlay reports plausible live values. `F2` → a screenshot is written.
10. `Escape` → pause; change FOV and mouse sensitivity in Settings and observe the change on resume.
11. Save & Quit to Menu → the world appears in World Select.
12. Load the same world → the structure from step 6, the player position, and the inventory are all exactly as left.
13. Quit to desktop → process exits 0, no crash, no leaked handles.

**If any step fails, the work item is not complete.** Fix it, or if the fix genuinely belongs to a later work item, say so explicitly and loudly in the completion report rather than passing the gate.

## 7. Automated Tests

`tests/test_audio_runtime.cpp`:
* `Mixer.MixesMultipleVoicesWithoutClipping` — assert peak amplitude bounds.
* `Mixer.VoiceStealingPrefersQuietestAndFurthestVoice`.
* `Mixer.CallbackPerformsNoAllocations` — instrumented allocator asserts zero allocations during a callback.
* `Spatial.AttenuationAndPanningMatchExpectedCurve` — table-driven over listener-relative positions.
* `Spatial.SoundsBeyondMaxRadiusAreNotAllocatedAVoice`.
* `Decode.WavRoundTripsGeneratedFile` and `Decode.OggStreamProducesExpectedSampleCount`.
* `Decode.CorruptClipFallsBackToPlaceholderAndWarns`.
* `SoundBank.ResolvesBlockSoundIdsForEveryBlockDefinition` — no block references a missing sound.
* `Volume.CategoryGainsApplyMultiplicativelyAndImmediately`.
* `Device.OpenFailureInstallsNullDeviceAndKeepsGameRunning`.

`tests/test_mvp_playthrough.cpp` (rewritten to assert real side effects):
* `Mvp.EndToEndFlowProducesLoadedWorldSpawnedPlayerAndPersistedSave`.
* `Mvp.BuildQuitReloadPreservesPlayerEditedBlocks`.
* `Mvp.SpawnIsAlwaysClearOfTerrainAcrossFiftySeeds`.

## 8. Anti-Shell Checks

- [ ] Audio reaches a real output device; no path stops at "generated PCM".
- [ ] Gameplay events drive sound through the running game, not test-only calls.
- [ ] The MVP sweep was actually performed, and any step that could not be verified is named explicitly.

## 9. Assets & Human Actions

Request the full audio set from the operator (44.1 kHz 16-bit PCM WAV for SFX, Vorbis OGG for music) with exact filenames, and note that synthesized versions are shipping and can be replaced file-for-file. Add every entry to [ASSET_REQUESTS.md](../ASSET_REQUESTS.md).

## 10. Verification

Build, run all tests, then perform the §6 sweep and report each of the 13 steps with what you actually observed.
