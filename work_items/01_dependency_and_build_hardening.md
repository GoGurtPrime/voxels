# Work Item 01 — Dependency & Build Hardening

**Phase:** A — Foundation Repair · **Prerequisites:** none · **Blocks:** everything

---

## 1. Problem Statement

The build system is the root cause of the black window. `find_package(SDL2 QUIET)` fails silently on a clean machine, `sdl_platform.cpp` is then excluded from the engine target, and `voxels_app` falls back to `HeadlessPlatform` — a "successful build" that can never draw anything. Dear ImGui is not a dependency at all. There are no image, audio, or font decoders. There is no asset staging step, so even if assets existed the executable could not find them.

## 2. Objective

Make the default build produce an executable that **must** have a real window, a real GL context, real UI, and real decoders — or fail configuration with an actionable message. No silent degradation.

## 3. Scope

**In scope:** root and sub-project `CMakeLists.txt`, `CMakePresets.json`, dependency acquisition, asset staging, `core/paths` module, `.gitattributes`/`.gitignore` hygiene.

**Out of scope:** using any of the new dependencies (later work items), Vulkan/DX12/Metal, console SDKs.

## 4. Implementation Tasks

1. **Hard SDL2 dependency (ADR-002).**
   * `find_package(SDL2 CONFIG QUIET)`; if not found, `FetchContent` SDL2 release `2.30.x` with `SDL_SHARED=ON`, `SDL_STATIC=OFF`, `SDL_TEST=OFF`.
   * Always compile `src/platform/sdl_platform.cpp` into `voxels_engine`. Define `VOXELS_HAS_SDL2=1` unconditionally for desktop targets.
   * Copy the SDL2 runtime DLL/dylib next to `voxels_app` and `voxels_editor` via a post-build `copy_if_different` step.
2. **Vendor Dear ImGui (ADR-003).**
   * `FetchContent` `ocornut/imgui` (docking branch tag). Build a small `voxels_imgui` static target from the core sources plus `backends/imgui_impl_sdl2.cpp` and `backends/imgui_impl_opengl3.cpp`.
   * Mark ImGui includes `SYSTEM` so its warnings do not pollute `/W4`.
3. **Vendor a GL loader.** `glad` (GL 3.3 Core, or a checked-in generated loader) or `glew`. Pick one, commit the choice in the commit message, expose it as target `voxels_glad`.
4. **Vendor decoders (header-only, `SYSTEM` includes):** `stb_image.h` (PNG), `stb_image_write.h` (placeholder texture generation), `dr_wav.h` (WAV), `stb_vorbis.c` (OGG), `stb_truetype.h` (font). Place under `engine/third_party/`.
5. **`voxels::Paths` module** (`engine/include/voxels/core/paths.hpp` + `.cpp`) implementing ADR-011:
   * `UserDataDir()` — `%LOCALAPPDATA%\VoxelsEngine`, `$XDG_DATA_HOME/VoxelsEngine` (fallback `~/.local/share/...`), `~/Library/Application Support/VoxelsEngine`. Creates the directory tree on first call.
   * `SavesDir()`, `LogsDir()`, `SettingsFile()`, `AssetsDir()` (executable-relative, resolved via `SDL_GetBasePath`).
6. **Asset staging.** Copy `app/assets/` next to the built executable at build time so the running exe always finds content in both single- and multi-config generators.
7. **Build configuration.**
   * `VOXELS_WARNINGS_AS_ERRORS` option, **ON by default** for first-party targets only.
   * Options `VOXELS_BUILD_APP/EDITOR/TESTS` retained. Remove `VOXELS_ENABLE_VULKAN/DX12/METAL` defaults that imply working backends — keep them `OFF` and clearly marked unimplemented.
   * Update `CMakePresets.json`: `windows-debug`, `windows-release`, `linux-debug`, `macos-debug`, each with a matching build preset and a `test` preset. Keep the optional vcpkg toolchain path but do not require it.
8. **Delete inheritance theater.** Remove `graphics/vulkan/`, `graphics/dx12/`, `graphics/metal/`, `graphics/dreamcast/` renderer classes that merely subclass `MockRenderer` and change a name string (Anti-Shell Rule #4). Their absence is honest; a comment in `graphics/rhi.hpp` records the intended future backends.
9. **`.gitattributes`:** ensure `*.png`, `*.ogg`, `*.wav`, `*.ttf`, `*.vpk`, `*.vmdl` are treated as binary and LFS-ready. `.gitignore`: keep `build/`, `out/`, and staged asset copies out of the repo.

## 5. Acceptance Criteria

* A clean clone on a machine with **no SDL2 installed** configures and builds `voxels_app`, `voxels_editor`, and `voxels_tests` successfully.
* Configuring with `VOXELS_BUILD_APP=ON` never emits "SDL2 not found; the platform layer remains abstract".
* `voxels_app.exe` sits next to `SDL2.dll` and an `assets/` directory after the build.
* `voxels::Paths::UserDataDir()` returns a real, existing per-user directory on the host OS.
* Zero warnings across all first-party targets.

## 6. Automated Tests

`tests/test_build_environment.cpp`:
* `Paths.UserDataDirIsCreatedAndWritable` — resolve, create, write a probe file, read it back, delete.
* `Paths.SavesAndLogsAreUnderUserDataRoot` — path containment assertions.
* `Paths.AssetsDirExistsNextToExecutable` — the staged `assets/` directory is discoverable at runtime.
* `BuildConfig.Sdl2IsCompiledIn` — asserts `VOXELS_HAS_SDL2` is defined and the SDL2 platform factory is registered.

## 7. Anti-Shell Checks

- [ ] No `message(STATUS "... not found ...")` path leaves a required feature disabled.
- [ ] No renderer class remains whose only behavior is renaming `MockRenderer`.
- [ ] `HeadlessPlatform` is only reachable via tests or an explicit `--headless` flag.

## 8. Assets & Human Actions

None expected. If SDL2's `FetchContent` build fails on the operator's machine, report the exact CMake error and offer the system-package alternative.

## 9. Verification

1. `cmake --preset windows-debug` (or host equivalent) from a clean `build/`.
2. `cmake --build --preset windows-debug`
3. `ctest --preset windows-debug --output-on-failure`
4. Confirm `SDL2.dll` and `assets/` are staged beside the executable; run `voxels_app --version` and confirm it exits 0.
