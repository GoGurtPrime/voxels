#pragma once

/**
 * @file menus.hpp
 * @brief Logic-layer controllers for the main menu, world creation, pause menu, and
 *        loading screen (work item 08).
 *
 * @details These controllers hold state and behavior only; actual widget rendering is
 *          performed by the UI layer (voxels/ui/ui_manager.hpp) so the menu flow can be
 *          exercised in headless unit tests without a Dear ImGui backend or a graphics
 *          device. The app state machine constructs and drives them while transitioning
 *          through `MainMenuState`, `WorldCreationState`, `PauseMenuState`, and
 *          `LoadingScreenState`.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "voxels/core/save.hpp"
#include "voxels/world/world_options.hpp"

namespace voxels {

/// Backing model for the main menu's save list; refreshed from an `ISaveManager`.
class MainMenuModel {
public:
    void RefreshSaves(const ISaveManager& saveManager);
    [[nodiscard]] const std::vector<SaveSlot>& GetSaves() const noexcept { return m_saves; }

private:
    std::vector<SaveSlot> m_saves;
};

/// Collects the world creation screen's inputs (name, seed, and gameplay toggles) and builds
/// the `GameSave` + `WorldOptions` pair consumed by the world generation pipeline.
class WorldCreationController {
public:
    void SetWorldName(std::string name) noexcept { m_worldName = std::move(name); }
    void SetSeed(WorldSeed seed) noexcept { m_options.seed = seed; }
    void SetPeaceful(bool value) noexcept { m_options.peaceful = value; }
    void SetPermadeath(bool value) noexcept { m_options.permadeath = value; }
    void SetAlwaysSunny(bool value) noexcept { m_options.alwaysSunny = value; }
    void SetSandboxMode(bool value) noexcept { m_options.sandboxMode = value; }
    void SetPublic(bool isPublic) noexcept { m_options.isPublic = isPublic; }
    void SetRenderDistance(int value) noexcept { m_options.renderDistanceChunks = value; }

    [[nodiscard]] const std::string& GetWorldName() const noexcept { return m_worldName; }
    [[nodiscard]] const WorldOptions& GetWorldOptions() const noexcept { return m_options; }

    /// Builds a fresh `GameSave` (save/world name, seed, current generator version,
    /// visibility); timestamps and play time are filled in later by the save flow.
    [[nodiscard]] GameSave BuildGameSave(const std::string& playerName) const;

private:
    std::string m_worldName;
    WorldOptions m_options;
};

/// Deterministic FNV-1a hash of the seed text box, so any phrase maps to a stable seed.
[[nodiscard]] std::uint64_t SeedFromText(std::string_view text) noexcept;
/// True if `name` is usable as a save directory name: 1-48 chars, alphanumeric/space/'_'/'-'
/// only, and not "." or "..".
[[nodiscard]] bool IsFilesystemSafeWorldName(std::string_view name) noexcept;

/// Actions exposed by the pause menu; the app layer maps these onto state machine transitions.
enum class PauseMenuAction {
    Resume,
    OpenSettings,
    ToggleVisibility,
    SaveAndQuit
};

/// Wraps the active `GameSave` so the pause menu can flip world visibility in place.
class PauseMenuController {
public:
    explicit PauseMenuController(GameSave& activeSave) noexcept : m_activeSave(&activeSave) {}

    /// Only `ToggleVisibility` mutates the save; the other actions are handled by the app
    /// state machine transition that dispatched them.
    void Apply(PauseMenuAction action);
    [[nodiscard]] bool IsPublic() const noexcept { return m_activeSave->publicVisibility; }

private:
    GameSave* m_activeSave;
};

/// Ordered generation phases surfaced by the loading screen's progress bar.
enum class GenerationPhase {
    Shape,
    Caves,
    Vegetation,
    SpawnPlacement,
    Complete
};

/// Stable phase name for logging and the loading screen's status line.
[[nodiscard]] std::string_view ToString(GenerationPhase phase) noexcept;

/// Tracks the current generation phase and exposes a 0..1 progress fraction for the loading
/// screen's progress bar.
class LoadingScreenModel {
public:
    void SetPhase(GenerationPhase phase) noexcept { m_phase = phase; }
    [[nodiscard]] GenerationPhase GetPhase() const noexcept { return m_phase; }
    /// Fixed 0.25 step per phase (Shape=0 .. Complete=1); not interpolated within a phase.
    [[nodiscard]] float GetProgress() const noexcept;

private:
    GenerationPhase m_phase = GenerationPhase::Shape;
};

} // namespace voxels
