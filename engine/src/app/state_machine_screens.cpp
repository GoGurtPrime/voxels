/**
 * @file state_machine_screens.cpp
 * @brief ImGui implementations for the application menu and overlay states.
 *
 * @details Renders the state flow in DIAGRAMS.md section 3 while delegating transitions through
 *          AppContext so screens remain independent of main.cpp ownership.
 */

#include "voxels/app/state_machine.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cfloat>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

#include <imgui.h>

#include "voxels/app/save_manager.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/core/preferences.hpp"
#include "voxels/ui/imgui_ui_manager.hpp"

namespace voxels {
namespace {
constexpr ImGuiWindowFlags kMenuWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;

void CenterNextWindow() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
}

void BeginMenuFrame(AppContext* context) {
    if (context != nullptr && context->renderer != nullptr) context->renderer->BeginFrame({0.12f, 0.16f, 0.19f, 1.0f});
}

void SetResponsivePanelSize(float preferredWidth, float preferredHeight) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float horizontalMargin = 32.0f;
    const float verticalMargin = 32.0f;
    ImGui::SetNextWindowSize({std::clamp(display.x * 0.78f, preferredWidth, display.x - horizontalMargin),
                              std::clamp(display.y * 0.82f, preferredHeight, display.y - verticalMargin)}, ImGuiCond_Always);
}

bool BeginSettingsTable(const char* id) {
    return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV);
}

void EndSettingsRow() { ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); }

std::string TimestampNow() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M");
    return stream.str();
}

void ConfigureInGameState(InGameState& state, AppContext* context, const GameSave& save) {
    state.SetBlockRegistry(context->blockRegistry);
    state.SetTextureAtlas(context->textureAtlas);
    state.SetWorldOptions(WorldOptions{.seed = save.seed, .isPublic = save.publicVisibility});
    state.SetInputManager(context->input);
    state.SetPlatform(context->platform);
    state.SetActiveSave(save);
}
} // namespace

void MainMenuState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Menu);
    if (m_context != nullptr && m_context->input != nullptr) m_context->input->ClearGameplayInput();
}

void MainMenuState::Render() {
    if (m_context == nullptr || m_context->ui == nullptr) return;
    BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({390.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Voxel World", nullptr, kMenuWindowFlags);
    ui::MenuTitle("VOXELS ENGINE");
    ImGui::TextUnformatted("A block-based world is waiting.");
    ImGui::Spacing();
    if (ui::MenuButton("Play")) {
        if (m_context->saveManager != nullptr && m_context->saveManager->ListSaves().empty()) {
            m_context->requestTransition(std::make_unique<WorldCreationState>(m_context));
        } else {
            m_context->requestTransition(std::make_unique<WorldSelectState>(m_context));
        }
    }
    if (ui::MenuButton("Settings")) m_context->requestPushOverlay(std::make_unique<SettingsState>(m_context));
    if (ui::MenuButton("Quit")) m_context->requestQuit();
    ImGui::Separator();
    ImGui::TextDisabled("VoxelsEngine 0.1.0");
    ImGui::End();
}

void WorldSelectState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Menu);
    if (m_context != nullptr && m_context->saveManager != nullptr) m_saves = m_context->saveManager->ListSaves();
}

void WorldSelectState::Update(double) {}

void WorldSelectState::Render() {
    if (m_context == nullptr || m_context->saveManager == nullptr) return;
    BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({560.0f, 460.0f}, ImGuiCond_Always);
    ImGui::Begin("Select World", nullptr, kMenuWindowFlags);
    ui::MenuTitle("SELECT WORLD");
    if (m_saves.empty()) ImGui::TextDisabled("No worlds yet. Create one to begin.");
    for (int index = 0; index < static_cast<int>(m_saves.size()); ++index) {
        const SaveSlot& slot = m_saves[static_cast<std::size_t>(index)];
        const std::string label = slot.save.worldName + "###save" + std::to_string(index);
        if (ui::SaveListEntry(label.c_str(), index == m_selectedSave)) m_selectedSave = index;
        ImGui::SameLine();
        ImGui::TextDisabled("Seed %u  %s", slot.save.seed, slot.save.lastPlayedAt.c_str());
    }
    ImGui::Spacing();
    if (ui::MenuButton("New World")) m_context->requestTransition(std::make_unique<WorldCreationState>(m_context));
    const bool hasSelection = m_selectedSave >= 0 && m_selectedSave < static_cast<int>(m_saves.size());
    if (ui::MenuButton("Play Selected", hasSelection)) {
        auto loading = std::make_unique<LoadingScreenState>(m_context);
        loading->SetSaveManager(*m_context->saveManager);
        loading->SetSaveName(m_saves[static_cast<std::size_t>(m_selectedSave)].slotName);
        m_context->requestTransition(std::move(loading));
    }
    if (ui::MenuButton("Delete Selected", hasSelection)) m_confirmDelete = true;
    if (ui::MenuButton("Back")) m_context->requestTransition(std::make_unique<MainMenuState>(m_context));
    if (m_confirmDelete && hasSelection) {
        ImGui::OpenPopup("Delete World");
        if (ImGui::BeginPopupModal("Delete World", &m_confirmDelete, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Type the world name to delete it:");
            ImGui::TextUnformatted(m_saves[static_cast<std::size_t>(m_selectedSave)].save.worldName.c_str());
            ui::TextField("Confirmation", m_deleteConfirmation);
            const bool valid = m_deleteConfirmation == m_saves[static_cast<std::size_t>(m_selectedSave)].save.worldName;
            if (ImGui::Button("Delete") && valid) {
                m_context->saveManager->DeleteSave(m_saves[static_cast<std::size_t>(m_selectedSave)].slotName);
                m_saves = m_context->saveManager->ListSaves();
                m_selectedSave = -1;
                m_deleteConfirmation.clear();
                m_confirmDelete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { m_confirmDelete = false; m_deleteConfirmation.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

WorldCreationController* WorldCreationState::GetController() noexcept { return m_controller.get(); }

void WorldCreationState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::TextEntry);
    m_controller = std::make_unique<WorldCreationController>();
    m_controller->SetWorldName("New World");
}

void WorldCreationState::Update(double) {}

void WorldCreationState::Render() {
    if (m_context == nullptr || m_controller == nullptr) return;
    BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({500.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Create World", nullptr, kMenuWindowFlags);
    ui::MenuTitle("CREATE WORLD");
    std::string name = m_controller->GetWorldName();
    if (ui::TextField("World Name", name)) m_controller->SetWorldName(std::move(name));
    ui::TextField("Seed (blank = random)", m_seedText);
    WorldOptions options = m_controller->GetWorldOptions();
    bool sandbox = options.sandboxMode;
    bool peaceful = options.peaceful;
    bool permadeath = options.permadeath;
    bool alwaysSunny = options.alwaysSunny;
    bool publicWorld = options.isPublic;
    int renderDistance = options.renderDistanceChunks;
    ui::SettingToggle("Creative Mode", &sandbox);
    ui::SettingToggle("Peaceful", &peaceful);
    ui::SettingToggle("Permadeath", &permadeath);
    ui::SettingToggle("Always Day", &alwaysSunny);
    ui::SettingToggle("Public LAN World", &publicWorld);
    ImGui::SliderInt("Render Distance", &renderDistance, 2, 16, "%d chunks");
    m_controller->SetSandboxMode(sandbox);
    m_controller->SetPeaceful(peaceful);
    m_controller->SetPermadeath(permadeath);
    m_controller->SetAlwaysSunny(alwaysSunny);
    m_controller->SetPublic(publicWorld);
    m_controller->SetRenderDistance(renderDistance);
    if (!m_error.empty()) ImGui::TextColored({0.95f, 0.35f, 0.25f, 1.0f}, "%s", m_error.c_str());
    if (ui::MenuButton("Create")) {
        if (!IsFilesystemSafeWorldName(m_controller->GetWorldName())) {
            m_error = "World names use letters, numbers, spaces, hyphens, and underscores only.";
        } else if (m_context->saveManager == nullptr) {
            m_error = "Save service is unavailable.";
        } else if (m_context->saveManager->GetSaveDirectory(m_controller->GetWorldName()).lexically_normal().filename() != m_controller->GetWorldName()) {
            m_error = "World name is invalid.";
        } else if (std::filesystem::exists(m_context->saveManager->GetSaveDirectory(m_controller->GetWorldName()))) {
            m_error = "A world with that name already exists.";
        } else {
            WorldOptions createdOptions = m_controller->GetWorldOptions();
            if (m_seedText.empty()) createdOptions.seed = std::random_device{}(); else createdOptions.seed = SeedFromText(m_seedText);
            GameSave save = m_controller->BuildGameSave("Player");
            save.seed = static_cast<WorldSeed>(createdOptions.seed);
            save.publicVisibility = createdOptions.isPublic;
            save.lastPlayedAt = TimestampNow();
            if (!m_context->saveManager->Save(save)) {
                m_error = "Could not create the world directory.";
            } else {
                auto loading = std::make_unique<LoadingScreenState>(m_context);
                loading->SetSaveManager(*m_context->saveManager);
                loading->SetSaveName(save.saveName);
                loading->SetWorldOptions(createdOptions);
                m_context->requestTransition(std::move(loading));
            }
        }
    }
    if (ui::MenuButton("Back")) m_context->requestTransition(std::make_unique<MainMenuState>(m_context));
    ImGui::End();
}

void LoadingScreenState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Menu);
    m_generationComplete = false;
    RunGeneration();
}

void LoadingScreenState::Update(double) {
    if (m_world == nullptr || m_generatedChunks < m_generationQueue.size()) {
        if (m_world == nullptr) {
            m_context->requestTransition(std::make_unique<ErrorState>(m_context, "World Generation Failed", "The loading world could not be initialized."));
            return;
        }
        const ChunkCoordinate coordinate = m_generationQueue[m_generatedChunks];
        if (coordinate.y == 3) m_world->GetOrCreateChunk(coordinate);
        else {
            WorldGenerator generator(m_options);
            m_world->GetOrCreateChunk(coordinate) = generator.GenerateChunk(coordinate);
        }
        ++m_generatedChunks;
        const float fraction = static_cast<float>(m_generatedChunks) / static_cast<float>(m_generationQueue.size());
        m_phase = fraction < 0.33f ? GenerationPhase::Shape : fraction < 0.66f ? GenerationPhase::Caves : GenerationPhase::Vegetation;
        return;
    }
    m_phase = GenerationPhase::SpawnPlacement;
    m_generationComplete = true;
    if (m_context == nullptr || m_context->saveManager == nullptr) {
        m_phase = GenerationPhase::Complete;
        return;
    }
    GameSave save{};
    if (!m_context->saveManager->Load(m_saveName, save)) {
        m_context->requestTransition(std::make_unique<ErrorState>(m_context, "World Load Failed", "The selected world metadata could not be read."));
        return;
    }
    auto game = std::make_unique<InGameState>(m_context);
    ConfigureInGameState(*game, m_context, save);
    game->SetPreparedWorld(ReleaseGeneratedWorld());
    m_phase = GenerationPhase::Complete;
    m_context->requestTransition(std::move(game));
}

void LoadingScreenState::Render() {
        BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({460.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Loading", nullptr, kMenuWindowFlags);
    ui::MenuTitle("LOADING...");
    ImGui::Text("Generating spawn terrain: %s", ToString(m_phase).data());
    const float chunkProgress = m_generationQueue.empty() ? 0.0f : static_cast<float>(m_generatedChunks) / static_cast<float>(m_generationQueue.size());
    const std::string percent = std::to_string(static_cast<int>(chunkProgress * 100.0f)) + "%";
    ui::ProgressBar(chunkProgress, percent.c_str());
    ImGui::Text("Chunks ready: %zu / %zu", m_generatedChunks, m_generationQueue.size());
    ImGui::End();
}

void PauseMenuState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Menu);
    if (m_context != nullptr && m_context->input != nullptr) m_context->input->ClearGameplayInput();
}
void PauseMenuState::OnExit() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Gameplay);
    if (m_context != nullptr && m_context->input != nullptr) m_context->input->ClearGameplayInput();
}
void PauseMenuState::Update(double) {}
void PauseMenuState::Render() {
    if (m_context == nullptr) return;
    CenterNextWindow();
    ImGui::SetNextWindowSize({390.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Paused", nullptr, kMenuWindowFlags);
    ui::MenuTitle("PAUSED");
    if (ui::MenuButton("Resume")) m_context->requestPopOverlay();
    if (ui::MenuButton("Settings")) m_context->requestPushOverlay(std::make_unique<SettingsState>(m_context));
    if (ui::MenuButton(m_activeSave.publicVisibility ? "Set World Private" : "Set World Public")) {
        m_activeSave.publicVisibility = !m_activeSave.publicVisibility;
        if (m_context->saveManager != nullptr) m_context->saveManager->Save(m_activeSave);
    }
    if (ui::MenuButton("Save and Quit to Menu")) {
        m_activeSave.lastPlayedAt = TimestampNow();
        if (m_context->saveManager != nullptr) m_context->saveManager->Save(m_activeSave);
        m_context->requestTransition(std::make_unique<MainMenuState>(m_context));
    }
    if (ui::MenuButton("Save and Exit to Desktop")) {
        m_activeSave.lastPlayedAt = TimestampNow();
        if (m_context->saveManager != nullptr) m_context->saveManager->Save(m_activeSave);
        m_context->requestQuit();
    }
    ImGui::End();
}

void SettingsState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Menu);
    if (m_context != nullptr && m_context->preferences != nullptr) m_pending = *m_context->preferences;
}
void SettingsState::Update(double) {}
void SettingsState::Render() {
    if (m_context == nullptr || m_context->preferences == nullptr) return;
    CenterNextWindow();
    SetResponsivePanelSize(760.0f, 560.0f);
    ImGui::Begin("Settings", nullptr, kMenuWindowFlags);
    ui::MenuTitle("SETTINGS");
    if (ImGui::BeginTabBar("Settings Tabs")) {
        if (ImGui::BeginTabItem("Video")) {
            if (BeginSettingsTable("VideoSettings")) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Field of View"); EndSettingsRow(); ui::SettingSlider("##fov", &m_pending.fieldOfView, 60.0f, 110.0f, "%.0f deg");
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Render Distance"); EndSettingsRow(); ImGui::SliderInt("##renderDistance", &m_pending.renderDistance, 2, 16, "%d chunks");
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Simulation Distance"); EndSettingsRow(); ImGui::SliderInt("##simulationDistance", &m_pending.simulationDistance, 2, 12, "%d chunks");
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio")) {
            if (BeginSettingsTable("AudioSettings")) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Master Volume"); EndSettingsRow(); ui::SettingPercentSlider("##masterVolume", &m_pending.masterVolume);
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Music Volume"); EndSettingsRow(); ui::SettingPercentSlider("##musicVolume", &m_pending.musicVolume);
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Effects Volume"); EndSettingsRow(); ui::SettingPercentSlider("##effectsVolume", &m_pending.sfxVolume);
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Audio output arrives with work item 11.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
            if (BeginSettingsTable("ControlsSettings")) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Mouse Sensitivity"); EndSettingsRow(); ui::SettingSlider("##mouseSensitivity", &m_pending.mouseSensitivity, 0.1f, 4.0f, "%.1f");
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Invert Y"); EndSettingsRow(); ui::SettingToggle("##invertY", &m_pending.invertY);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gameplay")) {
            ui::SettingToggle("Particles", &m_pending.particles);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (ui::MenuButton("Apply")) {
        *m_context->preferences = m_pending;
        if (m_context->activeGame != nullptr) m_context->activeGame->ApplyPreferences(m_pending);
        PreferencesManager manager(Paths::UserDataDir() / "settings.json", m_context->platform->GetContext().type);
        manager.Save(m_pending);
    }
    if (ui::MenuButton("Revert")) m_pending = *m_context->preferences;
    if (ui::MenuButton("Back")) m_context->requestPopOverlay();
    ImGui::End();
}

void ErrorState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputContext(InputContext::Menu);
}
void ErrorState::Update(double) {}
void ErrorState::Render() {
    if (m_context == nullptr) return;
    BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({500.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Error", nullptr, kMenuWindowFlags);
    ui::MenuTitle(m_title.c_str());
    ImGui::TextWrapped("%s", m_detail.c_str());
    ImGui::TextDisabled("Logs: %s", Paths::LogsDir().string().c_str());
    if (ui::MenuButton("Return to Main Menu")) m_context->requestTransition(std::make_unique<MainMenuState>(m_context));
    ImGui::End();
}

} // namespace voxels