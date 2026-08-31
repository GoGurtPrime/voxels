#pragma once

/*
 * Scope: Concrete command-line argument parser implementation.
 *
 * Supports `--fullscreen=<true|false>`, `--resolution=<WIDTHxHEIGHT>`, `--server`,
 * `--port=<PORT>`, `--join=<HOST:PORT>`, `--world=<NAME>`, `--seed=<SEED>`, and
 * `--render-distance=<CHUNKS>`, overriding whatever defaults come from the on-disk configuration file.
 *
 * Relation to the rest of the codebase: the app entry point runs this before constructing
 * `GamePreferences`/`WorldOptions` so CLI flags win over persisted settings.
 */

#include "voxels/app/command_line.hpp"

namespace voxels {

/// Parses the flags described in work item 08's CLI contract into `AppCommandLineOptions`.
/// Unknown or malformed flags are ignored so future flags can be added without breaking
/// older command lines.
class CliParser final : public ICommandLineParser {
public:
    [[nodiscard]] AppCommandLineOptions Parse(const std::vector<std::string>& args) const override;
};

} // namespace voxels
