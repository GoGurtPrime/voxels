/**
 * @file world_serialization.cpp
 * @brief File-backed world and player persistence for runtime saves.
 *
 * @details Serializes only dirty chunks into a simple directory structure and stores the player
 *          inventory/transform as plain text so a save can be read by hand and reloaded lazily.
 */

#include "voxels/world/world_serialization.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace voxels {
namespace {

constexpr std::uint32_t kSectorSize = 4096;
constexpr std::uint32_t kRegionWidth = 32;
constexpr std::uint32_t kRegionEntryCount = kRegionWidth * kRegionWidth;
constexpr std::uint32_t kRegionVersion = 1;
constexpr std::uint32_t kPlayerVersion = 1;

struct RegionEntry { std::uint32_t offset = 0; std::uint32_t sectors = 0; std::uint32_t timestamp = 0; std::uint32_t crc = 0; };

template <typename T> void Append(std::vector<std::uint8_t>& bytes, const T& value) {
    const auto* source = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), source, source + sizeof(T));
}

template <typename T> bool Read(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, T& value) {
    if (cursor + sizeof(T) > bytes.size()) return false;
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

std::uint32_t Crc32(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool AtomicWrite(const std::filesystem::path& target, const std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) return false;
    const std::filesystem::path temporary = target.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) return false;
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) return false;
    }
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
#else
    std::filesystem::rename(temporary, target, error);
    if (error) { std::filesystem::remove(temporary, error); return false; }
#endif
    return true;
}

int FloorDivide(int value, int divisor) { return value >= 0 ? value / divisor : -(((-value) + divisor - 1) / divisor); }
std::filesystem::path RegionDirectory(const std::filesystem::path& saveRoot) { return saveRoot / "regions"; }
std::filesystem::path RegionPath(const std::filesystem::path& root, int regionX, int regionZ) {
    return RegionDirectory(root) / ("r." + std::to_string(regionX) + "." + std::to_string(regionZ) + ".vrg");
}

using ChunkMap = std::unordered_map<ChunkCoordinate, Chunk, ChunkCoordinateHash>;

bool ReadRegion(const std::filesystem::path& path, ChunkMap& chunks) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return !std::filesystem::exists(path);
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    std::size_t cursor = 0;
    std::array<char, 4> magic{}; std::uint32_t version = 0; std::uint32_t entries = 0;
    if (!Read(bytes, cursor, magic) || magic != std::array<char, 4>{'V', 'R', 'G', '1'} || !Read(bytes, cursor, version) ||
        !Read(bytes, cursor, entries) || version > kRegionVersion || entries != kRegionEntryCount) return false;
    std::vector<RegionEntry> table(entries);
    for (RegionEntry& entry : table) if (!Read(bytes, cursor, entry)) return false;
    for (const RegionEntry& entry : table) {
        const std::size_t offset = static_cast<std::size_t>(entry.offset) * kSectorSize;
        const std::size_t length = static_cast<std::size_t>(entry.sectors) * kSectorSize;
        if (entry.sectors == 0 || offset + length > bytes.size()) continue;
        std::vector<std::uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        if (Crc32(payload) != entry.crc) continue;
        std::size_t payloadCursor = 0; std::uint32_t sectionCount = 0;
        if (!Read(payload, payloadCursor, sectionCount)) continue;
        for (std::uint32_t section = 0; section < sectionCount; ++section) {
            ChunkCoordinate coordinate{}; std::uint32_t rleSize = 0;
            if (!Read(payload, payloadCursor, coordinate) || !Read(payload, payloadCursor, rleSize) || payloadCursor + rleSize > payload.size()) break;
            std::vector<std::uint8_t> rle(payload.begin() + static_cast<std::ptrdiff_t>(payloadCursor), payload.begin() + static_cast<std::ptrdiff_t>(payloadCursor + rleSize));
            payloadCursor += rleSize;
            Chunk chunk = Chunk::DeserializeRLE(rle, coordinate);
            chunk.ClearDirty();
            chunks.insert_or_assign(coordinate, std::move(chunk));
        }
    }
    return true;
}

bool WriteRegion(const std::filesystem::path& path, const ChunkMap& chunks) {
    std::array<std::vector<Chunk>, kRegionEntryCount> columns;
    for (const auto& [coordinate, chunk] : chunks) {
        const int localX = ((coordinate.x % static_cast<int>(kRegionWidth)) + static_cast<int>(kRegionWidth)) % static_cast<int>(kRegionWidth);
        const int localZ = ((coordinate.z % static_cast<int>(kRegionWidth)) + static_cast<int>(kRegionWidth)) % static_cast<int>(kRegionWidth);
        columns[static_cast<std::size_t>(localZ * static_cast<int>(kRegionWidth) + localX)].push_back(chunk);
    }
    const std::size_t headerBytes = 12 + sizeof(RegionEntry) * kRegionEntryCount;
    const std::size_t headerSectors = (headerBytes + kSectorSize - 1) / kSectorSize;
    std::vector<RegionEntry> table(kRegionEntryCount);
    std::vector<std::vector<std::uint8_t>> payloads(kRegionEntryCount);
    std::uint32_t nextSector = static_cast<std::uint32_t>(headerSectors);
    const auto timestamp = static_cast<std::uint32_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (columns[index].empty()) continue;
        auto& payload = payloads[index];
        const std::uint32_t count = static_cast<std::uint32_t>(columns[index].size()); Append(payload, count);
        for (const Chunk& chunk : columns[index]) {
            const auto rle = chunk.SerializeRLE(); Append(payload, chunk.GetCoordinate());
            const std::uint32_t rleSize = static_cast<std::uint32_t>(rle.size()); Append(payload, rleSize);
            payload.insert(payload.end(), rle.begin(), rle.end());
        }
        const std::uint32_t sectors = static_cast<std::uint32_t>((payload.size() + kSectorSize - 1) / kSectorSize);
        payload.resize(static_cast<std::size_t>(sectors) * kSectorSize);
        table[index] = {nextSector, sectors, timestamp, Crc32(payload)};
        nextSector += sectors;
    }
    std::vector<std::uint8_t> output(static_cast<std::size_t>(headerSectors) * kSectorSize, 0);
    const std::array<char, 4> magic{'V', 'R', 'G', '1'}; Append(output, magic);
    // The preallocated header is rebuilt below to keep sector offsets deterministic.
    output.clear(); Append(output, magic); Append(output, kRegionVersion); Append(output, kRegionEntryCount);
    for (const RegionEntry& entry : table) Append(output, entry);
    output.resize(static_cast<std::size_t>(headerSectors) * kSectorSize, 0);
    for (const auto& payload : payloads) output.insert(output.end(), payload.begin(), payload.end());
    return AtomicWrite(path, output);
}

} // namespace

bool SaveWorld(const World& world, const std::filesystem::path& saveRoot) {
    std::map<std::pair<int, int>, ChunkMap> regions;
    for (const auto& [coordinate, chunk] : world.GetChunks()) {
        if (!chunk->IsDirty()) continue;
        const std::pair key{FloorDivide(coordinate.x, static_cast<int>(kRegionWidth)), FloorDivide(coordinate.z, static_cast<int>(kRegionWidth))};
        auto& region = regions[key];
        const auto path = RegionPath(saveRoot, key.first, key.second);
        if (region.empty() && !ReadRegion(path, region)) return false;
        region.insert_or_assign(coordinate, *chunk);
    }
    for (auto& [key, region] : regions) {
        if (!WriteRegion(RegionPath(saveRoot, key.first, key.second), region)) return false;
    }
    for (const auto& [coordinate, chunk] : world.GetChunks()) if (chunk->IsDirty()) chunk->ClearDirty();
    return true;
}

bool LoadWorld(World& world, const std::filesystem::path& saveRoot) {
    std::error_code ec;
    const auto regionDir = RegionDirectory(saveRoot);
    if (!std::filesystem::exists(regionDir, ec)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(regionDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".vrg") {
            continue;
        }
        ChunkMap chunks;
        if (!ReadRegion(entry.path(), chunks)) continue;
        for (auto& [coordinate, chunk] : chunks) world.GetOrCreateChunk(coordinate) = std::move(chunk);
    }
    return true;
}

bool SavePlayerState(const std::filesystem::path& playerFile, const PlayerState& state) {
    std::vector<std::uint8_t> bytes;
    const std::array<char, 4> magic{'P', 'L', 'R', '1'}; Append(bytes, magic); Append(bytes, kPlayerVersion);
    Append(bytes, state.position); Append(bytes, state.velocity); Append(bytes, state.yaw); Append(bytes, state.pitch);
    Append(bytes, state.onGround); Append(bytes, state.health);
    const std::int32_t selectedSlot = state.inventory.GetSelectedSlot(); Append(bytes, selectedSlot);
    const std::uint32_t count = static_cast<std::uint32_t>(state.inventory.Slots().size()); Append(bytes, count);
    for (const auto& stack : state.inventory.Slots()) { Append(bytes, stack.blockId); Append(bytes, stack.count); }
    return AtomicWrite(playerFile, bytes);
}

bool LoadPlayerState(const std::filesystem::path& playerFile, PlayerState& outState) {
    std::ifstream input(playerFile, std::ios::binary);
    if (!input.is_open()) return false;
    const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    std::size_t cursor = 0; std::array<char, 4> magic{}; std::uint32_t version = 0;
    if (!Read(bytes, cursor, magic) || magic != std::array<char, 4>{'P', 'L', 'R', '1'} || !Read(bytes, cursor, version) || version > kPlayerVersion) return false;
    PlayerState result{}; std::int32_t selectedSlot = 0; std::uint32_t count = 0;
    if (!Read(bytes, cursor, result.position) || !Read(bytes, cursor, result.velocity) || !Read(bytes, cursor, result.yaw) ||
        !Read(bytes, cursor, result.pitch) || !Read(bytes, cursor, result.onGround) || !Read(bytes, cursor, result.health) ||
        !Read(bytes, cursor, selectedSlot) || !Read(bytes, cursor, count) || count != result.inventory.Slots().size()) return false;
    for (std::size_t index = 0; index < count; ++index) {
        gameplay::ItemStack stack{};
        if (!Read(bytes, cursor, stack.blockId) || !Read(bytes, cursor, stack.count)) return false;
        result.inventory.GetSlot(index) = stack;
    }
    result.inventory.SetSelectedSlot(selectedSlot);
    outState = result;
    return true;
}

} // namespace voxels
