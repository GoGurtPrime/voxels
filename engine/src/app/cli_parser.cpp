/**
 * @file cli_parser.cpp
 * @brief Implementation of `CliParser::Parse`.
 *
 * @details Splits each `--key=value` argument on the first `=`, and dispatches on the key
 *          name. Boolean-only flags (`--server`) require no value. Malformed numeric values
 *          are ignored rather than throwing, so a bad flag never crashes application boot.
 */

#include "voxels/app/cli_parser.hpp"

#include <charconv>
#include <string_view>

namespace voxels {

namespace {

bool ParseBoolValue(std::string_view value, bool fallback) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return fallback;
}

bool ParseIntValue(std::string_view value, int& outValue) {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), outValue);
    return result.ec == std::errc{};
}

bool ParseU32Value(std::string_view value, std::uint32_t& outValue) {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), outValue);
    return result.ec == std::errc{};
}

bool ParsePort(std::string_view value, std::uint16_t& outValue) {
    unsigned int port = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
    if (result.ec != std::errc{} || port == 0 || port > 65535) return false;
    outValue = static_cast<std::uint16_t>(port);
    return true;
}

bool ParseResolution(std::string_view value, int& width, int& height) {
    const auto sep = value.find('x');
    if (sep == std::string_view::npos) {
        return false;
    }
    int parsedWidth = 0;
    int parsedHeight = 0;
    if (!ParseIntValue(value.substr(0, sep), parsedWidth) ||
        !ParseIntValue(value.substr(sep + 1), parsedHeight)) {
        return false;
    }
    width = parsedWidth;
    height = parsedHeight;
    return true;
}

} // namespace

AppCommandLineOptions CliParser::Parse(const std::vector<std::string>& args) const {
    AppCommandLineOptions options{};

    for (const std::string& arg : args) {
        std::string_view view(arg);
        if (view.rfind("--", 0) != 0) {
            continue;
        }
        view.remove_prefix(2);

        const auto eq = view.find('=');
        const std::string_view key = (eq == std::string_view::npos) ? view : view.substr(0, eq);
        const std::string_view value = (eq == std::string_view::npos) ? std::string_view{} : view.substr(eq + 1);

        if (key == "fullscreen") {
            options.fullscreenOverride = true;
            options.fullscreenValue = ParseBoolValue(value, options.fullscreenValue);
        } else if (key == "resolution") {
            int width = 0;
            int height = 0;
            if (ParseResolution(value, width, height)) {
                options.resolutionOverride = true;
                options.resolutionWidth = width;
                options.resolutionHeight = height;
            }
        } else if (key == "vsync") {
            options.vsyncOverride = true;
            options.vsyncValue = ParseBoolValue(value, options.vsyncValue);
        } else if (key == "headless") {
            options.headless = true;
        } else if (key == "server") {
            options.serverMode = true;
        } else if (key == "port") {
            std::uint16_t port = 0;
            if (ParsePort(value, port)) {
                options.serverPortOverride = true;
                options.serverPort = port;
            }
        } else if (key == "join" && !value.empty()) {
            options.joinEndpointOverride = true;
            options.joinEndpoint = std::string(value);
        } else if (key == "world") {
            options.worldNameOverride = true;
            options.worldName = std::string(value);
        } else if (key == "seed") {
            std::uint32_t seed = 0;
            if (ParseU32Value(value, seed)) {
                options.seedOverride = true;
                options.seed = seed;
            }
        } else if (key == "render-distance") {
            int renderDistance = 0;
            if (ParseIntValue(value, renderDistance)) {
                options.renderDistanceOverride = true;
                options.renderDistance = renderDistance;
            }
        } else if (key == "max-ticks") {
            int maxTicks = 0;
            if (ParseIntValue(value, maxTicks) && maxTicks > 0) {
                options.maxTicksOverride = true;
                options.maxTicks = maxTicks;
            }
        } else if (key == "max-frames") {
            int maxFrames = 0;
            if (ParseIntValue(value, maxFrames) && maxFrames > 0) {
                options.maxFrames = maxFrames;
            }
        } else if (key == "dump-atlas") {
            options.dumpAtlas = true;
            if (!value.empty()) {
                options.dumpAtlasPath = std::string(value);
            }
        } else if (key == "forge-interaction-assets") {
            options.forgeInteractionAssets = true;
            if (!value.empty()) {
                options.forgeInteractionAssetsPath = std::string(value);
            }
        } else if (key == "forge-audio-assets") {
            options.forgeAudioAssets = true;
            if (!value.empty()) options.forgeAudioAssetsPath = std::string(value);
        } else if (key == "gen-preview") {
            options.genPreview = true;
        } else if (key == "out") {
            options.genPreviewPath = std::string(value);
        }
    }

    return options;
}

} // namespace voxels
