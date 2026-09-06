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
#include "voxels/core/logger.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/core/preferences.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/ui/imgui_ui_manager.hpp"

namespace voxels {
namespace {
constexpr ImGuiWindowFlags kMenuWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
constexpr std::size_t kMaximumSeedTextLength = 20;

Logger& ScreenLog() {
    static Logger logger;
    static const bool initialized = [] {
        logger.AddSink(std::make_shared<ConsoleLogSink>());
        return true;
    }();
    (void)initialized;
    return logger;
}

void CenterNextWindow() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
}

void BeginMenuFrame(AppContext* context) {
    if (context != nullptr && context->renderer != nullptr) {
        static_cast<void>(context->renderer->BeginFrame({0.12f, 0.16f, 0.19f, 1.0f}));
    }
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
    state.SetWorldOptions(WorldOptions{.seed = save.seed, .generatorVersion = save.generatorVersion, .isPublic = save.publicVisibility});
    state.SetInputManager(context->input);
    state.SetPlatform(context->platform);
    state.SetActiveSave(save);
}
} // namespace

void MainMenuState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::MainMenu, .revision = 1, .title = "VOXELS ENGINE", .message = "A block-based world is waiting.", .items = {"Play", "Join Game", "Settings", "Quit"}});
    if (m_context != nullptr && m_context->input != nullptr) m_context->input->ClearGameplayInput();
    if (m_context != nullptr && m_context->firstRun && m_context->preferences != nullptr &&
        !m_context->preferences->controlsCardSeen && m_context->requestPushOverlay) {
        m_context->requestPushOverlay(std::make_unique<ControlsCardState>(m_context));
    }
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
    if (ui::MenuButton("Join Game")) m_context->requestTransition(std::make_unique<JoinGameState>(m_context));
    if (ui::MenuButton("Settings")) m_context->requestPushOverlay(std::make_unique<SettingsState>(m_context));
    if (ui::MenuButton("Quit")) m_context->requestQuit();
    ImGui::Separator();
    ImGui::TextDisabled("VoxelsEngine v%s (%s)", kEngineVersion, kEngineGitCommit);
    ImGui::End();
}

void WorldSelectState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->saveManager != nullptr) m_saves = m_context->saveManager->ListSaves();
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::SaveSelection, .revision = 1, .title = "SELECT WORLD", .items = {"New World", "Play Selected", "Delete Selected", "Back"}});
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
    if (ImGui::BeginTable("WorldSaves", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
    for (int index = 0; index < static_cast<int>(m_saves.size()); ++index) {
        const SaveSlot& slot = m_saves[static_cast<std::size_t>(index)];
        const std::string label = slot.save.worldName + "###save" + std::to_string(index);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ui::SaveListEntry(label.c_str(), index == m_selectedSave)) m_selectedSave = index;
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Seed %u  %s", slot.save.seed, slot.save.lastPlayedAt.c_str());
    }
    ImGui::EndTable();
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
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::TextEntry);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::WorldCreation, .revision = 1, .title = "CREATE WORLD", .items = {"World Name", "Seed", "Create", "Back"}});
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
    ui::TextField("Seed (blank = random)", m_seedText, kMaximumSeedTextLength + 1);
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
        } else if (m_seedText.size() > kMaximumSeedTextLength) {
            m_error = "Seed text is limited to 20 characters.";
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
                if (m_context->platformServices != nullptr) {
                    m_context->platformServices->UnlockAchievement(Achievement::FirstWorldCreated);
                }
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

void JoinGameState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::TextEntry);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::Join, .revision = 1, .title = "JOIN GAME", .items = {"Host Address", "UDP Port", "Connect", "Back"}});
}

void JoinGameState::Update(double) {}

void JoinGameState::Render() {
    if (m_context == nullptr) return;
    BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({480.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Join Game", nullptr, kMenuWindowFlags);
    ui::MenuTitle("JOIN GAME");
    ImGui::TextUnformatted("Enter the host's address and UDP port.");
    ImGui::Spacing();
    ui::TextField("Host Address", m_host, 64);
    ui::TextField("UDP Port", m_portText, 6);
    if (!m_error.empty()) ImGui::TextColored({0.95f, 0.35f, 0.25f, 1.0f}, "%s", m_error.c_str());
    if (ui::MenuButton("Connect")) {
        unsigned int port = 0;
        const auto result = std::from_chars(m_portText.data(), m_portText.data() + m_portText.size(), port);
        const bool portValid = result.ec == std::errc{} && result.ptr == m_portText.data() + m_portText.size() &&
                               port > 0 && port <= 65535;
        if (m_host.empty() || !portValid) {
            m_error = "Enter a host address and a port between 1 and 65535.";
        } else if (!m_context->connectRemote || !m_context->connectRemote(m_host, static_cast<std::uint16_t>(port))) {
            m_error = "Could not open a connection to " + m_host + ":" + m_portText + ".";
        } else {
            m_context->requestTransition(
                std::make_unique<JoinLoadingState>(m_context, m_host + ":" + m_portText));
        }
    }
    if (ui::MenuButton("Back")) m_context->requestTransition(std::make_unique<MainMenuState>(m_context));
    ImGui::End();
}

void JoinLoadingState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::Loading, .revision = 1, .title = "JOINING...", .message = m_endpointLabel, .progress = 0.0f, .blocking = true});
    m_world = std::make_unique<World>();
    m_elapsedSeconds = 0.0;
    m_lastProgressSeconds = 0.0;
}

void JoinLoadingState::FailWith(std::string title, std::string detail) {
    if (m_context == nullptr) return;
    Logger& logger = ScreenLog();
    logger.Warn("Join failed for " + m_endpointLabel + ": " + detail);
    if (m_context->resetNetworkToLocal) m_context->resetNetworkToLocal();
    m_context->requestTransition(std::make_unique<ErrorState>(m_context, std::move(title), std::move(detail)));
}

void JoinLoadingState::Update(double deltaSeconds) {
    if (m_context == nullptr || m_context->networkClient == nullptr) return;
    m_elapsedSeconds += deltaSeconds;
    networking::GameClient& client = *m_context->networkClient;
    if (client.WasRejected()) {
        switch (client.GetRejectReason()) {
            case networking::RejectReason::ServerFull:
                FailWith("Join Failed", "The server is full.");
                return;
            case networking::RejectReason::WorldNotReady:
                FailWith("Join Failed", "The host is not in a world yet. Ask them to enter their world, then retry.");
                return;
            case networking::RejectReason::WorldPrivate:
                FailWith("Join Failed", "The host's world is private. Ask them to set it public from the pause menu.");
                return;
        }
    }
    if (client.WasDisconnectedByServer()) {
        FailWith("Join Failed", "The host ended the session.");
        return;
    }
    if (!client.HasReceivedConnectAck()) {
        if (m_elapsedSeconds > kHandshakeTimeoutSeconds) {
            FailWith("Join Failed", "No response from " + m_endpointLabel +
                                        ". Check the address, port, and the host's firewall (UDP).");
        }
        return;
    }
    if (!client.IsWorldReadyOnServer()) {
        FailWith("Join Failed", "The host is not in a world yet. Ask them to enter their world, then retry.");
        return;
    }
    const networking::WorldInfo& info = client.GetWorldInfo();
    const std::size_t appliedBefore = m_applier.AppliedChunkCount();
    (void)m_applier.Apply(client, *m_world, *m_context->blockRegistry);
    if (m_applier.AppliedChunkCount() != appliedBefore) {
        m_lastProgressSeconds = m_elapsedSeconds;
        m_lastAppliedChunks = m_applier.AppliedChunkCount();
    } else if (m_elapsedSeconds - m_lastProgressSeconds > kStreamStallTimeoutSeconds) {
        FailWith("Join Failed", "The world download from " + m_endpointLabel + " stalled.");
        return;
    }
    const int spawnColumnX = static_cast<int>(std::floor(info.spawnPosition.x / 16.0f));
    const int spawnColumnZ = static_cast<int>(std::floor(info.spawnPosition.z / 16.0f));
    for (int z = -kRequiredColumnRadius; z <= kRequiredColumnRadius; ++z) {
        for (int x = -kRequiredColumnRadius; x <= kRequiredColumnRadius; ++x) {
            if (!m_applier.IsColumnComplete(spawnColumnX + x, spawnColumnZ + z)) return;
        }
    }
    WorldOptions options{};
    options.seed = info.seed;
    options.generatorVersion = info.generatorVersion;
    options.sandboxMode = info.sandboxMode;
    options.peaceful = info.peaceful;
    options.alwaysSunny = info.alwaysSunny;
    options.permadeath = info.permadeath;
    ScreenLog().Info("Join complete: entering remote world from " + m_endpointLabel + " (seed " +
                     std::to_string(info.seed) + ", " + std::to_string(m_applier.AppliedChunkCount()) +
                     " chunk sections streamed)");
    auto game = std::make_unique<InGameState>(m_context);
    game->SetBlockRegistry(m_context->blockRegistry);
    game->SetTextureAtlas(m_context->textureAtlas);
    game->SetWorldOptions(options);
    game->SetInputManager(m_context->input);
    game->SetPlatform(m_context->platform);
    game->SetRemoteSession(true);
    game->SetRemoteSpawn(info.spawnPosition);
    game->SetPreparedWorld(std::move(m_world));
    m_context->requestTransition(std::move(game));
}

void JoinLoadingState::Render() {
    BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({460.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Joining", nullptr, kMenuWindowFlags);
    ui::MenuTitle("JOINING...");
    const bool acked = m_context != nullptr && m_context->networkClient != nullptr &&
                       m_context->networkClient->HasReceivedConnectAck();
    ImGui::Text("%s %s", acked ? "Downloading world from" : "Contacting", m_endpointLabel.c_str());
    constexpr float kExpectedColumns = 9.0f;
    const float progress = acked
        ? 0.15f + 0.85f * std::min(1.0f, static_cast<float>(m_applier.CompletedColumnCount()) / kExpectedColumns)
        : std::min(0.15f, static_cast<float>(m_elapsedSeconds / kHandshakeTimeoutSeconds) * 0.15f);
    const std::string percent = std::to_string(static_cast<int>(progress * 100.0f)) + "%";
    ui::ProgressBar(progress, percent.c_str());
    ImGui::Text("Chunk sections received: %zu", m_applier.AppliedChunkCount());
    ImGui::End();
}

void LoadingScreenState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::Loading, .revision = 1, .title = "LOADING...", .progress = 0.0f, .blocking = true});
    m_generationComplete = false;
    RunGeneration();
}

void LoadingScreenState::OnExit() {
    if (m_generationJobs != nullptr) {
        m_generationJobs->Shutdown();
        m_generationJobs.reset();
    }
    m_generationResults.clear();
}

void LoadingScreenState::Update(double) {
    if (m_options.generatorVersion > WorldGenerator::kGeneratorVersion) {
        m_context->requestTransition(std::make_unique<ErrorState>(m_context, "World Load Failed", "This world uses a newer generator version."));
        return;
    }
    if (m_world == nullptr || m_generatedChunks < m_generationQueue.size()) {
        if (m_world == nullptr) {
            m_context->requestTransition(std::make_unique<ErrorState>(m_context, "World Generation Failed", "The loading world could not be initialized."));
            return;
        }
        if (m_generationResults.empty()) {
            const ChunkCoordinate coordinate = m_generationQueue[m_generatedChunks];
            m_world->GetOrCreateChunk(coordinate) = WorldGenerator(m_options).GenerateChunk(coordinate);
            if (coordinate.y == 7) m_world->GetOrCreateChunk({coordinate.x, 8, coordinate.z});
            ++m_generatedChunks;
        } else {
            constexpr std::size_t kMaxChunksIntegratedPerFrame = 2;
            std::size_t applied = 0;
            auto result = m_generationResults.begin();
            while (result != m_generationResults.end() && applied < kMaxChunksIntegratedPerFrame) {
                if (result->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    ++result;
                    continue;
                }
                m_world->GetOrCreateChunk(result->coordinate) = result->result.get();
                if (result->coordinate.y == 7) {
                    m_world->GetOrCreateChunk({result->coordinate.x, 8, result->coordinate.z});
                }
                ++m_generatedChunks;
                ++applied;
                result = m_generationResults.erase(result);
            }
            if (applied == 0) return;
        }
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
    for (const auto& [coordinate, chunk] : m_world->GetChunks()) {
        (void)coordinate;
        chunk->ClearDirty();
    }
    if (!m_context->saveManager->LoadWorldState(m_saveName, *m_world) &&
        std::filesystem::exists(m_context->saveManager->GetSaveDirectory(m_saveName) / "regions")) {
        m_context->requestTransition(std::make_unique<ErrorState>(m_context, "World Load Failed", "The world region data could not be read."));
        return;
    }
    const Vec3 savedSpawn{save.spawnX, save.spawnY, save.spawnZ};
    if (save.spawnY <= 0.0f || !IsSafePlayerSpawn(*m_world, savedSpawn)) {
        m_spawnPosition = FindSafeSpawn(*m_world);
        save.spawnX = static_cast<float>(m_spawnPosition.x) + 0.5f;
        save.spawnY = static_cast<float>(m_spawnPosition.y) + 1.9f;
        save.spawnZ = static_cast<float>(m_spawnPosition.z) + 0.5f;
        m_context->saveManager->Save(save);
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
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::Pause, .revision = 1, .title = "PAUSED", .items = {"Resume", "Settings", "Save and Quit"}});
    if (m_context != nullptr && m_context->input != nullptr) m_context->input->ClearGameplayInput();
}
void PauseMenuState::OnExit() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Gameplay);
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
    if (ui::MenuButton("Controls")) m_context->requestPushOverlay(std::make_unique<ControlsCardState>(m_context));
    if (m_remoteSession) {
        ImGui::Separator();
        if (ui::MenuButton("Leave Server")) {
            if (m_context->resetNetworkToLocal) m_context->resetNetworkToLocal();
            m_context->requestTransition(std::make_unique<MainMenuState>(m_context));
        }
        if (ui::MenuButton("Leave and Exit to Desktop")) {
            if (m_context->resetNetworkToLocal) m_context->resetNetworkToLocal();
            m_context->requestQuit();
        }
        ImGui::End();
        return;
    }
    if (ui::MenuButton(m_activeSave.publicVisibility ? "Set World Private" : "Set World Public")) {
        m_activeSave.publicVisibility = !m_activeSave.publicVisibility;
        if (m_context->saveManager != nullptr) m_context->saveManager->Save(m_activeSave);
        if (m_context->activeGame != nullptr) {
            // Visibility gates non-loopback joiners server-side, so republish the world.
            WorldOptions options{.seed = m_activeSave.seed,
                                 .generatorVersion = m_activeSave.generatorVersion,
                                 .isPublic = m_activeSave.publicVisibility};
            if (m_context->networkServer != nullptr) {
                m_context->networkServer->SetWorldReady(options,
                                                        {m_activeSave.spawnX, m_activeSave.spawnY, m_activeSave.spawnZ});
            }
        }
    }
    if (m_context->networkServer != nullptr && m_context->networkServer->IsRunning()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Hosting on UDP port %u (%s)", m_context->networkServer->Port(),
                            m_activeSave.publicVisibility ? "public: LAN players can join this machine's IP"
                                                          : "private: this machine only");
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
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::Settings, .revision = 1, .title = "SETTINGS", .items = {"Apply", "Back"}});
    if (m_context != nullptr && m_context->preferences != nullptr) m_pending = *m_context->preferences;
    if (m_context != nullptr && m_context->renderer != nullptr) {
        m_activeRenderer = m_context->renderer->GetBackend();
        if (m_pending.rendererBackend == RendererBackend::Automatic) m_pending.rendererBackend = m_activeRenderer;
    }
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
                const PlatformType platformType = m_context->platform != nullptr
                                                      ? m_context->platform->GetContext().type
                                                      : PlatformType::Unknown;
                const std::vector<RendererBackend> available = graphics::AvailableRendererBackends(platformType);
                std::vector<const char*> labels;
                labels.reserve(available.size());
                int selectedBackend = 0;
                for (std::size_t index = 0; index < available.size(); ++index) {
                    labels.push_back(graphics::RendererBackendName(available[index]).data());
                    if (available[index] == m_pending.rendererBackend) selectedBackend = static_cast<int>(index);
                }
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Renderer"); EndSettingsRow();
                if (!labels.empty() && ImGui::Combo("##renderer", &selectedBackend, labels.data(), static_cast<int>(labels.size()))) {
                    m_pending.rendererBackend = available[static_cast<std::size_t>(selectedBackend)];
                }
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Field of View"); EndSettingsRow(); ui::SettingSlider("##fov", &m_pending.fieldOfView, 60.0f, 110.0f, "%.0f deg");
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Render Distance"); EndSettingsRow(); ImGui::SliderInt("##renderDistance", &m_pending.renderDistance, 2, 16, "%d chunks");
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Simulation Distance"); EndSettingsRow(); ImGui::SliderInt("##simulationDistance", &m_pending.simulationDistance, 2, 12, "%d chunks");
                ImGui::EndTable();
            }
            if (m_pending.rendererBackend != m_activeRenderer) {
                ImGui::TextColored({0.95f, 0.24f, 0.20f, 1.0f}, "Restart required to change renderer");
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
        if (m_context->audio != nullptr) {
            m_context->audio->ApplyVolumes(m_pending.masterVolume, m_pending.musicVolume,
                                           m_pending.sfxVolume, 0.7f);
        }
        PreferencesManager manager(Paths::UserDataDir() / "settings.json", m_context->platform->GetContext().type);
        manager.Save(m_pending);
    }
    if (ui::MenuButton("Revert")) m_pending = *m_context->preferences;
    if (ui::MenuButton("Back")) m_context->requestPopOverlay();
    ImGui::End();
}

void ControlsCardState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::ControlsCard, .revision = 1, .title = "CONTROLS", .items = {"Got it"}});
}
void ControlsCardState::Update(double) {}
void ControlsCardState::Render() {
    if (m_context == nullptr) return;
    BeginMenuFrame(m_context);
    CenterNextWindow();
    ImGui::SetNextWindowSize({460.0f, 0.0f}, ImGuiCond_Always);
    ImGui::Begin("Controls", nullptr, kMenuWindowFlags);
    ui::MenuTitle("CONTROLS");
    if (ImGui::BeginTable("ControlsCard", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        const std::pair<const char*, const char*> rows[] = {
            {"Move", "W A S D"},
            {"Look", "Mouse"},
            {"Jump", "Space"},
            {"Sprint", "Left Shift"},
            {"Break Block", "Left Click"},
            {"Place Block", "Right Click"},
            {"Hotbar Slot", "1 - 9 / Mouse Wheel"},
            {"Pause / Back", "Escape"},
            {"Debug Overlay", "F3"},
            {"Screenshot", "F2"},
        };
        for (const auto& [action, keys] : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(action);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", keys);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ui::MenuButton("Got it")) {
        if (m_context->preferences != nullptr && m_context->platform != nullptr) {
            m_context->preferences->controlsCardSeen = true;
            PreferencesManager manager(Paths::UserDataDir() / "settings.json", m_context->platform->GetContext().type);
            manager.Save(*m_context->preferences);
        }
        m_context->requestPopOverlay();
    }
    ImGui::End();
}

void ErrorState::OnEnter() {
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->Publish({.route = PlayerUIRoute::Error, .revision = 1, .title = m_title, .message = m_detail, .items = {"Acknowledge"}, .blocking = true});
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