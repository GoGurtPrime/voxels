#pragma once

/*
 * Scope: Platform abstraction and target-specific runtime interfaces.
 *
 * This header provides a platform-neutral entry point for windowing, system services,
 * and target detection. The actual implementation will later dispatch to SDL2, native
 * APIs, or KallistiOS-specific code depending on the active platform configuration.
 *
 * Relation to the rest of the codebase: the engine, app, and editor all rely on this layer
 * for event pumping, platform context, and hardware/runtime integration.
 */

#include <cstdint>
#include <memory>
#include <string>

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

/// Requested window/display parameters passed to `IPlatform::Initialize`. Platforms with fixed
/// hardware displays (e.g. Dreamcast) may ignore fields that do not apply.
struct WindowConfig {
    std::string title = "Voxels Engine";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
};

enum class PlatformEventType {
    WindowClosed,
    WindowResized,
    WindowFocusGained,
    WindowFocusLost
};

/// Native OS/window event normalized into an engine-agnostic representation.
struct PlatformEvent {
    PlatformEventType type = PlatformEventType::WindowClosed;
    int width = 0;
    int height = 0;
};

/// Receives normalized platform events dispatched from `IPlatform::PollEvents`.
class IPlatformEventListener {
public:
    virtual ~IPlatformEventListener() = default;
    virtual void OnPlatformEvent(const PlatformEvent& event) = 0;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual PlatformContext GetContext() const = 0;
    virtual bool Initialize(const WindowConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual void PollEvents(IPlatformEventListener* listener) = 0;
    virtual void SwapBuffers() = 0;
    virtual void SetWindowFullscreen(bool fullscreen) = 0;
    virtual void SetWindowResolution(int width, int height) = 0;
    virtual double GetHighResTimeSeconds() const = 0;
};

/// Constructs the appropriate `IPlatform` implementation for the active build target:
/// Dreamcast stub when `VOXELS_ENABLE_DREAMCAST` is set, SDL2 when linked in
/// (`VOXELS_HAS_SDL2`), otherwise an in-memory headless fallback used for tooling and tests.
std::unique_ptr<IPlatform> CreateDefaultPlatform();

} // namespace voxels
