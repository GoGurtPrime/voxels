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
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_process_message.h"
#include "include/cef_parser.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_scheme.h"
#include "include/cef_v8.h"

#include "voxels/core/paths.hpp"
#include "voxels/core/save.hpp"
#include "voxels/ui/web_ui_manifest.hpp"

namespace voxels {
namespace {

int CefModifiers(const PlatformEvent& event) noexcept {
    int modifiers = EVENTFLAG_NONE;
    if ((event.modifiers & PlatformModifierShift) != 0U) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if ((event.modifiers & PlatformModifierControl) != 0U) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if ((event.modifiers & PlatformModifierAlt) != 0U) modifiers |= EVENTFLAG_ALT_DOWN;
    if ((event.modifiers & PlatformModifierSuper) != 0U) modifiers |= EVENTFLAG_COMMAND_DOWN;
    if ((event.modifiers & PlatformModifierCapsLock) != 0U) modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    if ((event.modifiers & PlatformModifierNumLock) != 0U) modifiers |= EVENTFLAG_NUM_LOCK_ON;
    if ((event.modifiers & PlatformModifierLeftMouse) != 0U) modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    if ((event.modifiers & PlatformModifierMiddleMouse) != 0U) modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    if ((event.modifiers & PlatformModifierRightMouse) != 0U) modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    if (event.repeat) modifiers |= EVENTFLAG_IS_REPEAT;
    return modifiers;
}

int CefWindowsKeyCode(const PlatformEvent& event) noexcept {
    if (event.keyCode >= static_cast<std::uint32_t>('a') && event.keyCode <= static_cast<std::uint32_t>('z')) {
        return static_cast<int>(event.keyCode - static_cast<std::uint32_t>('a') + static_cast<std::uint32_t>('A'));
    }
    if (event.keyCode <= 0x7FU) return static_cast<int>(event.keyCode);
    switch (event.scancode) {
        case 40U: return 0x0D;
        case 41U: return 0x1B;
        case 42U: return 0x08;
        case 43U: return 0x09;
        case 73U: return 0x2D;
        case 74U: return 0x24;
        case 75U: return 0x21;
        case 76U: return 0x2E;
        case 77U: return 0x23;
        case 78U: return 0x22;
        case 79U: return 0x27;
        case 80U: return 0x25;
        case 81U: return 0x28;
        case 82U: return 0x26;
        default: return static_cast<int>(event.keyCode);
    }
}

int CefControllerKeyCode(int button) noexcept {
    switch (button) {
        case 0: return 0x0D;
        case 1: return 0x1B;
        case 11: return 0x26;
        case 12: return 0x28;
        case 13: return 0x25;
        case 14: return 0x27;
        default: return 0;
    }
}

constexpr char kUiScheme[] = "voxels-ui";
constexpr char kUiHost[] = "app";
constexpr char kUiOriginPrefix[] = "voxels-ui://app/";

constexpr char kBridgeActionPrefix[] = "ui.action.";
constexpr char kBridgeModelKind[] = "ui.model";

struct VerifiedUiAssets {
    std::unordered_map<std::string, std::filesystem::path> files;
};

[[nodiscard]] bool IsUiUrl(const std::string& url) {
    CefURLParts parts;
    if (!CefParseURL(url, parts)) return false;
    return CefString(&parts.scheme).ToString() == kUiScheme &&
           CefString(&parts.host).ToString() == kUiHost;
}

[[nodiscard]] std::optional<PlayerUIActionKind> ActionKindFromWire(std::string_view wireKind) {
    if (wireKind.starts_with(kBridgeActionPrefix)) wireKind.remove_prefix(std::char_traits<char>::length(kBridgeActionPrefix));
    if (wireKind == "play") return PlayerUIActionKind::Play;
    if (wireKind == "create-world" || wireKind == "create_world") return PlayerUIActionKind::CreateWorld;
    if (wireKind == "load-world" || wireKind == "load_world") return PlayerUIActionKind::LoadWorld;
    if (wireKind == "delete-world" || wireKind == "delete_world") return PlayerUIActionKind::DeleteWorld;
    if (wireKind == "confirm-delete" || wireKind == "confirm_delete") return PlayerUIActionKind::ConfirmDelete;
    if (wireKind == "join") return PlayerUIActionKind::Join;
    if (wireKind == "resume") return PlayerUIActionKind::Resume;
    if (wireKind == "controls" || wireKind == "open-controls") return PlayerUIActionKind::OpenControls;
    if (wireKind == "toggle-world-visibility") return PlayerUIActionKind::ToggleWorldVisibility;
    if (wireKind == "return-to-main-menu") return PlayerUIActionKind::ReturnToMainMenu;
    if (wireKind == "exit-to-desktop") return PlayerUIActionKind::ExitToDesktop;
    if (wireKind == "quit") return PlayerUIActionKind::Quit;
    if (wireKind == "settings" || wireKind == "open-settings" || wireKind == "open_settings") return PlayerUIActionKind::OpenSettings;
    if (wireKind == "back") return PlayerUIActionKind::Back;
    if (wireKind == "apply-settings" || wireKind == "apply_settings") return PlayerUIActionKind::ApplySettings;
    if (wireKind == "dismiss-controls" || wireKind == "dismiss_controls") return PlayerUIActionKind::DismissControls;
    if (wireKind == "hotbar") return PlayerUIActionKind::HudHotbar;
    if (wireKind == "open-chat") return PlayerUIActionKind::HudOpenChat;
    if (wireKind == "close-chat") return PlayerUIActionKind::HudCloseChat;
    if (wireKind == "send-chat") return PlayerUIActionKind::HudSendChat;
    if (wireKind == "open-crafting") return PlayerUIActionKind::HudOpenCrafting;
    if (wireKind == "close-crafting") return PlayerUIActionKind::HudCloseCrafting;
    if (wireKind == "craft-recipe") return PlayerUIActionKind::HudCraftRecipe;
    if (wireKind == "drop-item") return PlayerUIActionKind::HudDropItem;
    if (wireKind == "acknowledge-error" || wireKind == "acknowledge_error") return PlayerUIActionKind::AcknowledgeError;
    return std::nullopt;
}

[[nodiscard]] std::optional<PlayerUIAction> DecodeBridgeActionEnvelope(const PlayerUIProtocolMessage& envelope) {
    const auto kind = ActionKindFromWire(envelope.kind);
    if (!kind.has_value()) return std::nullopt;
    PlayerUIAction action{};
    action.requestId = envelope.requestId;
    action.kind = *kind;
    if (envelope.payload.empty()) return action;
    try {
        const nlohmann::json payload = nlohmann::json::parse(envelope.payload);
        if (payload.is_object()) {
            if (const auto primary = payload.find("primary"); primary != payload.end() && primary->is_string()) {
                action.primary = primary->get<std::string>();
            }
            if (const auto secondary = payload.find("secondary"); secondary != payload.end()) {
                if (secondary->is_string()) action.secondary = secondary->get<std::string>();
                else action.secondary = secondary->dump();
            }
            if (const auto value = payload.find("value"); value != payload.end() && value->is_number()) {
                action.value = value->get<float>();
            }
            if (action.secondary.empty()) {
                if (const auto settings = payload.find("settings"); settings != payload.end()) {
                    action.secondary = settings->dump();
                }
            }
            return action;
        }
        if (payload.is_string()) {
            action.secondary = payload.get<std::string>();
            return action;
        }
        action.secondary = payload.dump();
        return action;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<PlayerUIAction> DecodeLegacyActionJson(std::string_view encoded) {
    try {
        const nlohmann::json command = nlohmann::json::parse(encoded);
        const std::string wireKind = command.value("kind", "");
        const std::uint32_t requestId = command.value("requestId", 0U);
        const auto kind = ActionKindFromWire(wireKind);
        if (!kind.has_value() || requestId == 0) return std::nullopt;
        PlayerUIAction action{.requestId = requestId, .kind = *kind,
                              .primary = command.value("primary", ""),
                              .secondary = command.value("secondary", "")};
        if (const auto value = command.find("value"); value != command.end() && value->is_number()) {
            action.value = value->get<float>();
        }
        return action;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::string AssetPathFromUrl(const std::string& url) {
    CefURLParts parts;
    if (!CefParseURL(url, parts) ||
        CefString(&parts.scheme).ToString() != kUiScheme ||
        CefString(&parts.host).ToString() != kUiHost) {
        return {};
    }
    std::string rawPath = CefString(&parts.path).ToString();
    const std::size_t query = rawPath.find_first_of("?#");
    if (query != std::string::npos) rawPath.resize(query);
    if (!rawPath.empty() && rawPath.front() == '/') rawPath.erase(rawPath.begin());
    if (rawPath.empty()) rawPath = "index.html";
    for (std::size_t index = 0; index < rawPath.size(); ++index) {
        if (rawPath[index] == '%' && index + 2 < rawPath.size()) {
            unsigned int value = 0;
            const auto result = std::from_chars(rawPath.data() + index + 1, rawPath.data() + index + 3, value, 16);
            if (result.ec == std::errc{} && result.ptr == rawPath.data() + index + 3) {
                rawPath[index] = static_cast<char>(value);
                rawPath.erase(index + 1, 2);
            }
        }
    }
    std::replace(rawPath.begin(), rawPath.end(), '\\', '/');
    const std::filesystem::path normalized = std::filesystem::path(rawPath).lexically_normal();
    if (normalized.is_absolute()) return {};
    if (normalized.empty()) return "index.html";
    if (normalized.begin() != normalized.end() && normalized.begin()->string() == "..") return {};
    return normalized.generic_string();
}

[[nodiscard]] std::string MimeTypeForPath(const std::filesystem::path& filePath) {
    const std::string extension = filePath.extension().string();
    if (extension == ".html") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".js") return "application/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".woff2") return "font/woff2";
    if (extension == ".woff") return "font/woff";
    if (extension == ".ttf") return "font/ttf";
    return "application/octet-stream";
}

[[nodiscard]] std::optional<std::filesystem::path> WorldPreviewPathFromResource(
    const std::string& relativePath) {
    constexpr std::string_view prefix = "world-preview/";
    constexpr std::string_view suffix = ".png";
    if (!relativePath.starts_with(prefix) || !relativePath.ends_with(suffix)) return std::nullopt;
    const std::string saveName = relativePath.substr(
        prefix.size(), relativePath.size() - prefix.size() - suffix.size());
    if (saveName.empty()) return std::nullopt;

    std::error_code error;
    const std::filesystem::path saveRoot = std::filesystem::weakly_canonical(
        Paths::SavesDir() / kCurrentSaveFormatDirectory, error);
    if (error) return std::nullopt;
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(
        saveRoot / saveName / "preview.png", error);
    if (error || candidate.parent_path().parent_path() != saveRoot) return std::nullopt;
    return candidate;
}

[[nodiscard]] std::string StripBrowserPrefix(const std::filesystem::path& relativePath) {
    const std::string normalized = relativePath.generic_string();
    constexpr std::string_view prefix = "browser/";
    if (!normalized.starts_with(prefix)) return {};
    return normalized.substr(prefix.size());
}

[[nodiscard]] std::shared_ptr<VerifiedUiAssets> BuildVerifiedUiAssets(const std::filesystem::path& manifestPath,
                                                                      const WebUiManifest& manifest,
                                                                      std::string& entryHtmlPath) {
    auto assets = std::make_shared<VerifiedUiAssets>();
    for (const WebUiAsset& asset : manifest.assets) {
        const std::string relative = StripBrowserPrefix(asset.path);
        if (relative.empty()) continue;
        assets->files.emplace(relative, manifestPath.parent_path() / asset.path);
    }
    entryHtmlPath = StripBrowserPrefix(manifest.entryHtml);
    if (entryHtmlPath.empty() || assets->files.find(entryHtmlPath) == assets->files.end()) return {};
    return assets;
}

class UiResourceHandler final : public CefResourceHandler {
public:
    explicit UiResourceHandler(std::shared_ptr<const VerifiedUiAssets> assets)
        : m_assets(std::move(assets)) {}

    bool ProcessRequest(CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) override {
        m_payload.clear();
        m_offset = 0;
        m_statusCode = 404;
        m_statusText = "Not Found";
        m_mimeType = "text/plain; charset=utf-8";

        if (request == nullptr) return false;
        const std::string requestUrl = request->GetURL();
        const std::string relative = AssetPathFromUrl(requestUrl);
        std::cerr << "Web UI request: " << requestUrl << " -> '" << relative << "'\n";
        const auto it = m_assets->files.find(relative);
        std::filesystem::path resourcePath;
        if (it != m_assets->files.end()) {
            resourcePath = it->second;
        } else if (const auto previewPath = WorldPreviewPathFromResource(relative); previewPath.has_value()) {
            resourcePath = *previewPath;
        } else {
            std::cerr << "Web UI asset lookup failed for URL: " << requestUrl << '\n';
            const std::string body = "Missing UI asset: " + relative;
            m_payload.assign(body.begin(), body.end());
            callback->Continue();
            return true;
        }
        std::ifstream stream(resourcePath, std::ios::binary);
        if (!stream) {
            const std::string body = "Failed to open UI asset: " + relative;
            m_payload.assign(body.begin(), body.end());
            callback->Continue();
            return true;
        }
        m_payload.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        m_statusCode = 200;
        m_statusText = "OK";
        m_mimeType = MimeTypeForPath(resourcePath);
        std::cerr << "Web UI serving: " << relative << " as " << m_mimeType.ToString()
                  << " (" << m_payload.size() << " bytes)\n";
        callback->Continue();
        return true;
    }

    void GetResponseHeaders(CefRefPtr<CefResponse> response,
                            std::int64_t& responseLength,
                            CefString&) override {
        response->SetStatus(m_statusCode);
        response->SetStatusText(m_statusText);
        response->SetMimeType(m_mimeType);
        const std::string mime = m_mimeType.ToString();
        if (mime.starts_with("text/") || mime == "application/javascript" || mime == "application/json") {
            response->SetCharset("utf-8");
        }
        response->SetHeaderByName("Content-Type", mime, true);
        response->SetHeaderByName("Cache-Control", "no-cache", true);
        response->SetHeaderByName("X-Content-Type-Options", "nosniff", true);
        responseLength = static_cast<std::int64_t>(m_payload.size());
    }

    bool ReadResponse(void* dataOut,
                      int bytesToRead,
                      int& bytesRead,
                      CefRefPtr<CefCallback>) override {
        bytesRead = 0;
        if (m_offset >= m_payload.size() || bytesToRead <= 0) return false;
        const std::size_t remaining = m_payload.size() - m_offset;
        const std::size_t count = std::min<std::size_t>(remaining, static_cast<std::size_t>(bytesToRead));
        std::memcpy(dataOut, m_payload.data() + m_offset, count);
        m_offset += count;
        bytesRead = static_cast<int>(count);
        return true;
    }

    void Cancel() override {}

private:
    std::shared_ptr<const VerifiedUiAssets> m_assets;
    std::vector<std::uint8_t> m_payload;
    std::size_t m_offset = 0;
    int m_statusCode = 404;
    CefString m_statusText;
    CefString m_mimeType;
    IMPLEMENT_REFCOUNTING(UiResourceHandler);
};

class UiSchemeFactory final : public CefSchemeHandlerFactory {
public:
    explicit UiSchemeFactory(std::shared_ptr<const VerifiedUiAssets> assets)
        : m_assets(std::move(assets)) {}

    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>,
                                          CefRefPtr<CefFrame>,
                                          const CefString&,
                                          CefRefPtr<CefRequest>) override {
        return new UiResourceHandler(m_assets);
    }

private:
    std::shared_ptr<const VerifiedUiAssets> m_assets;
    IMPLEMENT_REFCOUNTING(UiSchemeFactory);
};

class ActionV8Handler final : public CefV8Handler {
public:
    bool Execute(const CefString&, CefRefPtr<CefV8Value>, const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>&, CefString&) override {
        if (arguments.size() != 1 || !arguments.front()->IsString()) return false;
        CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
        if (!context) return false;
        CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("voxels-action");
        message->GetArgumentList()->SetString(0, arguments.front()->GetStringValue());
        context->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, message);
        return true;
    }

private:
    IMPLEMENT_REFCOUNTING(ActionV8Handler);
};

class FileAccessApp final : public CefApp, public CefRenderProcessHandler {
public:
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
        registrar->AddCustomScheme(kUiScheme,
                                   CEF_SCHEME_OPTION_STANDARD |
                                   CEF_SCHEME_OPTION_SECURE |
                                   CEF_SCHEME_OPTION_CORS_ENABLED |
                                   CEF_SCHEME_OPTION_FETCH_ENABLED);
    }

    void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> commandLine) override {
        commandLine->AppendSwitch("disable-gpu");
        commandLine->AppendSwitch("disable-gpu-compositing");
    }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
    void OnContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override {
        if (!frame->IsMain()) return;
        CefRefPtr<CefV8Value> send = CefV8Value::CreateFunction("voxelsAction", new ActionV8Handler());
        context->GetGlobal()->SetValue("voxelsAction", send, V8_PROPERTY_ATTRIBUTE_NONE);
        context->GetGlobal()->SetValue("voxelsBridgeSend", send, V8_PROPERTY_ATTRIBUTE_NONE);
    }

private:
    IMPLEMENT_REFCOUNTING(FileAccessApp);
};

} // namespace

class WebUIManager::BrowserClient final : public CefClient,
                                           public CefRenderHandler,
                                           public CefLifeSpanHandler,
                                           public CefDisplayHandler,
                                           public CefLoadHandler,
                                           public CefRequestHandler {
public:
    explicit BrowserClient(WebUIManager& owner) : m_owner(owner) {}

    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, m_width, m_height); }
    bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& screenInfo) override {
        screenInfo.device_scale_factor = m_deviceScaleFactor;
        screenInfo.depth = 32;
        screenInfo.depth_per_component = 8;
        screenInfo.is_monochrome = false;
        screenInfo.rect = CefRect(0, 0, m_width, m_height);
        screenInfo.available_rect = screenInfo.rect;
        return true;
    }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        m_browser = browser;
        m_mainFrameReady = false;
        if (m_browser) {
            m_browser->GetHost()->WasResized();
            m_browser->GetHost()->Invalidate(PET_VIEW);
        }
    }
    void OnBeforeClose(CefRefPtr<CefBrowser>) override { m_browser = nullptr; m_mainFrameReady = false; }
    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int) override {
        if (!frame || !frame->IsMain()) return;
        m_mainFrameReady = true;
        m_owner.RepublishLatestModel();
    }
    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                     const CefString& errorText, const CefString& failedUrl) override {
        if (frame && frame->IsMain()) {
            std::cerr << "Web UI load error [" << static_cast<int>(code) << "] "
                      << errorText.ToString() << " at " << failedUrl.ToString() << '\n';
        }
    }
    bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                          const CefString& message, const CefString& source, int line) override {
        std::cerr << "Web UI console: " << source.ToString() << ':' << line << ' '
                  << message.ToString() << '\n';
        return false;
    }
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId,
                                  CefRefPtr<CefProcessMessage> message) override {
        if (message->GetName() != "voxels-action") return false;
        const std::string encoded = message->GetArgumentList()->GetString(0);
        if (const auto envelope = DecodePlayerUIProtocolMessage(encoded); envelope.has_value()) {
            if (const auto action = DecodeBridgeActionEnvelope(*envelope); action.has_value()) {
                m_owner.SubmitAction(*action);
                std::cerr << "Web UI action received: " << envelope->kind << '\n';
            } else {
                std::cerr << "Web UI rejected unsupported bridge action kind.\n";
            }
            return true;
        }
        if (const auto action = DecodeLegacyActionJson(encoded); action.has_value()) {
            m_owner.SubmitAction(*action);
            std::cerr << "Web UI action received (legacy envelope).\n";
        } else {
            std::cerr << "Web UI rejected malformed action payload.\n";
        }
        return true;
    }
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
        std::cerr << "Web UI browse request: " << url << '\n';
        if (url == "about:blank") return false;
        return !IsUiUrl(url);
    }
    void ResizeSurface(const WindowMetrics& windowMetrics) {
        const WebUiSurfaceMetrics metrics = ResolveWebUiSurfaceMetrics(
            windowMetrics.logicalWidth, windowMetrics.logicalHeight,
            windowMetrics.drawableWidth, windowMetrics.drawableHeight);
        if (metrics.viewWidth == m_width && metrics.viewHeight == m_height &&
            std::abs(metrics.deviceScaleFactor - m_deviceScaleFactor) < 0.001f) {
            return;
        }
        m_width = metrics.viewWidth;
        m_height = metrics.viewHeight;
        m_deviceScaleFactor = metrics.deviceScaleFactor;
        if (m_browser) {
            m_browser->GetHost()->NotifyScreenInfoChanged();
            m_browser->GetHost()->WasResized();
            m_browser->GetHost()->Invalidate(PET_VIEW);
        }
    }

    void ForwardEvent(const PlatformEvent& event) {
        if (!m_browser || m_owner.m_policy == PlayerUIInputPolicy::Gameplay) return;
        CefRefPtr<CefBrowserHost> host = m_browser->GetHost();
        CefMouseEvent mouseEvent{};
        mouseEvent.x = event.x;
        mouseEvent.y = event.y;
        mouseEvent.modifiers = CefModifiers(event);
        switch (event.type) {
            case PlatformEventType::MouseMotion: host->SendMouseMoveEvent(mouseEvent, false); break;
            case PlatformEventType::MouseButtonDown:
            case PlatformEventType::MouseButtonUp: {
                const cef_mouse_button_type_t button = event.button == 2 ? MBT_MIDDLE : event.button == 3 ? MBT_RIGHT : MBT_LEFT;
                if (event.type == PlatformEventType::MouseButtonDown) host->SetFocus(true);
                host->SendMouseClickEvent(mouseEvent, button, event.type == PlatformEventType::MouseButtonUp,
                                          std::max(event.clickCount, 1));
                break;
            }
            case PlatformEventType::MouseWheel:
                host->SendMouseWheelEvent(mouseEvent, event.wheelX * 120, event.wheelY * 120);
                break;
            case PlatformEventType::KeyDown:
            case PlatformEventType::KeyUp: {
                CefKeyEvent keyEvent{};
                keyEvent.type = event.type == PlatformEventType::KeyDown ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
                keyEvent.windows_key_code = CefWindowsKeyCode(event);
                keyEvent.native_key_code = event.scancode;
                keyEvent.modifiers = CefModifiers(event);
                keyEvent.is_system_key = (event.modifiers & PlatformModifierAlt) != 0U;
                host->SendKeyEvent(keyEvent);
                break;
            }
            case PlatformEventType::TextInput: {
                const std::u16string characters = CefString(event.text).ToString16();
                for (const char16_t character : characters) {
                    CefKeyEvent keyEvent{};
                    keyEvent.type = KEYEVENT_CHAR;
                    keyEvent.windows_key_code = static_cast<int>(character);
                    keyEvent.native_key_code = static_cast<int>(character);
                    keyEvent.character = character;
                    keyEvent.unmodified_character = character;
                    keyEvent.modifiers = CefModifiers(event);
                    host->SendKeyEvent(keyEvent);
                }
                break;
            }
            case PlatformEventType::ControllerButton: {
                const int keyCode = CefControllerKeyCode(event.button);
                if (keyCode == 0) break;
                CefKeyEvent keyEvent{};
                keyEvent.type = event.pressed ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
                keyEvent.windows_key_code = keyCode;
                keyEvent.native_key_code = event.button;
                host->SendKeyEvent(keyEvent);
                break;
            }
            case PlatformEventType::WindowFocusGained: host->SetFocus(true); break;
            case PlatformEventType::WindowFocusLost: host->SetFocus(false); break;
            default: break;
        }
    }
    /// Requests an asynchronous close; the browser stays alive (`OnBeforeClose` not yet fired)
    /// until the pumped message loop finishes tearing it down.
    void CloseBrowser() { if (m_browser) m_browser->GetHost()->CloseBrowser(true); }
    [[nodiscard]] bool HasBrowser() const noexcept { return static_cast<bool>(m_browser); }
    void Publish(const PlayerUIViewModel& model) {
        if (!m_browser || !m_mainFrameReady) return;
        const nlohmann::json modelPayload{{"route", static_cast<int>(model.route)},
                                          {"revision", model.revision},
                                          {"title", model.title},
                                          {"message", model.message},
                                          {"items", model.items},
                                          {"payload", model.payload},
                                          {"progress", model.progress},
                                          {"blocking", model.blocking}};
        const PlayerUIProtocolMessage bridgeMessage{
            .kind = kBridgeModelKind,
            .requestId = static_cast<std::uint32_t>((model.revision % static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max() - 1U)) + 1U),
            .payload = modelPayload.dump()};
        const std::optional<std::string> encoded = EncodePlayerUIProtocolMessage(bridgeMessage);
        if (!encoded.has_value()) return;
        const std::string script =
            "window.__voxelsLastBridgeMessage=" + *encoded + ";"
            "if(window.__voxelsReceiveBridgeMessage){window.__voxelsReceiveBridgeMessage(window.__voxelsLastBridgeMessage);}" 
            "window.__voxelsLastModel=" + modelPayload.dump() + ";"
            "if(window.__voxelsReceiveModel){window.__voxelsReceiveModel(window.__voxelsLastModel);}";
        m_browser->GetMainFrame()->ExecuteJavaScript(script, m_browser->GetMainFrame()->GetURL(), 0);
    }

private:
    static std::string DecodeQueryValue(std::string_view encoded) {
        std::string decoded;
        decoded.reserve(encoded.size());
        for (std::size_t index = 0; index < encoded.size(); ++index) {
            if (encoded[index] == '+' ) { decoded.push_back(' '); continue; }
            if (encoded[index] == '%' && index + 2 < encoded.size()) {
                unsigned int value = 0;
                const auto result = std::from_chars(encoded.data() + index + 1, encoded.data() + index + 3, value, 16);
                if (result.ec == std::errc{} && result.ptr == encoded.data() + index + 3) {
                    decoded.push_back(static_cast<char>(value));
                    index += 2;
                    continue;
                }
            }
            decoded.push_back(encoded[index]);
        }
        return decoded;
    }
    static std::string QueryValue(std::string_view url, std::string_view name) {
        const std::size_t query = url.find('?');
        if (query == std::string_view::npos) return {};
        std::size_t start = query + 1;
        while (start < url.size()) {
            const std::size_t end = url.find('&', start);
            const std::string_view pair = url.substr(start, end == std::string_view::npos ? url.size() - start : end - start);
            const std::size_t equals = pair.find('=');
            if (pair.substr(0, equals) == name) return DecodeQueryValue(equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return {};
    }
    static std::optional<PlayerUIAction> ParseAction(const std::string& kind, const std::string& url) {
        PlayerUIAction action{.requestId = 1, .primary = QueryValue(url, "primary"), .secondary = QueryValue(url, "secondary")};
        if (kind == "play") action.kind = PlayerUIActionKind::Play;
        else if (kind == "create-world") action.kind = PlayerUIActionKind::CreateWorld;
        else if (kind == "load-world") action.kind = PlayerUIActionKind::LoadWorld;
        else if (kind == "delete-world") action.kind = PlayerUIActionKind::DeleteWorld;
        else if (kind == "confirm-delete") action.kind = PlayerUIActionKind::ConfirmDelete;
        else if (kind == "join") action.kind = PlayerUIActionKind::Join;
        else if (kind == "resume") action.kind = PlayerUIActionKind::Resume;
        else if (kind == "controls" || kind == "open-controls") action.kind = PlayerUIActionKind::OpenControls;
        else if (kind == "toggle-world-visibility") action.kind = PlayerUIActionKind::ToggleWorldVisibility;
        else if (kind == "return-to-main-menu") action.kind = PlayerUIActionKind::ReturnToMainMenu;
        else if (kind == "exit-to-desktop") action.kind = PlayerUIActionKind::ExitToDesktop;
        else if (kind == "quit") action.kind = PlayerUIActionKind::Quit;
        else if (kind == "settings") action.kind = PlayerUIActionKind::OpenSettings;
        else if (kind == "back") action.kind = PlayerUIActionKind::Back;
        else if (kind == "apply-settings") action.kind = PlayerUIActionKind::ApplySettings;
        else if (kind == "dismiss-controls") action.kind = PlayerUIActionKind::DismissControls;
        else if (kind == "hotbar") action.kind = PlayerUIActionKind::HudHotbar;
        else if (kind == "open-chat") action.kind = PlayerUIActionKind::HudOpenChat;
        else if (kind == "close-chat") action.kind = PlayerUIActionKind::HudCloseChat;
        else if (kind == "send-chat") action.kind = PlayerUIActionKind::HudSendChat;
        else if (kind == "open-crafting") action.kind = PlayerUIActionKind::HudOpenCrafting;
        else if (kind == "close-crafting") action.kind = PlayerUIActionKind::HudCloseCrafting;
        else if (kind == "craft-recipe") action.kind = PlayerUIActionKind::HudCraftRecipe;
        else if (kind == "drop-item") action.kind = PlayerUIActionKind::HudDropItem;
        else if (kind == "acknowledge-error") action.kind = PlayerUIActionKind::AcknowledgeError;
        else return std::nullopt;
        const std::string requestId = QueryValue(url, "requestId");
        const auto result = std::from_chars(requestId.data(), requestId.data() + requestId.size(), action.requestId);
        if (result.ec != std::errc{} || result.ptr != requestId.data() + requestId.size()) return std::nullopt;
        return action.requestId == 0 ? std::nullopt : std::optional<PlayerUIAction>{action};
    }
    WebUIManager& m_owner;
    CefRefPtr<CefBrowser> m_browser;
    int m_width = 1280;
    int m_height = 720;
    float m_deviceScaleFactor = 1.0f;
    bool m_mainFrameReady = false;
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
    const std::filesystem::path manifestPath = Paths::AssetsDir() / "ui/ui-manifest.json";
    WebUiManifest manifest; std::string error;
    if (!LoadAndVerifyWebUiManifest(manifestPath, manifest, error)) return false;
    m_entryHtmlPath.clear();
    std::shared_ptr<VerifiedUiAssets> assets = BuildVerifiedUiAssets(manifestPath, manifest, m_entryHtmlPath);
    if (!assets) return false;
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
    if (!CefRegisterSchemeHandlerFactory(kUiScheme, kUiHost, new UiSchemeFactory(std::move(assets)))) {
        CefShutdown();
        return false;
    }
    m_platform = platform;
    m_lastWindowMetrics = platform->GetWindowMetrics();
    m_client = new BrowserClient(*this);
    m_client->ResizeSurface(m_lastWindowMetrics);
    CefWindowInfo info; info.SetAsWindowless(nullptr);
    const std::string url = std::string(kUiOriginPrefix) + m_entryHtmlPath;
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
    CefClearSchemeHandlerFactories();
    CefShutdown();
    m_compositor.Shutdown();
    m_lastModel.reset();
    m_entryHtmlPath.clear();
    m_initialized = false;
    m_platform = nullptr;
}
void WebUIManager::BeginFrame() {
    if (!m_initialized) return;
    SyncSurfaceSizeFromPlatform();
    CefDoMessageLoopWork();
    m_frameActive = true;
}
void WebUIManager::EndFrame() { if (m_frameActive) static_cast<void>(m_compositor.UploadAndComposite(m_frames)); m_frameActive = false; }
void WebUIManager::OnPlatformEvent(const PlatformEvent& event) {
    if (!m_client) return;
    if (event.type == PlatformEventType::WindowResized && m_platform != nullptr) {
        m_lastWindowMetrics = m_platform->GetWindowMetrics();
        m_client->ResizeSurface(m_lastWindowMetrics);
    }
    m_client->ForwardEvent(event);
}
void WebUIManager::SetInputPolicy(PlayerUIInputPolicy policy) {
    m_discardNextMouseDelta = policy == PlayerUIInputPolicy::Gameplay && m_policy != policy;
    m_policy = policy;
    if (m_platform != nullptr) {
        const bool gameplay = policy == PlayerUIInputPolicy::Gameplay;
        m_platform->SetRelativeMouseMode(gameplay);
        m_platform->SetCursorVisible(!gameplay);
        m_platform->SetTextInputEnabled(policy == PlayerUIInputPolicy::TextEntry);
    }
}
bool WebUIManager::ConsumeTransitionMouseDelta() noexcept { const bool discard = m_discardNextMouseDelta; m_discardNextMouseDelta = false; return discard; }
void WebUIManager::Publish(PlayerUIViewModel model) {
    m_lastModel = std::move(model);
    RepublishLatestModel();
}
std::optional<PlayerUIAction> WebUIManager::ConsumeAction() { if (m_actions.empty()) return std::nullopt; PlayerUIAction action = std::move(m_actions.front()); m_actions.erase(m_actions.begin()); return action; }
void WebUIManager::ShowToast(std::string, float) {}
void WebUIManager::SubmitAction(PlayerUIAction action) { if (m_actions.empty() || m_actions.back().requestId != action.requestId) m_actions.push_back(std::move(action)); }
void WebUIManager::RepublishLatestModel() {
    if (m_client && m_lastModel.has_value()) m_client->Publish(*m_lastModel);
}

void WebUIManager::SyncSurfaceSizeFromPlatform() {
    if (m_platform == nullptr || !m_client) return;
    const WindowMetrics metrics = m_platform->GetWindowMetrics();
    if (metrics.logicalWidth == m_lastWindowMetrics.logicalWidth &&
        metrics.logicalHeight == m_lastWindowMetrics.logicalHeight &&
        metrics.drawableWidth == m_lastWindowMetrics.drawableWidth &&
        metrics.drawableHeight == m_lastWindowMetrics.drawableHeight) {
        return;
    }
    m_lastWindowMetrics = metrics;
    m_client->ResizeSurface(m_lastWindowMetrics);
}

} // namespace voxels
