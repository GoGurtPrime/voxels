# Work Item 00: Git & Repository Hygiene

## 🎯 Objective & Overview
Initialize version control for the project. This is the true first step of the sequence: every subsequent work item assumes the repository is already tracked in Git and that the agent will commit its own work at the end of a successful build+test cycle (see `AGENT_RULES.md` → *Agentic Git Workflow*). This item retroactively brings the existing scaffolded project (work items that may have already been implemented) under version control with one clean baseline commit.

---

## 🔗 Dependencies & Context
* **Prerequisites:** None (First sequence item — run before `01_testing_and_build_infrastructure` even if that work has already been performed).
* **Target Subsystems:** repository root (`.git/`, `.gitignore`, `.gitattributes`)

---

## 📋 Detailed Task Breakdown
1. **Initialize the repository:**
   * Run `git init` at the workspace root if `.git/` does not already exist.
   * Configure the default branch name to `master` or `main` (match whatever the human operator prefers; do not create or switch to feature branches — this project commits directly to the trunk branch).
2. **Create `.gitignore`:**
   * Ignore CMake build output: `/build/`, `**/CMakeFiles/`, `CMakeCache.txt`, `*.vcxproj*`, `*.sln`, `*.slnx`, `Testing/`, `_deps/`.
   * Ignore IDE/editor cruft: `.vs/`, `.vscode/*.log`, `*.user`, `.DS_Store`, `Thumbs.db`.
   * Ignore compiled binaries/objects: `*.obj`, `*.o`, `*.exe`, `*.dll`, `*.lib`, `*.pdb`, `*.ilk`.
   * Ignore local secrets/config overrides: `*.local.json`, `.env`.
   * Do **not** ignore `.vscode/launch.json`, `.vscode/tasks.json`, `.vscode/c_cpp_properties.json`, `.vscode/settings.json` — these are shared developer tooling and must be committed.
3. **Create `.gitattributes` for large binary assets:**
   * Normalize line endings: `* text=auto eol=lf`.
   * Configure Git LFS (or document the equivalent if LFS is unavailable in the environment) for common game-dev binary asset types that will be supplied later, so large files never bloat plain diffs:
     ```
     *.png filter=lfs diff=lfs merge=lfs -text
     *.jpg filter=lfs diff=lfs merge=lfs -text
     *.wav filter=lfs diff=lfs merge=lfs -text
     *.ogg filter=lfs diff=lfs merge=lfs -text
     *.mp3 filter=lfs diff=lfs merge=lfs -text
     *.ttf filter=lfs diff=lfs merge=lfs -text
     *.vpk filter=lfs diff=lfs merge=lfs -text
     *.vmdl filter=lfs diff=lfs merge=lfs -text
     *.fbx filter=lfs diff=lfs merge=lfs -text
     *.glb filter=lfs diff=lfs merge=lfs -text
     *.glTF filter=lfs diff=lfs merge=lfs -text
     ```
   * If `git-lfs` is not installed on the build machine, document the one-time human setup step (`git lfs install`) rather than failing silently — the `.gitattributes` rules should be committed regardless since they take effect once LFS is installed.
4. **Baseline commit:**
   * Stage the entire current project tree (respecting `.gitignore`).
   * Commit with message prefix `Agent:` per the commit message convention in `AGENT_RULES.md`, e.g.:
     `Agent: Initial commit — scaffold engine/app/editor, work items 00-10 baseline`
   * The commit body should briefly enumerate the major subsystems already present (engine core/platform/input/graphics/world/worldgen/audio/app/networking) so future `git log` spelunking gives useful context.

---

## 🧪 Automated Testing Requirements
* No new automated test target is required for this item (it is repository plumbing, not runtime code). Verification is manual:
  * `git status` reports a clean working tree after the commit.
  * `git log -1` shows the baseline commit with the `Agent:` prefix.
  * Confirm `/build/` is not tracked: `git ls-files | grep -i build` returns nothing.

---

## 👤 Human-in-the-Loop Actions Required
* **Git LFS Installation (Human Step, optional but recommended):**
  1. Install Git LFS (`git lfs install`) once per machine before large texture/audio/model assets are added in later work items.
  2. Configure a remote (GitHub/GitLab/etc.) and confirm the LFS storage quota if applicable.
  3. (Procedural Fallback: `.gitattributes` LFS rules are committed regardless; until LFS is installed locally, matching files are simply stored normally in Git, so nothing blocks automated work.)

---

## 🔄 Verification & Self-Healing Protocol
1. Confirm `.git/` exists and `git log -1 --name-status` shows the expected baseline commit.
2. Confirm the build/test loop still works after the commit (`cmake --build build`, `ctest --test-dir build`) — committing must never alter build output.
3. **Self-Healing:** If `git init` finds an existing `.git/` directory (repository already initialized in a prior session), skip re-initialization and only ensure `.gitignore`/`.gitattributes` are present and up to date before committing.
