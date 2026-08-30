# Work Item 03 — OpenGL 3.3 Render Backend

**Phase:** A — Foundation Repair · **Prerequisites:** 01, 02

---

## 1. Problem Statement

There is no renderer. `Renderer` in `rendering.cpp` is a no-op class. `MockRenderer` records draw calls into a vector and draws nothing. Every "backend" was a subclass of the mock. Nothing in this project has ever issued a graphics API call.

## 2. Objective

Implement `GLRenderer` — a real OpenGL 3.3 Core backend behind the existing `IRenderer`/RHI contracts — and prove it by rendering a lit, textured, depth-tested 3D scene inside the game window.

## 3. Scope

**In scope:** `engine/graphics/gl/`, RHI contract cleanup, shader loading/compilation, GPU resource RAII wrappers, camera/frustum math in `engine/render/`, debug line drawing, GL error/debug-callback plumbing.

**Out of scope:** chunk meshes (05), texture atlas (04), UI rendering (08).

## 4. Implementation Tasks

1. **Tighten the RHI surface** in `graphics/rhi.hpp` so it is expressible in GL 3.3 and still portable: buffers (vertex/index/uniform, static/dynamic), 2D textures + 2D array textures, samplers (nearest/linear, mip, anisotropy, clamp/repeat), shader programs, pipeline state (depth test/write, cull face, blend mode, polygon mode), and a `DrawCall { pipeline, vao, indexCount, indexOffset, textures[], uniforms }`. Delete anything the GL backend cannot honor.
2. **`GLRenderer` implementing `IRenderer`:**
   * `Initialize(IPlatform&)` — adopt the platform's GL context, query and log `GL_VENDOR`/`GL_RENDERER`/`GL_VERSION`, verify required features, install `glDebugMessageCallback` in debug builds routed to `voxels::Logger`.
   * `BeginFrame(clearColor)` — viewport from drawable size, clear color+depth.
   * `Submit(const DrawCall&)` — bind pipeline state only on change (state cache), bind VAO/textures, set uniforms, `glDrawElements`.
   * `EndFrame()` — flush batches, `SDL_GL_SwapWindow`.
   * Resource creation returns RAII handles; destruction defers deletion to the render thread.
3. **Shader system** (`engine/graphics/gl/gl_shader.cpp`):
   * Load `.glsl` vertex/fragment pairs from `assets/shaders/`, `#include` support for a shared `common.glsl`.
   * Compile/link with full info-log capture; on failure log the shader source with line numbers and fall back to a compiled-in default shader so the screen never goes black.
   * Uniform location cache.
   * Ship real sources: `chunk.glsl` (atlas sampling + directional light + AO + fog), `model.glsl`, `line.glsl`, `ui.glsl`.
4. **`engine/render/camera.hpp/.cpp`:** perspective projection from FOV/aspect/near/far, view matrix from position + yaw/pitch, `ViewProjection()`, and a `Frustum` with six planes plus `Intersects(AABB)`.
5. **`DebugDraw`:** immediate-mode line batching (`DrawLine`, `DrawAABB`) flushed once per frame in the debug pass. Used immediately for the block-selection outline in work item 07.
6. **Prove it now — temporary scene:** while chunk rendering does not yet exist, `InGameState` (or a `--render-test` mode) renders a textured, depth-tested, lit cube grid on a sky-colored background, with a mouse-orbit camera. This is the visible proof the backend works and is removed by work item 05.
7. **Sky:** clear to a configurable sky color; add a simple gradient/fog term in `chunk.glsl` for depth cueing.
8. **Retire the mock from shipping:** `MockRenderer` moves under a test-only path and is only constructed by tests or `--headless`.

## 5. Acceptance Criteria

* Launching `voxels_app` shows a **non-black window** with a sky-colored background — the reported symptom is gone.
* The temporary scene renders correct 3D geometry with depth testing, back-face culling, and a texture sampled from a generated checkerboard.
* Deleting a shader file still yields a rendered image (fallback shader) plus a warning in the log.
* Zero GL errors reported by the debug callback during a normal session.
* Resizing the window correctly updates viewport and projection aspect ratio with no stretching.

## 6. Automated Tests

`tests/test_render_backend.cpp` (GL-context tests tagged `[.gpu]`, excluded from headless CI):
* `Camera.ProjectionAndViewMatricesMatchKnownValues` — compare against hand-computed matrices.
* `Frustum.CullsAabbsOutsideAndKeepsInside` — corner and edge cases, including a box straddling the near plane.
* `ShaderPreprocessor.ResolvesIncludesAndReportsErrorsWithLineNumbers`.
* `RhiPipelineStateCache.AvoidsRedundantStateChanges` — submitting N identical draw calls produces one state application.
* `[.gpu] GLRenderer.InitializesAndRendersOffscreenFrame` — create a hidden window + context, render a known triangle to an FBO, read back pixels, assert the expected color at the expected pixel.

## 7. Anti-Shell Checks

- [ ] `GLRenderer` issues real GL calls; no method returns success without doing work.
- [ ] No renderer subclass exists whose only difference is a name string.
- [ ] The default app path constructs `GLRenderer`, not `MockRenderer`.

## 8. Assets & Human Actions

* Shaders are authored by the agent and committed under `app/assets/shaders/`.
* Placeholder textures are generated procedurally; register them in [ASSET_REQUESTS.md](../ASSET_REQUESTS.md).
* If the environment has no GPU/GL 3.3 driver, say so explicitly in the completion report and state that the visual check could not be performed.

## 9. Verification

Build, run tests, launch the app, and **describe what appeared on screen** — background color, geometry visible, camera responsive, measured FPS.
