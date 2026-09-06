## WI-07.01: Harden web UI security, accessibility, reliability, and packaged delivery

### Goal
Make local web UI safe to ship and robust across desktop conditions before it becomes the only player UI path.

### Scope
- Own: CEF security policy/audit, resource-handler tests, crash/recovery, asset/package verification, accessibility audit/remediation, profiling/soak automation, release documentation and asset/license records.
- Exclude: renderer backend delivery and editor migration.
- Prerequisites: WI-03 through WI-06.

### Implementation Contract
- Inputs and outputs: enforce manifest allowlist and hashes; deny non-UI schemes/origins, redirects, windows, downloads, clipboard except native-approved cases, drag/drop navigation, file access, devtools, context-menu inspection, and unsupported permissions. Generated HTML has restrictive CSP and no runtime `eval` or remote script.
- Runtime integration: render-process termination, paint stall, corrupt assets, protocol-rejection flood, and load timeout enter an engine-owned visible error route with support-log details. Unready UI cannot authorize gameplay actions.
- Threading and performance: repeatable menu/gameplay route soak and resize/focus stress. At 1080p: UI median $\leq 1.5$ ms CPU, $\leq 0.5$ ms GPU, retained buffers $\leq 32$ MiB, bounded model/action/paint queues, and no growth after a 30-minute soak beyond allocator noise.
- Platform and dependencies: validate Windows and build/package smoke on Linux/macOS where runners exist. Verify CEF redistributable, sandbox/helper requirements, UI assets, licenses, and source notices are installed outside developer paths.

### Acceptance Criteria
- [ ] Security tests prove navigation, network, file, download, devtools, and permission escape attempts cannot leave the local asset allowlist.
- [ ] Fuzz/property tests prove malformed/replayed/oversized messages cannot crash, transition unexpectedly, or mutate persistent/gameplay state.
- [ ] Accessibility review passes keyboard traversal, focus visibility/restoration, semantic names/roles, contrast, text scaling, reduced motion, and supported aspect ratios.
- [ ] Browser crash/reload, invalid manifest, missing CEF file, minimize, context recreation where applicable, and shutdown during active UI work are bounded and logged.
- [ ] Clean-machine packaged smoke needs no Node, source tree, cache, or internet and presents menu plus in-game HUD.
- [ ] CI retains native/frontend/E2E/package/security/performance gates and failure artifacts.

### Verification Commands
```text
cmake --build build --config Debug --target voxels_app voxels_tests
ctest --test-dir build -C Debug --output-on-failure -R "WebUi(Security|Protocol|Package|Soak|Accessibility)"
npm --prefix ui run lint
npm --prefix ui run test
npm --prefix ui run test:e2e
cpack -C Debug
build/package-smoke/Debug/voxels_app.exe
```

### Completion Evidence
- Changed: security/resource policy, recovery/diagnostics, accessibility fixes, CI/package tests, release records, and instrumentation.
- Observed: offline packaged game loads local UI and survives documented stress/recovery conditions.
- Results: record security/fuzz outcome, soak memory/timings, package hash/path, platform results, and accessibility smoke observations.
- Known gaps: final shipping-path ImGui removal is WI-08.
