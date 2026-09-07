#pragma once

/**
 * @file platform.hpp
 * @brief Platform abstraction: windowing, event pumping, timing, and target detection.
 *
 * @details Defines `IPlatform`, the engine-neutral contract implemented by SdlPlatform
 *          (desktop), HeadlessPlatform (tests/CI), and DreamcastPlatform (declared), plus the
 *          normalized `PlatformEvent` stream consumed by input, UI, and the app shell.
 *          `CreateDefaultPlatform()` selects the implementation for the active build target.
 *          See ARCHITECTURE.md §Platform Layer and DIAGRAMS.md (boot / input flow).
 */

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace voxels {

enum class PlatformType {
    Windows,
    Linux,
    MacOS,
    Dreamcast,
    Unknown
};

struct PlatformContext {
    PlatformType type = PlatformType::Unknown;
    std::string name;
    int majorVersion = 0;
    int minorVersion = 0;
    int patchVersion = 0;
};

enum class WindowGraphicsApi {
    OpenGL,
    Native
};

/// Requested window/display parameters passed to `IPlatform::Initialize`. Platforms with fixed
/// hardware displays (e.g. Dreamcast) may ignore fields that do not apply.
struct WindowConfig {
    std::string title = "Voxels Engine";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
    WindowGraphicsApi graphicsApi = WindowGraphicsApi::OpenGL;
};

enum class WindowPresentationMode {
    Windowed,
    Borderless,
    Fullscreen
};

/// Complete display transition requested from the platform. Applying all fields in one call
/// prevents native window state from being observed between partially-applied mode changes.
struct WindowDisplayConfig {
    int width = 1280;
    int height = 720;
    WindowPresentationMode mode = WindowPresentationMode::Windowed;
    bool resizable = true;
};

/// Authoritative native window dimensions. Logical dimensions are the coordinate space used by
/// SDL input events; drawable dimensions are framebuffer pixels and may differ on high-DPI output.
struct WindowMetrics {
    int logicalWidth = 1;
    int logicalHeight = 1;
    int drawableWidth = 1;
    int drawableHeight = 1;
};

enum class PlatformEventType {
    None = 0,
    WindowClosed,
    WindowResized,
    WindowFocusGained,
    WindowFocusLost,
    WindowMinimized,
    WindowRestored,
    KeyDown,
    KeyUp,
    TextInput,
    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    ControllerAdded,
    ControllerRemoved,
    ControllerButton,
    ControllerAxis,
    QuitRequested
};

enum PlatformEventModifier : std::uint32_t {
    PlatformModifierNone = 0U,
    PlatformModifierShift = 1U << 0U,
    PlatformModifierControl = 1U << 1U,
    PlatformModifierAlt = 1U << 2U,
    PlatformModifierSuper = 1U << 3U,
    PlatformModifierCapsLock = 1U << 4U,
    PlatformModifierNumLock = 1U << 5U,
    PlatformModifierLeftMouse = 1U << 6U,
    PlatformModifierMiddleMouse = 1U << 7U,
    PlatformModifierRightMouse = 1U << 8U
};

/// Native OS/window event normalized into an engine-agnostic representation.
struct PlatformEvent {
    PlatformEventType type = PlatformEventType::None;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    int relativeX = 0;
    int relativeY = 0;
    int wheelX = 0;
    int wheelY = 0;
    int button = 0;
    int clickCount = 0;
    int controllerIndex = 0;
    std::uint32_t keyCode = 0;
    std::uint32_t scancode = 0;
    std::uint32_t modifiers = 0;
    bool repeat = false;
    float axisValue = 0.0f;
    std::string text;
};

/// Receives normalized platform events dispatched from `IPlatform::PollEvents`.
class IPlatformEventListener {
public:
    virtual ~IPlatformEventListener() = default;
    virtual void OnPlatformEvent(const PlatformEvent& event) = 0;
};

/// Platform-neutral windowing/event/timing contract. Implementations own the native window,
/// GL context, and event queue; all methods must be called from the main thread (ADR-008).
class IPlatform {
public:
    virtual ~IPlatform() = default;

    /// Identifies the concrete backend (type, name, backend version) for logging and dispatch.
    virtual PlatformContext GetContext() const = 0;

    /// Creates the native window/context per `config`. Returns false on unrecoverable failure;
    /// callers must treat that as fatal at startup (no silent headless fallback).
    virtual bool Initialize(const WindowConfig& config) = 0;

    /// Destroys the window and releases platform resources; safe to call more than once.
    virtual void Shutdown() = 0;

    /// Drains the native event queue, forwarding each normalized event to `listener` and to
    /// all listeners registered via RegisterEventListener (in descending priority order).
    virtual void PollEvents(IPlatformEventListener* listener) = 0;

    /// Presents the back buffer (no-op on headless).
    virtual void SwapBuffers() = 0;

    /// Applies a complete window mode transition. Returns false if the platform cannot establish
    /// either the requested mode or a coverage-preserving fullscreen fallback.
    [[nodiscard]] virtual bool ApplyWindowDisplayConfig(const WindowDisplayConfig& config) = 0;

    virtual void SetWindowFullscreen(bool fullscreen) = 0;
    virtual void SetWindowBorderless(bool borderless) = 0;
    virtual void SetWindowResizable(bool resizable) = 0;
    virtual void SetWindowResolution(int width, int height) = 0;
    virtual void SetWindowTitle(const std::string& title) = 0;

    /// Enables relative (captured) mouse mode for FPS-style look; motion arrives as deltas.
    virtual void SetRelativeMouseMode(bool enabled) = 0;
    virtual void SetCursorVisible(bool visible) = 0;
    virtual void SetVSync(bool enabled) = 0;

    /// Returns the drawable framebuffer size in pixels, which may differ from the logical
    /// window size on high-DPI displays.
    virtual std::pair<int, int> GetDrawableSize() const = 0;

    /// Returns logical input-space and drawable framebuffer dimensions from one native snapshot.
    [[nodiscard]] virtual WindowMetrics GetWindowMetrics() const = 0;

    /// Opaque native window object used by graphics/UI adapters. SDL platforms return the
    /// SDL_Window pointer; headless platforms return null.
    [[nodiscard]] virtual void* GetNativeWindowHandle() const noexcept { return nullptr; }

    /// Monotonic high-resolution timestamp in seconds; the frame loop's time source.
    virtual double GetHighResTimeSeconds() const = 0;

    /// Adds a persistent event listener. Higher `priority` receives events first (UI layers
    /// register above gameplay so they can consume input). Default impl: optional feature.
    virtual void RegisterEventListener(IPlatformEventListener* listener, int priority = 0) {
        (void)listener;
        (void)priority;
    }
    virtual void UnregisterEventListener(IPlatformEventListener* listener) {
        (void)listener;
    }
};

/// Constructs the appropriate `IPlatform` implementation for the active build target:
/// Dreamcast stub when `VOXELS_ENABLE_DREAMCAST` is set, SDL2 when linked in
/// (`VOXELS_HAS_SDL2`), otherwise an in-memory headless fallback used for tooling and tests.
std::unique_ptr<IPlatform> CreateDefaultPlatform(bool headless = false);

} // namespace voxels
