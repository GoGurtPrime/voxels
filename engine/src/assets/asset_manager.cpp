/**
 * @file asset_manager.cpp
 * @brief Implementation of `.vpk` archive I/O, procedural fallback generators, and `AssetManager`.
 */

#include "voxels/assets/asset_manager.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <span>

namespace voxels {

namespace {

constexpr std::array<char, 4> kArchiveMagic = {'V', 'P', 'K', '1'};
constexpr std::size_t kHeaderSize = 36;

void AppendU16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void AppendU64(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] bool ReadU16(std::span<const std::byte> bytes, std::size_t& cursor, std::uint16_t& value) {
    if (cursor > bytes.size() || bytes.size() - cursor < 2) return false;
    value = static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[cursor])) |
            static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[cursor + 1])) << 8U;
    cursor += 2;
    return true;
}

[[nodiscard]] bool ReadU32(std::span<const std::byte> bytes, std::size_t& cursor, std::uint32_t& value) {
    if (cursor > bytes.size() || bytes.size() - cursor < 4) return false;
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[cursor++])) << shift;
    return true;
}

[[nodiscard]] bool ReadU64(std::span<const std::byte> bytes, std::size_t& cursor, std::uint64_t& value) {
    if (cursor > bytes.size() || bytes.size() - cursor < 8) return false;
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[cursor++])) << shift;
    return true;
}

[[nodiscard]] std::uint32_t Crc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes) {
        crc ^= std::to_integer<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
    }
    return ~crc;
}

[[nodiscard]] bool IsSafePath(const std::string& path) {
    const std::filesystem::path candidate(path);
    return !path.empty() && !candidate.is_absolute() && path.find("..") == std::string::npos && path.find('\\') == std::string::npos;
}

[[nodiscard]] std::vector<std::byte> ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open() || input.tellg() < 0) return {};
    const auto size = static_cast<std::size_t>(input.tellg());
    std::vector<std::byte> result(size);
    input.seekg(0);
    if (size != 0 && !input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size))) return {};
    return result;
}

} // namespace

bool VpkArchive::Write(const std::filesystem::path& archivePath, std::vector<AssetArchiveEntry> entries,
                       VpkBuildReport* report, std::string* error) {
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) { return left.name < right.name; });
    std::set<std::string> paths;
    for (const auto& entry : entries) {
        if (!IsSafePath(entry.name) || entry.name.size() > std::numeric_limits<std::uint16_t>::max() || !paths.insert(entry.name).second) {
            if (error != nullptr) *error = "archive entries must have unique safe relative paths";
            return false;
        }
    }
    std::vector<std::byte> bytes(kHeaderSize, std::byte{0});
    struct Blob { std::uint64_t offset; std::vector<std::byte> data; };
    std::unordered_map<std::uint32_t, std::vector<Blob>> blobsByHash;
    std::vector<std::uint64_t> offsets;
    offsets.reserve(entries.size());
    for (const auto& entry : entries) {
        while ((bytes.size() % 16U) != 0U) bytes.push_back(std::byte{0});
        const std::uint32_t hash = Crc32(entry.data);
        std::uint64_t offset = 0;
        bool duplicate = false;
        for (const Blob& blob : blobsByHash[hash]) {
            if (blob.data == entry.data) { offset = blob.offset; duplicate = true; break; }
        }
        if (!duplicate) {
            offset = bytes.size();
            bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
            blobsByHash[hash].push_back({offset, entry.data});
        }
        offsets.push_back(offset);
    }
    const std::uint64_t tocOffset = bytes.size();
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        AppendU16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        for (const char character : entry.name) bytes.push_back(static_cast<std::byte>(character));
        bytes.push_back(static_cast<std::byte>(entry.type));
        bytes.push_back(std::byte{0});
        AppendU64(bytes, offsets[index]);
        AppendU64(bytes, entry.data.size());
        AppendU64(bytes, entry.data.size());
        AppendU32(bytes, Crc32(entry.data));
    }
    const std::uint64_t tocSize = bytes.size() - tocOffset;
    for (std::size_t index = 0; index < kArchiveMagic.size(); ++index) {
        bytes[index] = static_cast<std::byte>(kArchiveMagic[index]);
    }
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{0};
    const auto writeU32At = [&bytes](std::size_t offset, std::uint32_t value) { for (int shift = 0; shift < 32; shift += 8) bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xFFU); };
    const auto writeU64At = [&bytes](std::size_t offset, std::uint64_t value) { for (int shift = 0; shift < 64; shift += 8) bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xFFU); };
    writeU32At(8, static_cast<std::uint32_t>(entries.size()));
    writeU64At(12, tocOffset);
    writeU64At(20, tocSize);
    writeU32At(28, 1);
    writeU32At(32, Crc32(std::span<const std::byte>(bytes.data(), 32)));
    std::error_code ec;
    if (archivePath.has_parent_path()) std::filesystem::create_directories(archivePath.parent_path(), ec);
    const std::filesystem::path temporary = archivePath.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) { if (error != nullptr) *error = "could not open archive output"; return false; }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    std::filesystem::rename(temporary, archivePath, ec);
    if (ec) { std::filesystem::remove(archivePath, ec); ec.clear(); std::filesystem::rename(temporary, archivePath, ec); }
    if (ec) { if (error != nullptr) *error = "could not atomically replace archive output"; return false; }
    if (report != nullptr) {
        report->entryCount = entries.size(); report->uniqueBlobCount = blobsByHash.size(); report->packedBytes = bytes.size();
        for (const auto& entry : entries) report->sourceBytes += entry.data.size();
        report->contentHash = Crc32(std::span<const std::byte>(bytes.data() + tocOffset, static_cast<std::size_t>(tocSize)));
    }
    return true;
}

std::optional<VpkArchive> VpkArchive::Open(const std::filesystem::path& archivePath, std::string* error) {
    VpkArchive archive;
    archive.m_bytes = ReadAll(archivePath);
    if (archive.m_bytes.size() < kHeaderSize) { if (error != nullptr) *error = "archive is truncated"; return std::nullopt; }
    std::span<const std::byte> bytes(archive.m_bytes);
    if (!std::equal(kArchiveMagic.begin(), kArchiveMagic.end(), reinterpret_cast<const char*>(bytes.data()))) { if (error != nullptr) *error = "archive has an invalid magic"; return std::nullopt; }
    std::size_t headerCursor = 4; std::uint16_t version = 0; std::uint16_t flags = 0; std::uint32_t count = 0; std::uint64_t tocOffset = 0; std::uint64_t tocSize = 0; std::uint32_t contentVersion = 0; std::uint32_t headerCrc = 0;
    if (!ReadU16(bytes, headerCursor, version) || !ReadU16(bytes, headerCursor, flags) || !ReadU32(bytes, headerCursor, count) || !ReadU64(bytes, headerCursor, tocOffset) || !ReadU64(bytes, headerCursor, tocSize) || !ReadU32(bytes, headerCursor, contentVersion) || !ReadU32(bytes, headerCursor, headerCrc) || version != kFormatVersion || flags != 0 || contentVersion == 0 || Crc32(bytes.first(32)) != headerCrc || tocOffset > bytes.size() || tocSize > bytes.size() - tocOffset) { if (error != nullptr) *error = "archive header is invalid"; return std::nullopt; }
    std::size_t cursor = static_cast<std::size_t>(tocOffset); const std::size_t tocEnd = cursor + static_cast<std::size_t>(tocSize);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint16_t length = 0; std::uint64_t offset = 0; std::uint64_t storedSize = 0; std::uint64_t originalSize = 0; std::uint32_t crc = 0;
        if (!ReadU16(bytes, cursor, length) || cursor > tocEnd || length > tocEnd - cursor) { if (error != nullptr) *error = "archive TOC is truncated"; return std::nullopt; }
        std::string path(reinterpret_cast<const char*>(bytes.data() + cursor), length); cursor += length;
        if (cursor > tocEnd || tocEnd - cursor < 2) { if (error != nullptr) *error = "archive TOC is truncated"; return std::nullopt; }
        const AssetType type = static_cast<AssetType>(std::to_integer<unsigned char>(bytes[cursor++])); const auto compression = std::to_integer<unsigned char>(bytes[cursor++]);
        if (!ReadU64(bytes, cursor, offset) || !ReadU64(bytes, cursor, storedSize) || !ReadU64(bytes, cursor, originalSize) || !ReadU32(bytes, cursor, crc) || compression != 0 || storedSize != originalSize || !IsSafePath(path) || offset > bytes.size() || storedSize > bytes.size() - offset || !archive.m_entries.emplace(std::move(path), Entry{type, offset, storedSize, originalSize, crc}).second) { if (error != nullptr) *error = "archive TOC contains an invalid entry"; return std::nullopt; }
    }
    if (cursor != tocEnd) { if (error != nullptr) *error = "archive TOC size is invalid"; return std::nullopt; }
    return archive;
}

std::optional<std::vector<std::byte>> VpkArchive::ReadEntry(const std::string& path, std::string* error) const {
    const auto iterator = m_entries.find(path);
    if (iterator == m_entries.end()) return std::nullopt;
    const Entry& entry = iterator->second;
    std::vector<std::byte> result(m_bytes.begin() + static_cast<std::ptrdiff_t>(entry.offset), m_bytes.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.storedSize));
    if (Crc32(result) != entry.crc32) { if (error != nullptr) *error = "archive entry checksum mismatch: " + path; return std::nullopt; }
    return result;
}

bool VpkArchive::Contains(const std::string& path) const noexcept { return m_entries.contains(path); }

std::vector<std::string> VpkArchive::Paths() const { std::vector<std::string> paths; paths.reserve(m_entries.size()); for (const auto& [path, entry] : m_entries) { static_cast<void>(entry); paths.push_back(path); } std::sort(paths.begin(), paths.end()); return paths; }

bool AssetArchive::WriteArchive(const std::filesystem::path& archivePath, const std::vector<AssetArchiveEntry>& entries) { return VpkArchive::Write(archivePath, entries); }

std::optional<std::vector<AssetArchiveEntry>> AssetArchive::ReadArchive(const std::filesystem::path& archivePath) {
    const auto archive = VpkArchive::Open(archivePath);
    if (!archive) return std::nullopt;
    std::vector<AssetArchiveEntry> entries;
    for (const std::string& path : archive->Paths()) { auto data = archive->ReadEntry(path); if (!data) return std::nullopt; entries.push_back({path, std::move(*data)}); }
    return entries;
}

ImageData GenerateSolidColorImage(int width, int height, std::array<std::uint8_t, 4> rgba) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.channels = 4;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    for (std::size_t pixel = 0; pixel < image.pixels.size() / 4; ++pixel) {
        image.pixels[pixel * 4 + 0] = rgba[0];
        image.pixels[pixel * 4 + 1] = rgba[1];
        image.pixels[pixel * 4 + 2] = rgba[2];
        image.pixels[pixel * 4 + 3] = rgba[3];
    }

    return image;
}

FontData GenerateFallbackAsciiFont() {
    FontData font;
    // Fully-lit glyph cells: a visible placeholder "tofu" block for every printable ASCII code
    // point, so UI text renders (as solid rectangles) even without a real TrueType asset.
    font.bitmap.assign(static_cast<std::size_t>(font.glyphCount) *
                            static_cast<std::size_t>(font.glyphWidth) *
                            static_cast<std::size_t>(font.glyphHeight),
                        std::uint8_t{255});
    return font;
}

AssetManager::AssetManager(std::filesystem::path assetRoot) : m_assetRoot(std::move(assetRoot)) {
}

bool AssetManager::MountArchive(const std::filesystem::path& archivePath) {
    std::string error;
    const std::optional<VpkArchive> archive = VpkArchive::Open(archivePath, &error);
    if (!archive) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const std::string& path : archive->Paths()) {
        const auto data = archive->ReadEntry(path, &error);
        if (!data) return false;
        m_mountedArchiveEntries[path] = *data;
    }
    return true;
}

bool AssetManager::MountArchives(const std::filesystem::path& packsDirectory, std::string* error) {
    std::error_code filesystemError;
    if (!std::filesystem::exists(packsDirectory, filesystemError)) return true;
    std::vector<std::filesystem::path> archives;
    for (const auto& entry : std::filesystem::directory_iterator(packsDirectory, filesystemError)) {
        if (filesystemError) break;
        if (entry.is_regular_file() && entry.path().extension() == ".vpk") archives.push_back(entry.path());
    }
    std::sort(archives.begin(), archives.end());
    for (const auto& archive : archives) {
        if (!MountArchive(archive)) {
            if (error != nullptr) *error = "could not mount asset pack: " + archive.string();
            return false;
        }
    }
    return true;
}

std::vector<std::byte> AssetManager::ResolvePayload(const std::string& relativePath, AssetType type) const {
    const std::filesystem::path fullPath = m_assetRoot.empty() ? std::filesystem::path(relativePath)
                                                                : m_assetRoot / relativePath;
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        const std::streamsize size = file.tellg();
        if (size >= 0) {
            file.seekg(0, std::ios::beg);
            std::vector<std::byte> data(static_cast<std::size_t>(size));
            if ((size == 0) || file.read(reinterpret_cast<char*>(data.data()), size)) return data;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto archiveIt = m_mountedArchiveEntries.find(relativePath);
        if (archiveIt != m_mountedArchiveEntries.end()) {
            return archiveIt->second;
        }
    }

    // Procedural fallback: synthesize a minimal placeholder payload appropriate to the asset type.
    switch (type) {
        case AssetType::Texture: {
            const ImageData image = GenerateSolidColorImage(16, 16, {255, 0, 255, 255});
            std::vector<std::byte> data(image.pixels.size());
            std::memcpy(data.data(), image.pixels.data(), image.pixels.size());
            return data;
        }
        case AssetType::Font: {
            const FontData font = GenerateFallbackAsciiFont();
            std::vector<std::byte> data(font.bitmap.size());
            std::memcpy(data.data(), font.bitmap.data(), font.bitmap.size());
            return data;
        }
        case AssetType::Sound:
        case AssetType::Model:
        case AssetType::Shader:
        default:
            return {};
    }
}

std::future<AssetHandle> AssetManager::LoadAsync(const std::string& relativePath, AssetType type) {
    return std::async(std::launch::async, [this, relativePath, type]() -> AssetHandle {
        const std::string cacheKey = std::to_string(static_cast<int>(type)) + ":" + relativePath;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto cached = m_cachedHandles.find(cacheKey);
            if (cached != m_cachedHandles.end()) return cached->second;
        }

        std::vector<std::byte> payload = ResolvePayload(relativePath, type);

        std::lock_guard<std::mutex> lock(m_mutex);
        const auto cached = m_cachedHandles.find(cacheKey);
        if (cached != m_cachedHandles.end()) return cached->second;
        const std::uint32_t id = m_nextHandleId++;
        m_loadedAssets.emplace(id, std::move(payload));
        const AssetHandle handle{id, type};
        m_cachedHandles.emplace(cacheKey, handle);
        return handle;
    });
}

bool AssetManager::IsLoaded(AssetHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return handle.IsValid() && m_loadedAssets.contains(handle.id);
}

std::optional<std::vector<std::byte>> AssetManager::GetData(AssetHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_loadedAssets.find(handle.id);
    if (it == m_loadedAssets.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace voxels
