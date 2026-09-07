/**
 * @file web_ui_manager.cpp
 * @brief CEF off-screen player UI backend.
 */

#include "voxels/ui/web_ui_manager.hpp"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include <nlohmann/json.hpp>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_request_handler.h"

#include "voxels/core/paths.hpp"
#include "voxels/ui/web_ui_manifest.hpp"

namespace voxels {
namespace {

/// Relaxes Chromium's opaque per-file-URL origin model so the bundled `file://` UI can load its
/// own same-directory Vite ES-module/CSS chunks; without it every subresource load is treated as
/// cross-origin and silently dropped (the overlay would build a browser but paint nothing).
class FileAccessApp final : public CefApp {
public:
    void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> commandLine) override {
        commandLine->AppendSwitch("allow-file-access-from-files");
    }

private:
    IMPLEMENT_REFCOUNTING(FileAccessApp);
};

} // namespace

class WebUIManager::BrowserClient final : public CefClient,
                                           public CefRenderHandler,
                                           public CefLifeSpanHandler,
                                           public CefRequestHandler {
public:
    explicit BrowserClient(WebUIManager& owner) : m_owner(owner) {}

    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, m_width, m_height); }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { m_browser = browser; }
    void OnBeforeClose(CefRefPtr<CefBrowser>) override { m_browser = nullptr; }
    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList& dirtyRects, const void* buffer, int width, int height) override {
        if (type != PET_VIEW || buffer == nullptr) return;
        std::vector<WebUiDirtyRect> dirty;
        dirty.reserve(dirtyRects.size());
        for (const CefRect& rect : dirtyRects) dirty.push_back({rect.x, rect.y, rect.width, rect.height});
        const auto bytes = std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(buffer), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        static_cast<void>(m_owner.m_frames.Submit(width, height, bytes, dirty));
    }
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, bool, bool) override {
        const std::string url = request->GetURL();
        constexpr std::string_view prefix = "voxels://action/";
        if (url.starts_with(prefix)) {
            const std::string kind = url.substr(prefix.size(), url.find('?') - prefix.size());
            const auto parsed = ParseAction(kind, url);
            if (parsed.has_value()) m_owner.SubmitAction(*parsed);
            return true;
        }
        return !url.starts_with("file:///");
    }
    void Resize(int width, int height) {
        m_width = std::max(width, 1);
        m_height = std::max(height, 1);
        if (m_browser) m_browser->GetHost()->WasResized();
    }
    /// Requests an asynchronous close; the browser stays alive (`OnBeforeClose` not yet fired)
    /// until the pumped message loop finishes tearing it down.
    void CloseBrowser() { if (m_browser) m_browser->GetHost()->CloseBrowser(true); }
    [[nodiscard]] bool HasBrowser() const noexcept { return static_cast<bool>(m_browser); }
    void Publish(const PlayerUIViewModel& model) {
        if (!m_browser) return;
        const nlohmann::json json{{"route", static_cast<int>(model.route)}, {"revision", model.revision}, {"title", model.title}, {"message", model.message}, {"items", model.items}, {"progress", model.progress}, {"blocking", model.blocking}};
        const std::string script = "window.__voxelsReceiveModel&&window.__voxelsReceiveModel(" + json.dump() + ");";
        m_browser->GetMainFrame()->ExecuteJavaScript(script, m_browser->GetMainFrame()->GetURL(), 0);
    }

private:
    static std::optional<PlayerUIAction> ParseAction(const std::string& kind, const std::string& url) {
        PlayerUIAction action{.requestId = 1};
        if (kind == "play") action.kind = PlayerUIActionKind::Play;
        else if (kind == "quit") action.kind = PlayerUIActionKind::Quit;
        else if (kind == "settings") action.kind = PlayerUIActionKind::OpenSettings;
        else if (kind == "back") action.kind = PlayerUIActionKind::Back;
        else return std::nullopt;
        const auto marker = url.find("requestId=");
        if (marker != std::string::npos) {
            try { action.requestId = static_cast<std::uint32_t>(std::stoul(url.substr(marker + 10))); } catch (...) { return std::nullopt; }
        }
        return action.requestId == 0 ? std::nullopt : std::optional<PlayerUIAction>{action};
    }
    WebUIManager& m_owner;
    CefRefPtr<CefBrowser> m_browser;
    int m_width = 1280;
    int m_height = 720;
    IMPLEMENT_REFCOUNTING(BrowserClient);
};

WebUIManager::WebUIManager() = default;
WebUIManager::~WebUIManager() { Shutdown(); }
int WebUIManager::ExecuteSubprocess(int, char**) {
    CefMainArgs args(GetModuleHandle(nullptr));
    return CefExecuteProcess(args, new FileAccessApp(), nullptr);
}
bool WebUIManager::Initialize(IPlatform* platform, graphics::IGraphicsRenderer*) {
    if (platform == nullptr || platform->GetContext().name != "SDL2") return false;
    WebUiManifest manifest; std::string error;
    if (!LoadAndVerifyWebUiManifest(Paths::AssetsDir() / "ui/ui-manifest.json", manifest, error)) return false;
    CefSettings settings; settings.windowless_rendering_enabled = true; settings.external_message_pump = true;
    // No sandbox loader is staged (ADR-015: cef_sandbox.lib is /MT-only, incompatible with the
    // engine's /MD runtime); tell CEF explicitly rather than let it warn/misbehave at startup.
    settings.no_sandbox = 1;
    // Pin every retail path explicitly so layout is deterministic regardless of CEF's own
    // executable-relative guesses, and keep CEF's user-writable state out of the read-only
    // install directory per ADR-011.
    const std::filesystem::path executableDir = Paths::ExecutableDir();
    CefString(&settings.resources_dir_path).FromString(executableDir.string());
    CefString(&settings.locales_dir_path).FromString((executableDir / "locales").string());
    const std::filesystem::path cacheDir = Paths::UserDataDir() / "cef_cache";
    std::error_code cacheError;
    std::filesystem::create_directories(cacheDir, cacheError);
    CefString(&settings.root_cache_path).FromString(cacheDir.string());
    CefString(&settings.log_file).FromString((Paths::LogsDir() / "cef.log").string());
    CefMainArgs args(GetModuleHandle(nullptr));
    if (!CefInitialize(args, settings, new FileAccessApp(), nullptr)) return false;
    m_platform = platform; const auto [width, height] = platform->GetDrawableSize();
    m_client = new BrowserClient(*this); m_client->Resize(width, height);
    CefWindowInfo info; info.SetAsWindowless(nullptr);
    const std::string url = "file:///" + std::filesystem::absolute(Paths::AssetsDir() / "ui" / manifest.entryHtml).generic_string();
    if (!CefBrowserHost::CreateBrowser(info, m_client.get(), url, CefBrowserSettings{}, nullptr, nullptr)) { Shutdown(); return false; }
    m_initialized = true; return true;
}
void WebUIManager::Shutdown() {
    if (!m_initialized) return;
    // CEF's windowless/external-pump shutdown contract: request an async browser close, then
    // keep pumping the message loop until CEF signals completion (OnBeforeClose) before
    // releasing our client or calling CefShutdown(); doing it immediately segfaults on exit
    // because the browser/renderer process is still tearing down. CEF's own scheduled-work
    // callbacks for the browser process arrive as native OS messages on this thread (SDL's
    // pump normally forwards them during the frame loop), so the platform must keep polling
    // here too or CloseBrowser() never actually completes.
    if (m_client) {
        m_client->CloseBrowser();
        for (int pumpIteration = 0; pumpIteration < 240 && m_client->HasBrowser(); ++pumpIteration) {
            if (m_platform != nullptr) m_platform->PollEvents(nullptr);
            CefDoMessageLoopWork();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }
    m_client.reset();
    CefShutdown();
    m_compositor.Shutdown();
    m_initialized = false;
    m_platform = nullptr;
}
void WebUIManager::BeginFrame() { if (!m_initialized) return; CefDoMessageLoopWork(); m_frameActive = true; }
void WebUIManager::EndFrame() { if (m_frameActive) static_cast<void>(m_compositor.UploadAndComposite(m_frames)); m_frameActive = false; }
void WebUIManager::OnPlatformEvent(const PlatformEvent& event) { if (event.type == PlatformEventType::WindowResized && m_client) m_client->Resize(event.width, event.height); }
void WebUIManager::SetInputPolicy(PlayerUIInputPolicy policy) { m_discardNextMouseDelta = policy == PlayerUIInputPolicy::Gameplay && m_policy != policy; m_policy = policy; if (m_platform) { const bool gameplay = policy == PlayerUIInputPolicy::Gameplay; m_platform->SetRelativeMouseMode(gameplay); m_platform->SetCursorVisible(!gameplay); } }
bool WebUIManager::ConsumeTransitionMouseDelta() noexcept { const bool discard = m_discardNextMouseDelta; m_discardNextMouseDelta = false; return discard; }
void WebUIManager::Publish(PlayerUIViewModel model) { if (m_client) m_client->Publish(model); }
std::optional<PlayerUIAction> WebUIManager::ConsumeAction() { if (m_actions.empty()) return std::nullopt; PlayerUIAction action = std::move(m_actions.front()); m_actions.erase(m_actions.begin()); return action; }
void WebUIManager::ShowToast(std::string, float) {}
void WebUIManager::SubmitAction(PlayerUIAction action) { if (m_actions.empty() || m_actions.back().requestId != action.requestId) m_actions.push_back(std::move(action)); }

} // namespace voxels