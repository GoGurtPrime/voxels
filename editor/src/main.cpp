/**
 * @file main.cpp
 * @brief Desktop ImGui application for authoring voxel content packs.
 *
 * @details Composes the real SDL2 platform, OpenGL renderer, and ImGui UI pass used by the
 *          game. Editing behavior lives in EditorDocument so this file remains a thin visual
 *          client of the VMDL and VPK content pipeline from ARCHITECTURE.md section 6.6.
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <imgui.h>

#include "editor_document.hpp"
#include "gpu_model_preview.hpp"
#include "voxels/assets/texture_loader.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/engine.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/platform/platform.hpp"
#include "voxels/render/texture_forge.hpp"
#include "voxels/ui/imgui_ui_manager.hpp"

namespace {

class EditorWindowEvents final : public voxels::IPlatformEventListener {
public:
    EditorWindowEvents(bool& running, bool& requestClose, voxels::graphics::GLRenderer& renderer)
        : m_running(running), m_requestClose(requestClose), m_renderer(renderer) {}
    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        if (event.type == voxels::PlatformEventType::WindowClosed || event.type == voxels::PlatformEventType::QuitRequested) m_requestClose = true;
        if (event.type == voxels::PlatformEventType::WindowResized) m_renderer.SetViewport(event.width, event.height);
    }
    void CloseNow() noexcept { m_running = false; }
private:
    bool& m_running;
    bool& m_requestClose;
    voxels::graphics::GLRenderer& m_renderer;
};

struct BlockDraft { std::string id = "authored_block"; std::string displayName = "Authored Block"; std::string modelId = "models/authored_block.vmdl"; std::string textureId = "blocks/authored_block"; int numericId = 1; float hardness = 1.0F; bool solid = true; bool opaque = true; };
enum class EditTool { AddRemove, Paint, Fill, Eyedropper, Select };

std::size_t VoxelIndex(const voxels::VoxelModel& model, std::uint32_t x, std::uint32_t y, std::uint32_t z) { return x + static_cast<std::size_t>(model.gridSize[0]) * (y + static_cast<std::size_t>(model.gridSize[1]) * z); }

bool BundleFromArguments(int argc, char** argv, int& result) {
    std::string input, output;
    bool bundle = false;
    for (int index = 1; index < argc; ++index) { const std::string argument(argv[index]); bundle = bundle || argument == "--bundle"; if (argument.rfind("--input=", 0) == 0) input = argument.substr(8); if (argument.rfind("--output=", 0) == 0) output = argument.substr(9); }
    if (!bundle) return false;
    if (input.empty() || output.empty()) { std::cerr << "Usage: voxels_editor --bundle --input=<content_dir> --output=<pack.vpk>\n"; result = 2; return true; }
    voxels::AssetBundleReport report;
    std::string error;
    if (!voxels::AssetBundler::Bundle(input, output, report, error)) { std::cerr << "Bundle failed: " << error << '\n'; result = 1; return true; }
    std::cout << "Bundle wrote " << report.archive.entryCount << " entries to " << output << ".\n";
    result = 0;
    return true;
}

void RenderSliceEditor(voxels::editor::EditorDocument& document, int& slice, std::uint16_t& selectedPalette, EditTool tool) {
    auto& model = document.Model();
    slice = std::clamp(slice, 0, static_cast<int>(model.gridSize[2]) - 1);
    ImGui::SliderInt("Slice Z", &slice, 0, static_cast<int>(model.gridSize[2]) - 1);
    const float cell = std::min(28.0F, std::max(10.0F, ImGui::GetContentRegionAvail().x / model.gridSize[0]));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (std::uint32_t y = 0; y < model.gridSize[1]; ++y) for (std::uint32_t x = 0; x < model.gridSize[0]; ++x) {
        const std::uint16_t palette = model.voxels[VoxelIndex(model, x, model.gridSize[1] - 1U - y, static_cast<std::uint32_t>(slice))];
        ImVec4 color{0.12F, 0.13F, 0.12F, 1.0F};
        if (palette != 0) { const auto& entry = model.palette[palette - 1U]; color = {entry.red / 255.0F, entry.green / 255.0F, entry.blue / 255.0F, entry.alpha / 255.0F}; }
        const ImVec2 minimum{origin.x + x * cell, origin.y + y * cell};
        ImGui::SetCursorScreenPos(minimum); ImGui::PushID(static_cast<int>(x + model.gridSize[0] * y));
        if (ImGui::InvisibleButton("voxel", {cell, cell})) {
            const voxels::editor::VoxelCoordinate coordinate{x, model.gridSize[1] - 1U - y, static_cast<std::uint32_t>(slice)};
            if (tool == EditTool::AddRemove) static_cast<void>(document.SetVoxel(coordinate, palette == 0 ? selectedPalette : 0));
            if (tool == EditTool::Paint) static_cast<void>(document.SetVoxel(coordinate, selectedPalette));
            if (tool == EditTool::Fill) static_cast<void>(document.Fill(coordinate, selectedPalette));
            if (tool == EditTool::Eyedropper) { const auto value = document.PaletteAt(coordinate); if (value.has_value() && *value != 0) selectedPalette = *value; }
            if (tool == EditTool::Select) static_cast<void>(document.Select(coordinate, ImGui::GetIO().KeyCtrl));
        }
        ImGui::PopID(); draw->AddRectFilled(minimum, {minimum.x + cell - 1.0F, minimum.y + cell - 1.0F}, ImGui::ColorConvertFloat4ToU32(color)); draw->AddRect(minimum, {minimum.x + cell - 1.0F, minimum.y + cell - 1.0F}, IM_COL32(75, 72, 59, 255));
        if (document.IsSelected({x, model.gridSize[1] - 1U - y, static_cast<std::uint32_t>(slice)})) draw->AddRect(minimum, {minimum.x + cell - 1.0F, minimum.y + cell - 1.0F}, IM_COL32(244, 191, 58, 255), 0.0F, 0, 2.0F);
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + model.gridSize[1] * cell + 8.0F});
    ImGui::TextUnformatted("Slice editing exposes interiors. Hold Ctrl to add to a selection.");
}

void RenderVoxelPreview(const voxels::VoxelModel& model) {
    const ImVec2 origin = ImGui::GetCursorScreenPos(); const ImVec2 size = ImGui::GetContentRegionAvail(); const float height = std::max(size.y, 190.0F);
    ImDrawList* draw = ImGui::GetWindowDrawList(); draw->AddRectFilled(origin, {origin.x + size.x, origin.y + height}, IM_COL32(23, 29, 28, 255));
    const float scale = std::min(size.x, height) * 0.34F; const ImVec2 center{origin.x + size.x * 0.5F, origin.y + height * 0.62F};
    for (std::uint32_t z = 0; z < model.gridSize[2]; ++z) for (std::uint32_t y = 0; y < model.gridSize[1]; ++y) for (std::uint32_t x = 0; x < model.gridSize[0]; ++x) {
        const std::uint16_t palette = model.voxels[VoxelIndex(model, x, y, z)]; if (palette == 0) continue;
        const auto& entry = model.palette[palette - 1U]; const float px = center.x + (static_cast<float>(x) - static_cast<float>(z)) / model.gridSize[0] * scale; const float py = center.y - (static_cast<float>(y) / model.gridSize[1]) * scale * 0.95F + (static_cast<float>(x + z) / model.gridSize[0]) * scale * 0.22F; const float unit = scale / model.gridSize[0];
        draw->AddRectFilled({px - unit, py - unit}, {px + unit, py + unit}, IM_COL32(entry.red, entry.green, entry.blue, entry.alpha)); draw->AddRect({px - unit, py - unit}, {px + unit, py + unit}, IM_COL32(25, 25, 20, 180));
    }
    ImGui::Dummy({size.x, height});
}

} // namespace

int main(int argc, char** argv) {
    int bundleResult = 0;
    if (BundleFromArguments(argc, argv, bundleResult)) return bundleResult;
    voxels::Engine engine;
    if (!engine.initialize(false)) { std::cerr << "Editor failed to initialize engine services.\n"; return 1; }
    voxels::IPlatform* platform = engine.getPlatform();
    if (platform == nullptr || platform->GetContext().name != "SDL2") { std::cerr << "Editor requires SDL2 desktop runtime.\n"; engine.shutdown(); return 1; }
    platform->SetWindowTitle("Voxels Content Editor"); platform->SetWindowResolution(1440, 900);
    voxels::graphics::GLRenderer renderer;
    if (!renderer.Initialize()) { std::cerr << "Editor failed to initialize OpenGL renderer.\n"; engine.shutdown(); return 1; }
    voxels::ImGuiUIManager ui;
    if (!ui.Initialize(platform, &renderer)) { std::cerr << "Editor failed to initialize ImGui.\n"; renderer.Shutdown(); engine.shutdown(); return 1; }
    ui.SetInputPolicy(voxels::PlayerUIInputPolicy::Overlay);
    const std::filesystem::path layoutPath = voxels::Paths::UserDataDir() / "editor_layout.ini";
    ImGui::LoadIniSettingsFromDisk(layoutPath.string().c_str());
    voxels::editor::EditorProject project(std::filesystem::path(VOXELS_SOURCE_DIR) / "editor" / "samples" / "starter");
    std::string error;
    if (!project.CreateLayout(error)) std::cerr << "Editor project setup failed: " << error << '\n';
    const std::filesystem::path defaultTexture = project.Root() / "textures" / "blocks" / "authored_block.png";
    if (!std::filesystem::exists(defaultTexture) && !project.CreateBlockTexture("blocks/authored_block", voxels::TextureForge::GenerateTexture("planks"), error)) std::cerr << "Editor texture setup failed: " << error << '\n';
    voxels::ImageData previewTexture = voxels::TextureForge::GenerateMissingTexture();
    if (const auto loadedTexture = voxels::TextureLoader::LoadFromFile(defaultTexture); loadedTexture.has_value()) previewTexture = *loadedTexture;
    voxels::editor::GpuModelPreview gpuPreview;
    voxels::editor::EditorDocument document;
    const std::filesystem::path modelPath = project.Root() / "models" / "authored_block.vmdl";
    if (std::filesystem::exists(modelPath) && !document.Load(modelPath, error)) std::cerr << "Editor model load failed: " << error << '\n';
    bool running = true, requestClose = false;
    EditorWindowEvents events(running, requestClose, renderer); platform->RegisterEventListener(&ui, 1000); platform->RegisterEventListener(&events, 100);
    const auto [width, height] = platform->GetDrawableSize(); renderer.SetViewport(width, height);
    int slice = 0; std::uint16_t selectedPalette = 1; EditTool editTool = EditTool::AddRemove; voxels::editor::VoxelCoordinate boxStart{}, boxEnd{}; std::string importPath; BlockDraft block; std::string output = "Editor ready."; auto lastRecovery = std::chrono::steady_clock::now();
    while (running) {
        platform->PollEvents(nullptr);
        const auto now = std::chrono::steady_clock::now();
        if (document.IsDirty() && now - lastRecovery >= std::chrono::seconds(60)) { output = document.WriteRecovery(voxels::Paths::UserDataDir() / "editor_recovery.vmdl", error) ? "Recovery snapshot written." : "Recovery failed: " + error; lastRecovery = now; }
        renderer.BeginFrame({0.08F, 0.10F, 0.09F, 1.0F}); static_cast<void>(gpuPreview.Render(document.Model(), previewTexture, 640, 480)); ui.BeginFrame();
        const ImGuiViewport* viewport = ImGui::GetMainViewport(); ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize); ImGui::SetNextWindowViewport(viewport->ID);
        constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("Voxels Content Editor", nullptr, hostFlags); ImGui::DockSpace(ImGui::GetID("EditorDockspace"), {0.0F, 0.0F}, ImGuiDockNodeFlags_PassthruCentralNode);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) { if (ImGui::MenuItem("Save Model", "Ctrl+S")) output = document.Save(modelPath, error) ? "Model saved." : "Save failed: " + error; if (ImGui::MenuItem("Export OBJ")) output = document.ExportObj(project.Root() / "exports" / "authored_block.obj", error) ? "OBJ exported." : "Export failed: " + error; if (ImGui::MenuItem("Exit")) requestClose = true; ImGui::EndMenu(); }
            if (ImGui::BeginMenu("Edit")) { if (ImGui::MenuItem("Undo", "Ctrl+Z", false, document.CanUndo())) document.Undo(); if (ImGui::MenuItem("Redo", "Ctrl+Y", false, document.CanRedo())) document.Redo(); ImGui::EndMenu(); }
            if (ImGui::BeginMenu("Content")) { if (ImGui::MenuItem("Build Pack")) { if (!document.Save(modelPath, error)) output = "Save failed: " + error; else if (!project.UpsertModelBlock(block.id, block.displayName, block.modelId, block.textureId, block.hardness, error)) output = "Block definition rejected: " + error; else { voxels::AssetBundleReport report; output = project.BuildPack(project.Root() / "packs" / "content.vpk", report, error) ? "Pack built: " + std::to_string(report.archive.entryCount) + " entries." : "Pack rejected: " + error; } } ImGui::EndMenu(); }
            if (ImGui::BeginMenu("Help")) { ImGui::TextUnformatted("Voxel Content Editor - VMDL v1 / VPK1"); ImGui::EndMenu(); } ImGui::EndMenuBar();
        }
        if (ImGui::Begin("Content Browser")) { ImGui::TextWrapped("%s", project.Root().string().c_str()); ImGui::Separator(); for (const auto& asset : project.ListAssets()) ImGui::BulletText("%s", asset.generic_string().c_str()); } ImGui::End();
        if (ImGui::Begin("Model Viewport")) { if (gpuPreview.Texture() != 0) ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(gpuPreview.Texture())), ImGui::GetContentRegionAvail(), {0, 1}, {1, 0}); else RenderVoxelPreview(document.Model()); } ImGui::End();
        if (ImGui::Begin("Slice Editor")) { const char* tools[] = {"Add/Remove", "Paint", "Fill", "Eyedropper", "Select"}; int toolIndex = static_cast<int>(editTool); if (ImGui::Combo("Tool", &toolIndex, tools, 5)) editTool = static_cast<EditTool>(toolIndex); RenderSliceEditor(document, slice, selectedPalette, editTool); ImGui::Separator(); ImGui::Text("Selected: %zu", document.Selection().size()); if (ImGui::Button("Clear Selection")) document.ClearSelection(); ImGui::SameLine(); if (ImGui::Button("Box Select")) static_cast<void>(document.SelectBox(boxStart, boxEnd, false)); ImGui::InputScalarN("Box start", ImGuiDataType_U32, &boxStart.x, 3); ImGui::InputScalarN("Box end", ImGuiDataType_U32, &boxEnd.x, 3); if (ImGui::Button("Move X-")) static_cast<void>(document.MoveSelected(-1, 0, 0)); ImGui::SameLine(); if (ImGui::Button("Move X+")) static_cast<void>(document.MoveSelected(1, 0, 0)); ImGui::SameLine(); if (ImGui::Button("Move Y-")) static_cast<void>(document.MoveSelected(0, -1, 0)); ImGui::SameLine(); if (ImGui::Button("Move Y+")) static_cast<void>(document.MoveSelected(0, 1, 0)); ImGui::SameLine(); if (ImGui::Button("Move Z-")) static_cast<void>(document.MoveSelected(0, 0, -1)); ImGui::SameLine(); if (ImGui::Button("Move Z+")) static_cast<void>(document.MoveSelected(0, 0, 1)); } ImGui::End();
        if (ImGui::Begin("Palette and Properties")) {
            for (std::size_t index = 0; index < document.Model().palette.size(); ++index) { const auto entry = document.Model().palette[index]; float color[4] = {entry.red / 255.0F, entry.green / 255.0F, entry.blue / 255.0F, entry.alpha / 255.0F}; ImGui::PushID(static_cast<int>(index)); if (ImGui::ColorEdit4("##color", color)) { auto updated = entry; updated.red = static_cast<std::uint8_t>(color[0] * 255.0F); updated.green = static_cast<std::uint8_t>(color[1] * 255.0F); updated.blue = static_cast<std::uint8_t>(color[2] * 255.0F); updated.alpha = static_cast<std::uint8_t>(color[3] * 255.0F); static_cast<void>(document.UpdatePaletteEntry(index, updated)); } ImGui::SameLine(); if (ImGui::Selectable(("Palette " + std::to_string(index + 1)).c_str(), selectedPalette == index + 1)) selectedPalette = static_cast<std::uint16_t>(index + 1); ImGui::SameLine(); if (ImGui::SmallButton("Remove")) static_cast<void>(document.RemovePaletteEntry(index)); ImGui::PopID(); }
            if (ImGui::Button("Add Palette")) document.AddPaletteEntry({180, 180, 180, 255, 0, 0, 0}); ImGui::Separator(); auto pivot = document.Model().pivot; auto boundsMin = document.Model().boundsMin; auto boundsMax = document.Model().boundsMax; if (ImGui::InputFloat3("Pivot", pivot.data())) static_cast<void>(document.SetPivot(pivot)); if (ImGui::InputFloat3("Bounds min", boundsMin.data())) static_cast<void>(document.SetBounds(boundsMin, boundsMax)); if (ImGui::InputFloat3("Bounds max", boundsMax.data())) static_cast<void>(document.SetBounds(boundsMin, boundsMax));
            if (ImGui::Button("Mirror X")) static_cast<void>(document.Mirror(voxels::editor::Axis::X)); ImGui::SameLine(); if (ImGui::Button("Mirror Y")) static_cast<void>(document.Mirror(voxels::editor::Axis::Y)); ImGui::SameLine(); if (ImGui::Button("Mirror Z")) static_cast<void>(document.Mirror(voxels::editor::Axis::Z)); if (ImGui::Button("Rotate X")) static_cast<void>(document.Rotate90(voxels::editor::Axis::X)); ImGui::SameLine(); if (ImGui::Button("Rotate Y")) static_cast<void>(document.Rotate90(voxels::editor::Axis::Y)); ImGui::SameLine(); if (ImGui::Button("Rotate Z")) static_cast<void>(document.Rotate90(voxels::editor::Axis::Z));
        } ImGui::End();
        if (ImGui::Begin("Block Definition")) { static_cast<void>(voxels::ui::TextField("Id", block.id)); static_cast<void>(voxels::ui::TextField("Display name", block.displayName)); static_cast<void>(voxels::ui::TextField("Model id", block.modelId)); static_cast<void>(voxels::ui::TextField("Texture id", block.textureId)); ImGui::InputFloat("Hardness", &block.hardness); static_cast<void>(voxels::ui::TextField("PNG to import", importPath)); if (ImGui::Button("Import PNG Texture")) { if (project.ImportBlockTexture(importPath, block.textureId, error)) { const auto texture = voxels::TextureLoader::LoadFromFile(project.Root() / "textures" / (block.textureId + ".png")); if (texture.has_value()) previewTexture = *texture; output = "Texture imported and applied to preview."; } else output = "Texture import failed: " + error; } if (ImGui::Button("Write Block Definition")) output = project.UpsertModelBlock(block.id, block.displayName, block.modelId, block.textureId, block.hardness, error) ? "Block definition written." : "Block definition failed: " + error; } ImGui::End();
        if (ImGui::Begin("Build Output")) ImGui::TextWrapped("%s", output.c_str()); ImGui::End(); ImGui::End();
        if (requestClose && document.IsDirty()) { ImGui::OpenPopup("Unsaved model"); if (ImGui::BeginPopupModal("Unsaved model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::TextUnformatted("Save the authored model before closing?"); if (ImGui::Button("Save and Exit")) { if (document.Save(modelPath, error)) events.CloseNow(); else { output = "Save failed: " + error; requestClose = false; } ImGui::CloseCurrentPopup(); } ImGui::SameLine(); if (ImGui::Button("Discard")) { events.CloseNow(); ImGui::CloseCurrentPopup(); } ImGui::SameLine(); if (ImGui::Button("Cancel")) { requestClose = false; ImGui::CloseCurrentPopup(); } ImGui::EndPopup(); } } else if (requestClose) events.CloseNow();
        ui.EndFrame(); renderer.EndFrame(); platform->SwapBuffers();
    }
    ImGui::SaveIniSettingsToDisk(layoutPath.string().c_str()); gpuPreview.Shutdown(); platform->UnregisterEventListener(&ui); platform->UnregisterEventListener(&events); ui.Shutdown(); renderer.Shutdown(); engine.shutdown(); return 0;
}