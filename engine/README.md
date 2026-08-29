# Engine

The engine project contains the platform abstraction, core data model, renderer interface, world generation contracts, and input abstractions for the gameplay runtime.

## Required structure

```text
engine/
├── CMakeLists.txt
├── README.md
├── include/
│   └── voxels/
│       ├── core/
│       │   ├── game_types.hpp
│       │   ├── player.hpp
│       │   ├── save.hpp
│       │   └── preferences.hpp
│       ├── graphics/
│       │   ├── renderer.hpp
│       │   ├── renderer_backend.hpp
│       │   ├── render_device.hpp
│       │   └── shader.hpp
│       ├── platform/
│       │   ├── platform.hpp
│       │   ├── headless_platform.hpp
│       │   ├── sdl_platform.hpp
│       │   ├── dreamcast_platform.hpp
│       │   └── platform_context.hpp
│       ├── input/
│       │   ├── input_manager.hpp
│       │   ├── input_state.hpp
│       │   └── binding.hpp
│       ├── world/
│       │   ├── block.hpp
│       │   ├── item.hpp
│       │   ├── geometry.hpp
│       │   ├── chunk.hpp
│       │   ├── world.hpp
│       │   └── generation_pipeline.hpp
│       ├── networking/
│       │   ├── network_client.hpp
│       │   ├── network_server.hpp
│       │   └── network_session.hpp
│       └── app/
│           ├── app_state.hpp
│           ├── main_menu.hpp
│           ├── pause_menu.hpp
│           └── command_line.hpp
├── src/
│   ├── engine.cpp
│   ├── platform/
│   │   ├── headless_platform.cpp
│   │   ├── sdl_platform.cpp
│   │   ├── dreamcast_platform.cpp
│   │   └── platform_factory.cpp
│   ├── rendering.cpp
│   ├── input.cpp
│   ├── world.cpp
│   └── networking.cpp
└── tests/
```

The initial scaffold does not implement gameplay logic yet; it establishes the interfaces and domain types needed by the rest of the project.

## Platform layer (Work Item 02)

`IPlatform` (`include/voxels/platform/platform.hpp`) abstracts window creation, event pumping,
buffer swapping, and high-resolution timing. `CreateDefaultPlatform()` selects an implementation
at build time: `DreamcastPlatform` when `VOXELS_ENABLE_DREAMCAST` is set, `SDLPlatform` when SDL2
is linked in, otherwise `HeadlessPlatform` — an in-memory fallback used for tooling and automated
tests so the engine and its test suite build and run without any native windowing library.

### Human setup: enabling the real SDL2 desktop backend

SDL2 development libraries are not vendored by this repository. To build `SDLPlatform` (real
windows instead of the headless fallback) on Windows/Mac/Linux:
1. Install SDL2 development headers/libraries (e.g. via `vcpkg install sdl2`, your system package
   manager, or the official SDL2 development binaries from libsdl.org).
2. Reconfigure with the SDL2 toolchain visible to CMake (e.g.
   `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`).
3. Re-run `cmake -S . -B build` — once `find_package(SDL2)` succeeds, the build automatically
   defines `VOXELS_HAS_SDL2`, links `SDL2::SDL2`, and compiles `src/platform/sdl_platform.cpp`.

No code changes are required; `CreateDefaultPlatform()` picks up the SDL2 backend automatically
once the library is available.
