#pragma once

/**
 * @file command_line.hpp
 * @brief Parser interface turning raw argv strings into `AppCommandLineOptions`.
 *
 * @details Command-line options override defaults and the on-disk configuration file,
 *          supporting headless/server execution and tooling launch modes without
 *          hard-coding startup rules. The runtime app consumes this interface to decide
 *          which mode to boot in; `CliParser` (cli_parser.hpp) is the shipping
 *          implementation.
 */

#include <string>
#include <vector>

#include "voxels/core/game_types.hpp"

namespace voxels {

/// Strategy interface for translating program arguments into typed startup options.
class ICommandLineParser {
public:
    virtual ~ICommandLineParser() = default;
    /// Parses `args` (argv without the program name). Implementations must ignore unknown
    /// or malformed flags rather than fail, so stale command lines never block startup.
    virtual AppCommandLineOptions Parse(const std::vector<std::string>& args) const = 0;
};

} // namespace voxels
