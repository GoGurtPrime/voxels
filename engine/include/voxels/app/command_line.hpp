#pragma once

/*
 * Scope: Command-line argument abstraction for app startup.
 *
 * Command-line options override defaults and configuration files, supporting local server
 * execution and editor-specific launch modes without hard-coding game startup rules.
 *
 * Relation to the rest of the codebase: the runtime app and future tooling consume this
 * interface to determine which mode to boot in and what settings to apply.
 */

#include <string>
#include <vector>

#include "voxels/core/game_types.hpp"

namespace voxels {

class ICommandLineParser {
public:
    virtual ~ICommandLineParser() = default;
    virtual AppCommandLineOptions Parse(const std::vector<std::string>& args) const = 0;
};

} // namespace voxels
