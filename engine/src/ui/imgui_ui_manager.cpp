/**
 * @file imgui_ui_manager.cpp
 * @brief Dear ImGui backend, themed widgets, and gameplay input arbitration.
 */

#include "voxels/ui/imgui_ui_manager.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <utility>

#include <SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#endif

#include "voxels/core/paths.hpp"

namespace voxels {
namespace {
constexpr float kBaseFontPixels = 18.0f;
constexpr std::size_t kFrameHistoryCapacity = 120;
constexpr ImVec4 kWarmAccent{0.82f, 0.52f, 0.19f, 1.0f};

ImGuiKey ToImGuiKey(std::uint32_t key) {
    if (key >= static_cast<std::uint32_t>('a') && key <= static_cast<std::uint32_t>('z')) return static_cast<ImGuiKey>(ImGuiKey_A + key - static_cast<std::uint32_t>('a'));
    if (key >= static_cast<std::uint32_t>('A') && key <= static_cast<std::uint32_t>('Z')) return static_cast<ImGuiKey>(ImGuiKey_A + key - static_cast<std::uint32_t>('A'));
    if (key >= static_cast<std::uint32_t>('0') && key <= static_cast<std::uint32_t>('9')) return static_cast<ImGuiKey>(ImGuiKey_0 + key - static_cast<std::uint32_t>('0'));
    if (key == 27U) return ImGuiKey_Escape;
    if (key == 13U) return ImGuiKey_Enter;
    if (key == 32U) return ImGuiKey_Space;
    return ImGuiKey_None;
}
} // namespace

void ApplyVoxelsTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowPadding = {18.0f, 16.0f};
    style.FramePadding = {12.0f, 8.0f};
    style.ItemSpacing = {10.0f, 9.0f};
    style.Colors[ImGuiCol_WindowBg] = {0.15f, 0.16f, 0.15f, 0.96f};
    style.Colors[ImGuiCol_Border] = {0.36f, 0.33f, 0.27f, 1.0f};
    style.Colors[ImGuiCol_FrameBg] = {0.23f, 0.24f, 0.22f, 1.0f};
    style.Colors[ImGuiCol_FrameBgHovered] = {0.32f, 0.30f, 0.24f, 1.0f};
    style.Colors[ImGuiCol_Button] = {0.32f, 0.29f, 0.22f, 1.0f};
    style.Colors[ImGuiCol_ButtonHovered] = kWarmAccent;
    style.Colors[ImGuiCol_ButtonActive] = {0.64f, 0.37f, 0.12f, 1.0f};
    style.Colors[ImGuiCol_CheckMark] = kWarmAccent;
    style.Colors[ImGuiCol_SliderGrab] = kWarmAccent;
    style.Colors[ImGuiCol_Text] = {0.93f, 0.91f, 0.84f, 1.0f};
}

bool ImGuiUIManager::Initialize(IPlatform* platform, graphics::IGraphicsRenderer* renderer) {
    if (platform == nullptr || platform->GetContext().name != "SDL2") return false;
    if (renderer == nullptr) return false;
    m_platform = platform;
    m_rendererBackend = renderer->GetBackend();
    const auto [width, height] = platform->GetDrawableSize();
    m_metrics.windowWidth = width;
    m_metrics.windowHeight = height;
    m_scale = ComputeUIScale(m_metrics);
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    auto* window = static_cast<SDL_Window*>(platform->GetNativeWindowHandle());
    bool backendInitialized = false;
    if (window != nullptr && m_rendererBackend == RendererBackend::OpenGL) {
        backendInitialized = ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext()) &&
                             ImGui_ImplOpenGL3_Init("#version 330");
    }
#if defined(_WIN32)
    if (window != nullptr && m_rendererBackend == RendererBackend::Direct3D11) {
        backendInitialized = ImGui_ImplSDL2_InitForD3D(window) &&
            ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(renderer->GetNativeDevice()),
                                static_cast<ID3D11DeviceContext*>(renderer->GetNativeContext()));
    }
#endif
    if (!backendInitialized) {
        Shutdown();
        return false;
    }
    ApplyVoxelsTheme();
    RebuildFonts();
    m_initialized = true;
    return true;
}

void ImGuiUIManager::Shutdown() {
    if (ImGui::GetCurrentContext() != nullptr) {
    if (m_rendererBackend == RendererBackend::OpenGL) ImGui_ImplOpenGL3_Shutdown();
#if defined(_WIN32)
    if (m_rendererBackend == RendererBackend::Direct3D11) ImGui_ImplDX11_Shutdown();
#endif
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    m_platform = nullptr;
    m_initialized = false;
    m_frameActive = false;
}

void ImGuiUIManager::BeginFrame() {
    if (!m_initialized || m_frameActive) return;
    if (m_rendererBackend == RendererBackend::OpenGL) ImGui_ImplOpenGL3_NewFrame();
#if defined(_WIN32)
    if (m_rendererBackend == RendererBackend::Direct3D11) ImGui_ImplDX11_NewFrame();
#endif
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    m_frameActive = true;
}

void ImGuiUIManager::EndFrame() {
    if (!m_frameActive) return;
    RenderDebugOverlay();
    RenderErrorModal();
    RenderToasts();
    ImGui::Render();
    if (m_rendererBackend == RendererBackend::OpenGL) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#if defined(_WIN32)
    if (m_rendererBackend == RendererBackend::Direct3D11) ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
    m_frameActive = false;
}

void ImGuiUIManager::SetInputPolicy(PlayerUIInputPolicy policy) {
    if (m_policy == policy) return;
    m_policy = policy;
    const bool gameplay = policy == PlayerUIInputPolicy::Gameplay;
    m_discardNextMouseDelta = gameplay;
    if (m_platform != nullptr) {
        m_platform->SetRelativeMouseMode(gameplay);
        m_platform->SetCursorVisible(!gameplay);
    }
}

bool ImGuiUIManager::CapturesMouse() const noexcept { return m_policy != PlayerUIInputPolicy::Gameplay || (m_initialized && ImGui::GetIO().WantCaptureMouse); }
bool ImGuiUIManager::CapturesKeyboard() const noexcept { return m_policy != PlayerUIInputPolicy::Gameplay; }
bool ImGuiUIManager::ConsumeTransitionMouseDelta() noexcept { const bool discard = m_discardNextMouseDelta; m_discardNextMouseDelta = false; return discard; }
void ImGuiUIManager::Publish(PlayerUIViewModel model) { m_lastModel = std::move(model); }
std::optional<PlayerUIAction> ImGuiUIManager::ConsumeAction() {
    if (m_actions.empty()) return std::nullopt;
    PlayerUIAction action = std::move(m_actions.front());
    m_actions.erase(m_actions.begin());
    return action;
}
void ImGuiUIManager::SetDebugMetrics(UIDebugMetrics metrics) {
    m_debugMetrics = std::move(metrics);
    m_frameHistory.push_back(m_debugMetrics.frameMilliseconds);
    if (m_frameHistory.size() > kFrameHistoryCapacity) m_frameHistory.erase(m_frameHistory.begin());
}
void ImGuiUIManager::ShowError(std::string title, std::string detail) { m_errorTitle = std::move(title); m_errorDetail = std::move(detail); }
void ImGuiUIManager::ShowToast(std::string message, float durationSeconds) { m_toasts.push_back({std::move(message), std::max(durationSeconds, 0.1f)}); }

void ImGuiUIManager::OnPlatformEvent(const PlatformEvent& event) {
    if (!m_initialized) return;
    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case PlatformEventType::WindowResized: m_metrics.windowWidth = event.width; m_metrics.windowHeight = event.height; m_scale = ComputeUIScale(m_metrics); RebuildFonts(); break;
        case PlatformEventType::MouseMotion: io.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y)); break;
        case PlatformEventType::MouseButtonDown: io.AddMouseButtonEvent(event.button - 1, true); break;
        case PlatformEventType::MouseButtonUp: io.AddMouseButtonEvent(event.button - 1, false); break;
        case PlatformEventType::MouseWheel: io.AddMouseWheelEvent(static_cast<float>(event.wheelX), static_cast<float>(event.wheelY)); break;
        case PlatformEventType::KeyDown: case PlatformEventType::KeyUp: { const ImGuiKey key = ToImGuiKey(event.keyCode); if (key != ImGuiKey_None) io.AddKeyEvent(key, event.type == PlatformEventType::KeyDown); break; }
        case PlatformEventType::TextInput: io.AddInputCharactersUTF8(event.text.c_str()); break;
        default: break;
    }
}

void ImGuiUIManager::RebuildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const std::array<std::filesystem::path, 2> paths{Paths::AssetsDir() / "ui/fonts/ui_font.ttf", Paths::AssetsDir() / "fonts/AtkinsonHyperlegible-Regular.ttf"};
    bool loaded = false;
    for (const auto& path : paths) if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path.string().c_str(), kBaseFontPixels * m_scale) != nullptr) { loaded = true; break; }
    if (!loaded) { io.Fonts->AddFontDefault(); if (!m_fontWarningReported) { std::cerr << "UI font missing; using embedded Dear ImGui fallback font.\n"; m_fontWarningReported = true; } }
}

void ImGuiUIManager::RenderDebugOverlay() {
    if (!m_debugOverlayVisible) return;
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_Always);
    if (ImGui::Begin("Debug Overlay", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("%.1f FPS | %.2f ms", m_debugMetrics.framesPerSecond, m_debugMetrics.frameMilliseconds);
        if (!m_frameHistory.empty()) ImGui::PlotLines("Frame time (ms)", m_frameHistory.data(), static_cast<int>(m_frameHistory.size()), 0, nullptr, 0.0f, 50.0f, {260.0f, 56.0f});
        ImGui::Text("Position %.1f, %.1f, %.1f | Chunk %d, %d, %d", m_debugMetrics.playerX, m_debugMetrics.playerY, m_debugMetrics.playerZ, m_debugMetrics.chunkX, m_debugMetrics.chunkY, m_debugMetrics.chunkZ);
        ImGui::Text("Chunks L/M/V: %zu / %zu / %zu", m_debugMetrics.loadedChunks, m_debugMetrics.meshedChunks, m_debugMetrics.visibleChunks);
        ImGui::Text("Draw calls: %zu | Triangles: %zu | Mesh queue: %zu", m_debugMetrics.drawCalls, m_debugMetrics.triangles, m_debugMetrics.meshQueueDepth);
        ImGui::Text("GL: %s", m_debugMetrics.glRenderer.c_str());
        ImGui::Text("Driver: %s | %s", m_debugMetrics.glVendor.c_str(), m_debugMetrics.glVersion.c_str());
    }
    ImGui::End();
}

void ImGuiUIManager::RenderErrorModal() { if (m_errorTitle.empty()) return; ImGui::OpenPopup(m_errorTitle.c_str()); if (ImGui::BeginPopupModal(m_errorTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::TextWrapped("%s", m_errorDetail.c_str()); if (ImGui::Button("Acknowledge")) { m_errorTitle.clear(); m_errorDetail.clear(); ImGui::CloseCurrentPopup(); } ImGui::EndPopup(); } }
void ImGuiUIManager::RenderToasts() { for (auto& toast : m_toasts) toast.remainingSeconds -= ImGui::GetIO().DeltaTime; std::erase_if(m_toasts, [](const ToastMessage& toast) { return toast.remainingSeconds <= 0.0f; }); }

namespace ui {
bool MenuButton(const char* label, bool enabled) { ImGui::BeginDisabled(!enabled); const bool pressed = ImGui::Button(label, {-1.0f, 0.0f}); ImGui::EndDisabled(); return pressed; }
void MenuTitle(const char* title) { ImGui::PushStyleColor(ImGuiCol_Text, kWarmAccent); ImGui::TextUnformatted(title); ImGui::PopStyleColor(); ImGui::Separator(); }
bool SettingSlider(const char* label, float* value, float minimum, float maximum, const char* format) { return ImGui::SliderFloat(label, value, minimum, maximum, format); }
bool SettingPercentSlider(const char* label, float* normalizedValue) {
    float percent = std::clamp(*normalizedValue, 0.0f, 1.0f) * 100.0f;
    if (!ImGui::SliderFloat(label, &percent, 0.0f, 100.0f, "%.0f%%")) return false;
    *normalizedValue = percent / 100.0f;
    return true;
}
bool SettingToggle(const char* label, bool* value) { return ImGui::Checkbox(label, value); }
bool SettingDropdown(const char* label, int* currentItem, const char* const items[], int itemCount) { return ImGui::Combo(label, currentItem, items, itemCount); }
bool KeyBindRow(const char* label, int* key) { return ImGui::InputInt(label, key); }
bool TextField(const char* label, std::string& value, std::size_t capacity) { std::vector<char> buffer(std::max(capacity, value.size() + 1), '\0'); std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str()); if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false; value = buffer.data(); return true; }
bool ConfirmDialog(const char* title, const char* detail, bool* open) { if (!*open) return false; ImGui::OpenPopup(title); bool confirmed = false; if (ImGui::BeginPopupModal(title, open, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::TextWrapped("%s", detail); confirmed = ImGui::Button("Confirm"); ImGui::SameLine(); if (ImGui::Button("Cancel")) *open = false; if (confirmed) { *open = false; ImGui::CloseCurrentPopup(); } ImGui::EndPopup(); } return confirmed; }
void ProgressBar(float fraction, const char* overlay) { ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), {-1.0f, 0.0f}, overlay); }
bool SaveListEntry(const char* label, bool selected) { return ImGui::Selectable(label, selected, 0, {-1.0f, 0.0f}); }
void Toast(const char* message) { ImGui::TextUnformatted(message); }
} // namespace ui
} // namespace voxels