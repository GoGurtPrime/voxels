/*
 * Scope: Application bootstrap and lifecycle entry point.
 *
 * This file is the placeholder for launching the runtime game, parsing command-line options,
 * and transitioning through the menu, world setup, loading, and gameplay states.
 *
 * Relation to the rest of the codebase: the game app depends on the engine runtime services
 * and should remain focused on orchestration rather than low-level systems implementation.
 */

#include <iostream>
#include <memory>
#include <vector>

#include "voxels/app/cli_parser.hpp"
#include "voxels/app/state_machine.hpp"
#include "voxels/engine.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"

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
        server.Tick();
        std::cout << "Voxels server listening on port " << server.Port() << "." << std::endl;
        server.Stop();
        return 0;
    }

    voxels::Engine engine;
    if (!engine.initialize()) {
        std::cerr << "Voxels engine failed to initialize." << std::endl;
        return 1;
    }

    voxels::networking::GameServer localServer;
    voxels::networking::GameClient localClient;
    if (!localServer.Start("127.0.0.1", 0) || !localClient.Connect("127.0.0.1", localServer.Port())) {
        std::cerr << "Voxels local server failed to start." << std::endl;
        engine.shutdown();
        return 1;
    }
    localServer.Tick();
    localClient.Tick();

    voxels::AppStateMachine stateMachine;
    stateMachine.Start(std::make_unique<voxels::BootState>());
    stateMachine.TransitionTo(std::make_unique<voxels::MainMenuState>());

    std::cout << "Voxels app scaffold initialized (engine v" << engine.getVersion() << ")." << std::endl;

    localClient.Disconnect();
    localServer.Stop();
    engine.shutdown();
    return 0;
}
