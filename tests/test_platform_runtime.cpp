#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "voxels/app/cli_parser.hpp"
#include "voxels/core/job_system.hpp"
#include "voxels/platform/headless_platform.hpp"
#include "voxels/platform/platform.hpp"

namespace {

class PriorityListener final : public voxels::IPlatformEventListener {
public:
    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        events.push_back(event);
    }

    std::vector<voxels::PlatformEvent> events;
};

class PauseRequestListener final : public voxels::IPlatformEventListener {
public:
    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        if (event.type == voxels::PlatformEventType::WindowFocusLost) {
            pausedRequested = true;
            mouseCaptured = false;
        }
        if (event.type == voxels::PlatformEventType::WindowFocusGained) {
            mouseCaptured = true;
        }
    }

    bool pausedRequested = false;
    bool mouseCaptured = true;
};

void TickAccumulatorFrame(voxels::FrameAccumulator& accumulator, double deltaSeconds, int& simTicks) {
    accumulator.Accumulate(deltaSeconds);
    simTicks += accumulator.Resolve(1.0 / 60.0);
}

} // namespace

TEST_CASE("FrameLoop.FixedStepAccumulatorRunsExpectedTicks", "[platform][runtime]") {
    voxels::FrameAccumulator accumulator;
    int simTicks = 0;

    for (int i = 0; i < 60; ++i) {
        TickAccumulatorFrame(accumulator, 1.0 / 60.0, simTicks);
    }

    REQUIRE(simTicks == 60);
    REQUIRE(accumulator.GetRemainder() < 1.0 / 60.0);
}

TEST_CASE("FrameLoop.LargeDeltaIsClamped", "[platform][runtime]") {
    voxels::FrameAccumulator accumulator;
    int simTicks = 0;

    TickAccumulatorFrame(accumulator, 5.0, simTicks);

    REQUIRE(simTicks <= 300);
    REQUIRE(accumulator.GetRemainder() >= 0.0);
}

TEST_CASE("Platform.EventDispatchOrderRespectsListenerPriority", "[platform][runtime]") {
    voxels::HeadlessPlatform platform;
    REQUIRE(platform.Initialize({"Priority", 800, 600, false, true}));

    PriorityListener uiListener;
    PriorityListener gameplayListener;

    platform.RegisterEventListener(&uiListener, 10);
    platform.RegisterEventListener(&gameplayListener, 0);

    platform.SimulateEvent(voxels::PlatformEvent{voxels::PlatformEventType::WindowResized, 1280, 720});
    platform.PollEvents(nullptr);

    REQUIRE(uiListener.events.size() == 1);
    REQUIRE(gameplayListener.events.size() == 1);
    REQUIRE(uiListener.events[0].width == 1280);
    REQUIRE(gameplayListener.events[0].width == 1280);

    platform.Shutdown();
}

TEST_CASE("Platform.FocusLossReleasesMouseCaptureAndRequestsPause", "[platform][runtime]") {
    voxels::HeadlessPlatform platform;
    REQUIRE(platform.Initialize({"Focus", 640, 480, false, true}));

    PauseRequestListener listener;
    platform.RegisterEventListener(&listener, 50);

    platform.SimulateEvent(voxels::PlatformEvent{voxels::PlatformEventType::WindowFocusLost, 0, 0});
    platform.PollEvents(nullptr);

    REQUIRE(listener.pausedRequested);
    REQUIRE_FALSE(listener.mouseCaptured);

    platform.Shutdown();
}

TEST_CASE("JobSystem.ExecutesEnqueuedWorkAndShutsDownCleanly", "[job_system]") {
    voxels::JobSystem jobs(2);
    std::atomic<int> count = 0;

    for (int i = 0; i < 10; ++i) {
        jobs.Enqueue([&count]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ++count;
        });
    }

    jobs.DrainCompleted();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    jobs.DrainCompleted();

    REQUIRE(count == 10);

    auto resultFuture = jobs.EnqueueWithResult([]() { return 42; });
    REQUIRE(resultFuture.get() == 42);
}

TEST_CASE("Cli.OverridesPreferences", "[platform][cli]") {
    const voxels::CliParser parser;
    const auto options = parser.Parse({
        "--fullscreen=true",
        "--resolution=1920x1080",
        "--vsync=false",
        "--headless",
        "--max-frames=30",
        "--world=Alpha",
    });

    REQUIRE(options.fullscreenOverride);
    REQUIRE(options.fullscreenValue);
    REQUIRE(options.resolutionOverride);
    REQUIRE(options.resolutionWidth == 1920);
    REQUIRE(options.resolutionHeight == 1080);
    REQUIRE(options.headless);
    REQUIRE(options.vsyncOverride);
    REQUIRE_FALSE(options.vsyncValue);
    REQUIRE(options.maxFrames == 30);
    REQUIRE(options.worldNameOverride);
    REQUIRE(options.worldName == "Alpha");
}
