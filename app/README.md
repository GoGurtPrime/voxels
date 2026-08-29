# App

The app project is the runtime layer that consumes the engine library and provides the game-specific lifecycle, world flow, and multiplayer abstraction.

## Required structure

```text
app/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── app.cpp
│   ├── settings.cpp
│   ├── world_flow.cpp
│   ├── server.cpp
│   └── game.cpp
├── include/
│   └── app/
│       ├── app.hpp
│       ├── config.hpp
│       ├── server_host.hpp
│       └── cli_options.hpp
└── assets/
```

This project should remain focused on game lifecycle orchestration. The engine owns the underlying systems and abstractions.

## Optional Voxel Shader Assets

The graphics RHI and mock backend work without shader files, so automated and headless builds do
not require a GPU SDK or shader compiler. When enabling a native Vulkan renderer, provide these
source files under `app/assets/shaders/`:

1. Create `voxel.vert` as a GLSL 450 vertex shader and `voxel.frag` as a GLSL 450 fragment shader.
2. Create `app/assets/shaders/spv/`.
3. Compile from `app/assets/shaders/` using `glslangValidator -V voxel.vert -o spv/voxel.vert.spv`
	and `glslangValidator -V voxel.frag -o spv/voxel.frag.spv`.
4. Keep the generated `.spv` files in `app/assets/shaders/spv/` for the application asset loader.

The current Vulkan scaffold uses the same CPU validation resource path as `MockRenderer` until a
native Vulkan device and swapchain are integrated, so absent shader assets do not prevent builds
or test execution.
