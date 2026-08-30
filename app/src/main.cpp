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
#include <memory>
#include <vector>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/engine.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/platform/platform.hpp"

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
    int maxTicks = options.maxTicks > 0 ? options.maxTicks : 200;
    int tickCount = 0;
    const auto lastTime = std::chrono::steady_clock::now();
    (void)lastTime;

    while (running && tickCount < maxTicks) {
        if (engine.getPlatform() != nullptr) {
            engine.getPlatform()->PollEvents(nullptr);
        }

        localServer.Tick();
        localClient.Tick();
        stateMachine.Update(1.0 / 60.0);
        stateMachine.Render();
        ++tickCount;
    }

    localClient.Disconnect();
    localServer.Stop();
    std::cout << "Voxels app loop exited after " << tickCount << " ticks.\n";
    engine.shutdown();
    return 0;
}
