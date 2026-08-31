# Release Checklist — VoxelsEngine

> Companion to [work_items/18_packaging_distribution_and_platform_services.md](../work_items/18_packaging_distribution_and_platform_services.md).
> Work through every box before tagging a release. Do not skip the clean-machine step — it is
> the acceptance test that actually matters (AGENT_RULES.md §4).

## 1. Versioning

- [ ] Version bumped in the root `project(VoxelsEngine VERSION X.Y.Z ...)` call in [CMakeLists.txt](../CMakeLists.txt).
- [ ] `CHANGELOG.md` updated with what shipped since the last release (create it if this is the first tagged release).
- [ ] The working tree is clean (`git status --porcelain` empty) so the embedded git commit in
      `voxels/core/version.hpp` is not suffixed `-dirty`.

## 2. Build & Test

- [ ] `cmake --build build --config Release` succeeds with zero warnings on the default
      (non-headless, `VOXELS_ENABLE_STEAM=OFF`) configuration.
- [ ] `ctest --test-dir build -C Release --output-on-failure` is 100% green.
- [ ] If a sanitizer/fuzz/soak job exists in CI, it is green (see
      [work_items/17_performance_stability_and_hardening.md](../work_items/17_performance_stability_and_hardening.md)).
- [ ] `voxels_app` was launched (not `--headless`) and played for several minutes without a
      crash, freeze, or leak-driven slowdown.

## 3. Content & Compliance

- [ ] [ASSET_REQUESTS.md](../ASSET_REQUESTS.md) reviewed: no `BLOCKING` entries remain, and every
      `QUALITY` placeholder shipping in this release is acceptable to ship as-is.
- [ ] `THIRD_PARTY_LICENSES.txt` (generated into `build/package/` at configure time) covers every
      bundled dependency, including any newly added since the last release.
- [ ] `LICENSE.txt` reflects the project's actual, current license terms (not the placeholder
      committed at the repository root, if that has not yet been replaced).

## 4. Package Build

For each target platform:

- [ ] `cmake --build build --config Release`
- [ ] `cpack -C Release` (from the build directory) produces the platform archive:
      Windows → `.zip`, Linux → `.tar.gz` (or `.AppImage` once packaged), macOS → `.tar.gz`
      (or `.app` + `.dmg` once a bundle target exists).
- [ ] Record the exact archive name, size, and SHA-256 (CPack writes a `.sha256` file alongside
      the archive) here or in the release notes.

## 5. Clean-Machine Verification

This is the step that matters — a package that only runs on the build machine is not shippable.

- [ ] Copy the archive to a machine/VM with **no** compilers, SDKs, IDEs, or SDL2 installed.
- [ ] Extract it and double-click `voxels_app(.exe)` directly (no terminal, no dev environment).
- [ ] Confirm: a window opens, the main menu renders, a new world can be created and entered,
      movement/break/place work, and audio plays.
- [ ] Confirm the first-run controls card appears on first launch and does not reappear after
      quitting and relaunching.
- [ ] Confirm the version shown in the main-menu corner matches the tagged version and git
      commit.
- [ ] Report the exact result (pass, or what failed) — do not claim this step passed if it was
      not actually performed on a clean machine.

## 6. Optional Steam Build

Only if this release includes a Steam-enabled build (`VOXELS_ENABLE_STEAM=ON`):

- [ ] Configured with `-DVOXELS_ENABLE_STEAM=ON -DSTEAM_SDK_ROOT=<path>` and the configure log
      shows "Steam platform services enabled".
- [ ] Launched with the Steam client running: the overlay opens (Shift+Tab), rich presence
      updates, and unlocking a test achievement (e.g. breaking the first block) shows in the
      Steam client's achievement list.
- [ ] Confirmed `VOXELS_ENABLE_STEAM=OFF` (the default) still builds and behaves identically —
      Steam is genuinely optional, not silently required.

## 7. Tag & Publish

- [ ] Git tag created for the release commit (e.g. `v0.1.0`), pushed to `origin`.
- [ ] Archives (and checksums) attached to the release.
- [ ] Player-facing `README.txt` inside the package reviewed for accuracy (system requirements,
      known limitations, save-file locations per OS).
