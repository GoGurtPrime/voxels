/**
 * @file asset_manager.cpp
 * @brief Implementation of `.vpk` archive I/O, procedural fallback generators, and `AssetManager`.
 */

#include "voxels/assets/asset_manager.hpp"

#include <cstring>
#include <fstream>

namespace voxels {

namespace {

constexpr char kArchiveMagic[4] = {'V', 'P', 'K', '1'};

template <typename T>
void WritePod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool ReadPod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

} // namespace

bool AssetArchive::WriteArchive(const std::filesystem::path& archivePath,
                                 const std::vector<AssetArchiveEntry>& entries) {
    if (archivePath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(archivePath.parent_path(), ec);
    }

    std::ofstream out(archivePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out.write(kArchiveMagic, sizeof(kArchiveMagic));
    WritePod(out, static_cast<std::uint32_t>(entries.size()));

    for (const AssetArchiveEntry& entry : entries) {
        WritePod(out, static_cast<std::uint32_t>(entry.name.size()));
        out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        WritePod(out, static_cast<std::uint64_t>(entry.data.size()));
        if (!entry.data.empty()) {
            out.write(reinterpret_cast<const char*>(entry.data.data()),
                       static_cast<std::streamsize>(entry.data.size()));
        }
    }

    return out.good();
}

std::optional<std::vector<AssetArchiveEntry>> AssetArchive::ReadArchive(const std::filesystem::path& archivePath) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    char magic[4] = {};
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, kArchiveMagic, sizeof(kArchiveMagic)) != 0) {
        return std::nullopt;
    }

    std::uint32_t entryCount = 0;
    if (!ReadPod(in, entryCount)) {
        return std::nullopt;
    }

    std::vector<AssetArchiveEntry> entries;
    entries.reserve(entryCount);

    for (std::uint32_t i = 0; i < entryCount; ++i) {
        std::uint32_t nameLen = 0;
        if (!ReadPod(in, nameLen)) {
            return std::nullopt;
        }

        AssetArchiveEntry entry;
        entry.name.resize(nameLen);
        if (nameLen > 0 && !in.read(entry.name.data(), nameLen)) {
            return std::nullopt;
        }

        std::uint64_t dataLen = 0;
        if (!ReadPod(in, dataLen)) {
            return std::nullopt;
        }

        entry.data.resize(static_cast<std::size_t>(dataLen));
        if (dataLen > 0 && !in.read(reinterpret_cast<char*>(entry.data.data()), static_cast<std::streamsize>(dataLen))) {
            return std::nullopt;
        }

        entries.push_back(std::move(entry));
    }

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
    std::optional<std::vector<AssetArchiveEntry>> entries = AssetArchive::ReadArchive(archivePath);
    if (!entries) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (AssetArchiveEntry& entry : *entries) {
        m_mountedArchiveEntries[entry.name] = std::move(entry.data);
    }
    return true;
}

std::vector<std::byte> AssetManager::ResolvePayload(const std::string& relativePath, AssetType type) const {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto archiveIt = m_mountedArchiveEntries.find(relativePath);
        if (archiveIt != m_mountedArchiveEntries.end()) {
            return archiveIt->second;
        }
    }

    const std::filesystem::path fullPath = m_assetRoot.empty() ? std::filesystem::path(relativePath)
                                                                : m_assetRoot / relativePath;
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        const std::streamsize size = file.tellg();
        if (size > 0) {
            file.seekg(0, std::ios::beg);
            std::vector<std::byte> data(static_cast<std::size_t>(size));
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                return data;
            }
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
        std::vector<std::byte> payload = ResolvePayload(relativePath, type);

        std::lock_guard<std::mutex> lock(m_mutex);
        const std::uint32_t id = m_nextHandleId++;
        m_loadedAssets.emplace(id, std::move(payload));
        return AssetHandle{id, type};
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
