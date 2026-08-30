/*
 * Scope: Application bootstrap and lifecycle entry point.
 *
 * This file is the placeholder for launching the runtime game, parsing command-line options,
 * and transitioning through the menu, world setup, loading, and gameplay states.
 *
 * Relation to the rest of the codebase: the game app depends on the engine runtime services
 * and should remain focused on orchestration rather than low-level systems implementation.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/core/job_system.hpp"
#include "voxels/engine.hpp"
#include "voxels/graphics/gl_renderer.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/platform/platform.hpp"

namespace {

class QuitOnWindowClosedListener final : public voxels::IPlatformEventListener {
public:
    explicit QuitOnWindowClosedListener(bool& runningFlag) : m_running(runningFlag) {}

    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        if (event.type == voxels::PlatformEventType::WindowClosed ||
            event.type == voxels::PlatformEventType::QuitRequested) {
            m_running = false;
        }
    }

private:
    bool& m_running;
};

constexpr double kFixedStepSeconds = 1.0 / 60.0;

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const voxels::CliParser cliParser;
    const voxels::AppCommandLineOptions options = cliParser.Parse(args);

    if (options.serverMode) {
        std::cout << "Voxels app booting in headless server mode." << std::endl;
        voxels::networking::GameServer server;
        if (!server.Start()) {
            std::cerr << "Voxels server failed to start." << std::endl;
            return 1;
        }
        std::cout << "Voxels server listening on port " << server.Port() << "." << std::endl;
        for (int tick = 0; tick < std::max(1, options.maxTicks > 0 ? options.maxTicks : 1); ++tick) {
            server.Tick();
        }
        server.Stop();
        return 0;
    }

    if (options.headless) {
        voxels::Engine engine;
        if (!engine.initialize(true)) {
            std::cerr << "Voxels engine failed to initialize in headless mode." << std::endl;
            return 1;
        }

        const int maxFrames = options.maxFrames > 0 ? options.maxFrames : 2;
        for (int frame = 0; frame < maxFrames; ++frame) {
            auto* platform = engine.getPlatform();
            if (platform != nullptr) {
                platform->PollEvents(nullptr);
                platform->SwapBuffers();
            }
        }

        engine.shutdown();
        std::cout << "Voxels headless smoke test completed after " << maxFrames << " frames.\n";
        return 0;
    }

    voxels::Engine engine;
    if (!engine.initialize(false)) {
        std::cerr << "Voxels engine failed to initialize." << std::endl;
        return 1;
    }

    auto* platform = engine.getPlatform();
    if (platform != nullptr) {
        voxels::WindowConfig config{};
        config.title = "Voxels Engine";
        config.width = options.resolutionOverride ? options.resolutionWidth : 1280;
        config.height = options.resolutionOverride ? options.resolutionHeight : 720;
        config.fullscreen = options.fullscreenOverride ? options.fullscreenValue : false;
        config.resizable = true;
        platform->Shutdown();
        if (!platform->Initialize(config)) {
            std::cerr << "Voxels platform failed to initialize with the configured window settings." << std::endl;
            return 1;
        }
        platform->SetWindowTitle("Voxels Engine");
        if (options.vsyncOverride) {
            platform->SetVSync(options.vsyncValue);
        }
    }

    voxels::graphics::GLRenderer renderer;
    if (!renderer.Initialize()) {
        std::cerr << "Voxels GL renderer failed to initialize." << std::endl;
        engine.shutdown();
        return 1;
    }

    voxels::SetGlobalRenderer(&renderer);

    voxels::AppStateMachine stateMachine;
    stateMachine.Start(std::make_unique<voxels::BootState>());
    stateMachine.TransitionTo(std::make_unique<voxels::MainMenuState>());

    voxels::networking::GameServer localServer;
    voxels::networking::GameClient localClient;
    if (!localServer.Start("127.0.0.1", 0) || !localClient.Connect("127.0.0.1", localServer.Port())) {
        std::cerr << "Voxels local server failed to start." << std::endl;
        engine.shutdown();
        return 1;
    }

    bool running = true;
    QuitOnWindowClosedListener windowCloseListener(running);
    if (platform != nullptr) {
        platform->RegisterEventListener(&windowCloseListener, 100);
    }

    voxels::FrameAccumulator frameAccumulator;
    auto lastTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    const int maxFrames = options.maxFrames > 0 ? options.maxFrames : std::numeric_limits<int>::max();

    while (running && frameCount < maxFrames) {
        if (platform != nullptr) {
            platform->PollEvents(&windowCloseListener);
        }

        const auto now = std::chrono::steady_clock::now();
        const double deltaSeconds = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        frameAccumulator.Accumulate(deltaSeconds);

        const int simTicks = frameAccumulator.Resolve(kFixedStepSeconds);
        for (int i = 0; i < simTicks; ++i) {
            localServer.Tick();
            localClient.Tick();
            stateMachine.Update(kFixedStepSeconds);
        }

        stateMachine.Render();
        if (platform != nullptr) {
            platform->SwapBuffers();
        }

        ++frameCount;
    }

    localClient.Disconnect();
    localServer.Stop();
    std::cout << "Voxels app loop exited after " << frameCount << " frames.\n";
    voxels::SetGlobalRenderer(nullptr);
    renderer.Shutdown();
    engine.shutdown();
    return 0;
}
