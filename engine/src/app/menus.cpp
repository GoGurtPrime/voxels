/**
 * @file menus.cpp
 * @brief Implementation of the menu logic controllers declared in `menus.hpp`.
 */

#include "voxels/app/menus.hpp"

#include <algorithm>
#include <cctype>

#include "voxels/world/generation_pipeline.hpp"

namespace voxels {

std::uint64_t SeedFromText(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char character : text) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool IsFilesystemSafeWorldName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 48 || name == "." || name == "..") return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return std::isalnum(character) || character == ' ' || character == '_' || character == '-';
    });
}

void MainMenuModel::RefreshSaves(const ISaveManager& saveManager) {
    m_saves = saveManager.ListSaves();
}

GameSave WorldCreationController::BuildGameSave(const std::string& playerName) const {
    GameSave save{};
    save.saveName = m_worldName;
    save.worldName = m_worldName;
    save.playerName = playerName;
    save.seed = static_cast<WorldSeed>(m_options.seed);
    save.generatorVersion = WorldGenerator::kGeneratorVersion;
    save.publicVisibility = m_options.isPublic;
    return save;
}

void PauseMenuController::Apply(PauseMenuAction action) {
    if (action == PauseMenuAction::ToggleVisibility) {
        m_activeSave->publicVisibility = !m_activeSave->publicVisibility;
    }
    // Resume / OpenSettings / SaveAndQuit are handled by the app state machine transition
    // that dispatches this action; no local state changes are required here.
}

std::string_view ToString(GenerationPhase phase) noexcept {
    switch (phase) {
        case GenerationPhase::Shape: return "Shape";
        case GenerationPhase::Caves: return "Caves";
        case GenerationPhase::Vegetation: return "Vegetation";
        case GenerationPhase::SpawnPlacement: return "SpawnPlacement";
        case GenerationPhase::Complete: return "Complete";
    }
    return "Unknown";
}

float LoadingScreenModel::GetProgress() const noexcept {
    switch (m_phase) {
        case GenerationPhase::Shape: return 0.0f;
        case GenerationPhase::Caves: return 0.25f;
        case GenerationPhase::Vegetation: return 0.5f;
        case GenerationPhase::SpawnPlacement: return 0.75f;
        case GenerationPhase::Complete: return 1.0f;
    }
    return 0.0f;
}

} // namespace voxels
