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
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/engine.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/platform/platform.hpp"

namespace {

/// Bridges platform window-close notifications into the app's main-loop exit condition.
class QuitOnWindowClosedListener final : public voxels::IPlatformEventListener {
public:
    explicit QuitOnWindowClosedListener(bool& runningFlag) : m_running(runningFlag) {}

    void OnPlatformEvent(const voxels::PlatformEvent& event) override {
        if (event.type == voxels::PlatformEventType::WindowClosed) {
            m_running = false;
        }
    }

private:
    bool& m_running;
};

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

    voxels::Engine engine;
    if (!engine.initialize()) {
        std::cerr << "Voxels engine failed to initialize." << std::endl;
        return 1;
    }

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

    // Platforms without a real window (e.g. HeadlessPlatform in CI/tooling) never deliver a
    // WindowClosed event, so fall back to a bounded tick count unless the caller overrode it.
    const bool hasRealWindow =
        engine.getPlatform() != nullptr && engine.getPlatform()->GetContext().name == "SDL2";
    const int maxTicks = options.maxTicksOverride  ? options.maxTicks
                          : hasRealWindow           ? std::numeric_limits<int>::max()
                                                     : 200;
    int tickCount = 0;
    constexpr double kTargetFrameSeconds = 1.0 / 60.0;
    auto lastTime = std::chrono::steady_clock::now();

    while (running && tickCount < maxTicks) {
        if (engine.getPlatform() != nullptr) {
            engine.getPlatform()->PollEvents(&windowCloseListener);
        }

        const auto now = std::chrono::steady_clock::now();
        const double deltaSeconds = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        localServer.Tick();
        localClient.Tick();
        stateMachine.Update(deltaSeconds);
        stateMachine.Render();
        ++tickCount;

        if (hasRealWindow) {
            const auto frameEnd = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(frameEnd - now).count();
            const double remaining = kTargetFrameSeconds - elapsed;
            if (remaining > 0.0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
            }
        }
    }

    localClient.Disconnect();
    localServer.Stop();
    std::cout << "Voxels app loop exited after " << tickCount << " ticks.\n";
    engine.shutdown();
    return 0;
}
