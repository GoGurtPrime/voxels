/**
 * @file test_platform.cpp
 * @brief Unit tests for Work Item 02: platform/windowing abstraction.
 */

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "voxels/engine.hpp"
#include "voxels/platform/headless_platform.hpp"
#include "voxels/platform/platform.hpp"

namespace {

/// Captures every event delivered by `IPlatform::PollEvents` for assertions.
class RecordingListener final : public voxels::IPlatformEventListener {
public:
    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        events.push_back(event);
    }

    std::vector<voxels::PlatformEvent> events;
};

} // namespace

TEST_CASE("Platform.HeadlessInitialization", "[platform]") {
    voxels::HeadlessPlatform platform;
    REQUIRE_FALSE(platform.IsInitialized());

    voxels::WindowConfig config;
    config.title = "Voxels Test";
    config.width = 1024;
    config.height = 768;
    config.fullscreen = false;

    REQUIRE(platform.Initialize(config));
    REQUIRE(platform.IsInitialized());
    REQUIRE(platform.GetWidth() == 1024);
    REQUIRE(platform.GetHeight() == 768);
    REQUIRE_FALSE(platform.IsFullscreen());
    REQUIRE(platform.GetContext().name == "Headless");

    platform.Shutdown();
    REQUIRE_FALSE(platform.IsInitialized());

    // The factory must always succeed in producing a usable platform, even in headless builds.
    const auto defaultPlatform = voxels::CreateDefaultPlatform();
    REQUIRE(defaultPlatform != nullptr);
    REQUIRE(defaultPlatform->Initialize(voxels::WindowConfig{}));
    defaultPlatform->Shutdown();
}

TEST_CASE("Platform.EventPumpDispatch", "[platform]") {
    voxels::HeadlessPlatform platform;
    REQUIRE(platform.Initialize(voxels::WindowConfig{}));

    RecordingListener listener;

    // Simulate an injected OS window resize event, as a real SDL2 backend would report.
    platform.SimulateEvent(
        voxels::PlatformEvent{voxels::PlatformEventType::WindowResized, 1920, 1080});
    // Simulate an injected OS window close event (e.g. SDL_QUIT).
    platform.SimulateEvent(voxels::PlatformEvent{voxels::PlatformEventType::WindowClosed, 0, 0});

    platform.PollEvents(&listener);

    REQUIRE(listener.events.size() == 2);
    REQUIRE(listener.events[0].type == voxels::PlatformEventType::WindowResized);
    REQUIRE(listener.events[0].width == 1920);
    REQUIRE(listener.events[0].height == 1080);
    REQUIRE(listener.events[1].type == voxels::PlatformEventType::WindowClosed);

    // The queue must be drained after dispatch.
    listener.events.clear();
    platform.PollEvents(&listener);
    REQUIRE(listener.events.empty());

    // SetWindowResolution should itself surface a resize event on the next poll.
    platform.SetWindowResolution(640, 480);
    platform.PollEvents(&listener);
    REQUIRE(listener.events.size() == 1);
    REQUIRE(listener.events[0].type == voxels::PlatformEventType::WindowResized);
    REQUIRE(listener.events[0].width == 640);
    REQUIRE(listener.events[0].height == 480);
    REQUIRE(platform.GetWidth() == 640);
    REQUIRE(platform.GetHeight() == 480);

    // Poll with no listener must not crash and must still drain the queue.
    platform.SetWindowFullscreen(true);
    platform.PollEvents(nullptr);
    REQUIRE(platform.IsFullscreen());
}

TEST_CASE("Platform.HighResTimer", "[platform]") {
    voxels::HeadlessPlatform platform;
    REQUIRE(platform.Initialize(voxels::WindowConfig{}));

    const double t1 = platform.GetHighResTimeSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const double t2 = platform.GetHighResTimeSeconds();

    REQUIRE(t1 > 0.0);
    REQUIRE(t2 > t1);
    REQUIRE((t2 - t1) > 0.0);
}

TEST_CASE("Platform.EngineLifecycleOwnsPlatform", "[platform][engine]") {
    voxels::Engine engine;
    REQUIRE(engine.getPlatform() == nullptr);

    REQUIRE(engine.initialize());
    REQUIRE(engine.getPlatform() != nullptr);

    engine.shutdown();
    REQUIRE(engine.getPlatform() == nullptr);
}
