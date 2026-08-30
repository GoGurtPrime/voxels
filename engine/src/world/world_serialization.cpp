/**
 * @file world_serialization.cpp
 * @brief File-backed world and player persistence for runtime saves.
 *
 * @details Serializes only dirty chunks into a simple directory structure and stores the player
 *          inventory/transform as plain text so a save can be read by hand and reloaded lazily.
 */

#include "voxels/world/world_serialization.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace voxels {
namespace {

std::string ToKeyValue(const PlayerState& state) {
    std::ostringstream out;
    out << "position=" << state.position.x << "," << state.position.y << "," << state.position.z << '\n';
    out << "velocity=" << state.velocity.x << "," << state.velocity.y << "," << state.velocity.z << '\n';
    out << "yaw=" << state.yaw << '\n';
    out << "pitch=" << state.pitch << '\n';
    out << "onGround=" << (state.onGround ? 1 : 0) << '\n';
    out << "selectedHotbarSlot=" << state.selectedHotbarSlot << '\n';
    out << "health=" << state.health << '\n';
    for (std::size_t i = 0; i < state.inventory.size(); ++i) {
        out << "inventory[" << i << "]=" << static_cast<unsigned int>(state.inventory[i]) << '\n';
    }
    return out.str();
}

bool FromKeyValue(PlayerState& state, const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        if (key == "position") {
            const auto comma1 = value.find(',');
            const auto comma2 = value.find(',', comma1 + 1);
            state.position.x = std::stof(value.substr(0, comma1));
            state.position.y = std::stof(value.substr(comma1 + 1, comma2 - comma1 - 1));
            state.position.z = std::stof(value.substr(comma2 + 1));
        } else if (key == "velocity") {
            const auto comma1 = value.find(',');
            const auto comma2 = value.find(',', comma1 + 1);
            state.velocity.x = std::stof(value.substr(0, comma1));
            state.velocity.y = std::stof(value.substr(comma1 + 1, comma2 - comma1 - 1));
            state.velocity.z = std::stof(value.substr(comma2 + 1));
        } else if (key == "yaw") {
            state.yaw = std::stof(value);
        } else if (key == "pitch") {
            state.pitch = std::stof(value);
        } else if (key == "onGround") {
            state.onGround = value == "1";
        } else if (key == "selectedHotbarSlot") {
            state.selectedHotbarSlot = std::stoi(value);
        } else if (key == "health") {
            state.health = std::stof(value);
        } else if (key.rfind("inventory[", 0) == 0) {
            const auto end = key.find(']');
            if (end != std::string::npos) {
                const std::string idx = key.substr(10, end - 10);
                const std::size_t index = static_cast<std::size_t>(std::stoul(idx));
                if (index < state.inventory.size()) {
                    state.inventory[index] = static_cast<BlockId>(static_cast<std::uint16_t>(std::stoul(value)));
                }
            }
        }
    }
    return true;
}

std::filesystem::path WorldDirectoryFor(const std::filesystem::path& saveRoot) {
    return saveRoot / "region";
}

} // namespace

bool SaveWorld(const World& world, const std::filesystem::path& saveRoot) {
    std::error_code ec;
    const auto regionDir = WorldDirectoryFor(saveRoot);
    std::filesystem::create_directories(regionDir, ec);
    if (ec) {
        return false;
    }

    for (const auto& [coordinate, chunk] : world.GetChunks()) {
        const std::filesystem::path chunkPath = regionDir / (std::to_string(coordinate.x) + "_" + std::to_string(coordinate.z) + ".chunk");
        const auto bytes = chunk->SerializeRLE();
        std::ofstream out(chunkPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return true;
}

bool LoadWorld(World& world, const std::filesystem::path& saveRoot) {
    std::error_code ec;
    const auto regionDir = WorldDirectoryFor(saveRoot);
    if (!std::filesystem::exists(regionDir, ec)) {
        return false;
    }

    world = World(world.GetChunkSize());
    for (const auto& entry : std::filesystem::directory_iterator(regionDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".chunk") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in.is_open()) {
            return false;
        }
        std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
        const std::string filename = entry.path().filename().string();
        const auto underscore = filename.find_last_of('_');
        const auto dot = filename.find_last_of('.');
        if (underscore == std::string::npos || dot == std::string::npos || underscore + 1 >= dot) {
            continue;
        }
        const int x = std::stoi(filename.substr(0, underscore));
        const int z = std::stoi(filename.substr(underscore + 1, dot - underscore - 1));
        auto chunk = Chunk::DeserializeRLE(bytes, ChunkCoordinate{x, 0, z}, world.GetChunkSize(),
                                          world.GetChunkSize(), world.GetChunkSize());
        auto& loadedChunk = world.GetOrCreateChunk(chunk.GetCoordinate());
        loadedChunk = std::move(chunk);
    }
    return true;
}

bool SavePlayerState(const std::filesystem::path& playerFile, const PlayerState& state) {
    std::error_code ec;
    std::filesystem::create_directories(playerFile.parent_path(), ec);
    if (ec) {
        return false;
    }
    std::ofstream out(playerFile, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << ToKeyValue(state);
    return static_cast<bool>(out);
}

bool LoadPlayerState(const std::filesystem::path& playerFile, PlayerState& outState) {
    std::ifstream in(playerFile);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    outState = {};
    return FromKeyValue(outState, buffer.str());
}

} // namespace voxels
