## WI-01.01: Establish the pinned web UI runtime and asset build contract

### Goal
Make a reproducible desktop build produce the exact local React/Tailwind assets and CEF runtime required for player UI, with a native/browser proof that renders a transparent page over a game framebuffer.

### Scope
- Own: root/app CMake, frontend source/build configuration, CEF version/SHA manifest and acquisition, runtime staging/install rules, frontend lockfile, UI asset manifest, architecture/diagram/asset-request updates.
- Exclude: app-state migration, gameplay HUD replacement, cinematics, and player-facing fallback removal.
- Prerequisites: SDL2, OpenGL renderer, CMake packaging path, Node.js LTS and pinned package manager in CI/developer setup.

### Implementation Contract
- Inputs and outputs: Vite consumes `ui/src` and emits immutable hashed files plus `ui-manifest.json` to `app/assets/ui/`. The manifest contains schema version, entry HTML, source revision/hash, and emitted asset paths/hashes. CMake fails if it is missing or inconsistent.
- Runtime integration: add experimental default-on desktop `VOXELS_ENABLE_WEB_UI` and a `voxels_web_ui_assets` dependency of `voxels_app`. Stage CEF binaries/resources beside the executable and include them in install/CPack. This item creates only a focused runtime proof, not the default game-loop backend.
- Threading and performance: compilation is build-time only; proof rendering is main-thread only. Record baseline bundle size and startup time.
- Platform and dependencies: choose CEF binaries by Windows/Linux/macOS triplet; verify archive SHA-256 before extraction. The committed manifest records CEF/Chromium versions, URL, SHA, runtime files, license notices, and source-build policy. `npm ci`/equivalent is mandatory.

### Acceptance Criteria
- [ ] Configure fails clearly when Node, pinned frontend packages, CEF archive/hash, or required runtime resources are unavailable.
- [ ] A clean build emits the React/Tailwind bundle and validated manifest without a development server.
- [ ] Build, install, and CPack stage all CEF files/resources and required license notices.
- [ ] A focused native proof shows CEF transparency over a GL-cleared scene; a pixel test fails if browser alpha becomes opaque.
- [ ] Automated tests validate manifest parsing/hash verification and staging inputs; proof logs CEF/Chromium versions.
- [ ] `ARCHITECTURE.md`, `DIAGRAMS.md`, package records, and `ASSET_REQUESTS.md` document the runtime, licensing owner, and sandbox prerequisites.

### Verification Commands
```text
cmake --preset windows-debug -DVOXELS_ENABLE_WEB_UI=ON
cmake --build build --config Debug --target voxels_web_ui_assets voxels_web_ui_runtime_proof
ctest --test-dir build -C Debug --output-on-failure -R "WebUiManifest|WebUiRuntimeProof"
cmake --install build --config Debug --prefix build/web-ui-stage
cpack -C Debug
```

### Completion Evidence
- Changed: frontend, lockfile, CEF verification/staging, CMake, package rules, focused proof/tests, and docs.
- Observed: native OpenGL content is visible through browser transparency.
- Results: record CEF/Chromium versions, archive SHA, bundle hash/size, test count, package paths, and smoke observation.
- Known gaps: game states do not own web UI until WI-02 and WI-03.
