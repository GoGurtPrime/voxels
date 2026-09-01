#pragma once

/**
 * @file cli_parser.hpp
 * @brief Concrete `--key=value` command-line parser for app startup.
 *
 * @details Recognizes display flags (`--fullscreen`, `--resolution=<WxH>`, `--vsync`,
 *          `--headless`), networking flags (`--server`, `--port`, `--join=<HOST:PORT>`),
 *          world flags (`--world`, `--seed`, `--render-distance`, `--max-ticks`,
 *          `--max-frames`), and asset/tooling flags (`--dump-atlas`,
 *          `--forge-interaction-assets`, `--forge-audio-assets`, `--gen-preview`, `--out`).
 *          The app entry point runs this before constructing `GamePreferences`/
 *          `WorldOptions`, so CLI flags win over persisted settings.
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
