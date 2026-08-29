# Work Item 05: Graphics & Renderer Abstraction (RHI)

## 🎯 Objective & Overview
Architect an extensible Render Hardware Interface (RHI) and render graph/pipeline that abstracts graphics APIs across target platforms: Vulkan (Windows/Linux), DirectX 12 (Windows/Xbox), Metal (macOS/iOS), and Dreamcast PVR (Sega Dreamcast/KallistiOS). Implement a headless software/mock backend for automated testing of pipeline state, vertex buffers, and draw calls without hardware GPU requirements.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `03_platform_windowing_sdl2`
* **Target Subsystems:** `engine/include/voxels/graphics/`, `engine/src/graphics/`

---

## 📋 Detailed Task Breakdown
1. **Render Hardware Interface Contracts (`voxels/graphics/rhi.hpp`):**
   * Define abstract classes and structs:
     * `IRenderer`: `initialize()`, `beginFrame()`, `endFrame()`, `submitCommandBuffer()`, `present()`.
     * `IBuffer`: Vertex buffers, index buffers, uniform/constant buffers.
     * `ITexture`: 2D texture, texture array, sampler state.
     * `IShader`: Shader module loading, reflection, layout bindings.
     * `IPipelineState`: Rasterizer state, depth-stencil state, blend state, vertex layout.

2. **Backend Implementations & Stubs:**
   * `VulkanRenderer` (`voxels/graphics/vulkan/`): Vulkan instance, physical device selection, logical device, swapchain, command pools.
   * `DirectX12Renderer` (`voxels/graphics/dx12/`): DXGI factory, ID3D12Device, command queues, swap chain stubs.
   * `MetalRenderer` (`voxels/graphics/metal/`): MTLDevice, MTLCameralayer stubs for Apple targets.
   * `DreamcastPVRRenderer` (`voxels/graphics/dreamcast/`): PowerVR TA/PVR direct polygon DMA submitting stubs for Dreamcast.
   * `MockRenderer` (`voxels/graphics/mock/`): CPU-side headless validation backend tracking draw calls, bound shaders, and vertex counts for unit tests.

3. **Mesh & Material Abstractions (`voxels/graphics/mesh.hpp`, `voxels/graphics/material.hpp`):**
   * Voxel vertex format (`Position`, `Normal`, `UV`, `Color`, `SubVoxelData`).
   * Material instance supporting block atlas texture maps.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_graphics.cpp`
* **Test Cases:**
  * `Renderer.MockBackendInitialization`: Instantiate `MockRenderer`, verify capabilities reporting and frame cycle (`beginFrame`, `endFrame`).
  * `Renderer.BufferCreationAndBinding`: Create vertex and index buffers on `MockRenderer`, verify memory allocation sizing and binding tracking.
  * `Renderer.VoxelMeshDrawCall`: Submit a 3D block mesh to `MockRenderer`, verify correct draw call recorded with expected vertex/index counts.

---

## 👤 Human-in-the-Loop Actions Required
* **Shader Source Files (Human Step):**
  1. Create GLSL/HLSL shader files for voxel chunk rendering:
     * `app/assets/shaders/voxel.vert` (GLSL 450 vertex shader)
     * `app/assets/shaders/voxel.frag` (GLSL 450 fragment shader)
  2. Compile GLSL shaders to SPIR-V binaries using `glslangValidator` or `dxc` if Vulkan validation is required:
     * `glslangValidator -V voxel.vert -o voxel.vert.spv`
     * `glslangValidator -V voxel.frag -o voxel.frag.spv`
  3. Place compile outputs into `app/assets/shaders/spv/`.
  4. (Procedural Fallback: The `MockRenderer` and `VulkanRenderer` will include embedded C++ byte arrays with fallback dummy SPIR-V bytecode if external shader files are missing).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R GraphicsTest`
3. **Self-Healing:** Ensure `MockRenderer` operates 100% headless so GPU-less CI nodes pass all graphic contract tests seamlessly.
