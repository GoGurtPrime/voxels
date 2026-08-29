#pragma once

/**
 * @file asset_manager.hpp
 * @brief Asynchronous asset loading, resource handles, and packed archive (.vpk) I/O.
 *
 * @details Declares `AssetManager`, which loads raw asset payloads (textures, shaders, sounds,
 *          models, fonts) either from loose files on disk or from a mounted single-file `.vpk`
 *          archive (see `AssetArchive`), off the calling thread via `std::async`. Resource
 *          handles are stable, cheap-to-copy identifiers returned immediately by `LoadAsync`
 *          while the payload streams in on a worker thread.
 *
 *          When a requested asset cannot be found (loose file missing and not present in any
 *          mounted archive), the manager falls back to a procedurally generated placeholder
 *          (solid color texture, embedded ASCII bitmap font) rather than failing outright, per
 *          the project's human-in-the-loop procedural fallback policy.
 *
 *          Relation to the rest of the codebase: the app layer and editor tool both depend on
 *          this module to stream content; `voxels/audio/audio_engine.hpp` independently owns
 *          decoding of the raw sound bytes this module retrieves.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace voxels {

enum class AssetType {
    Texture,
    Shader,
    Sound,
    Model,
    Font
};

/// Opaque handle to an asset payload managed by `AssetManager`. `id == 0` is invalid/unset.
struct AssetHandle {
    std::uint32_t id = 0;
    AssetType type = AssetType::Texture;

    [[nodiscard]] bool IsValid() const noexcept { return id != 0; }
    [[nodiscard]] bool operator==(const AssetHandle&) const noexcept = default;
};

/// A single named payload stored in (or extracted from) a `.vpk` archive.
struct AssetArchiveEntry {
    std::string name;
    std::vector<std::byte> data;
};

/// Reads/writes the engine's packed single-file asset archive format ("VPK1"):
/// `[magic:4][entryCount:u32] { [nameLen:u32][name][dataLen:u64][data] }*`.
class AssetArchive {
public:
    [[nodiscard]] static bool WriteArchive(const std::filesystem::path& archivePath,
                                            const std::vector<AssetArchiveEntry>& entries);
    [[nodiscard]] static std::optional<std::vector<AssetArchiveEntry>> ReadArchive(
        const std::filesystem::path& archivePath);
};

/// A decoded (or procedurally generated) RGBA8 image buffer.
struct ImageData {
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<std::uint8_t> pixels;
};

/// A fixed-size bitmap glyph grid covering printable ASCII (0x20..0x7E), used as the fallback
/// font when no TrueType asset is available.
struct FontData {
    int glyphWidth = 8;
    int glyphHeight = 8;
    int firstGlyph = 0x20;
    int glyphCount = 0x5F;
    std::vector<std::uint8_t> bitmap; // glyphCount * glyphWidth * glyphHeight, 1 byte per pixel (0 or 255)
};

/// Procedural fallback texture: a solid-color RGBA8 image of the requested size.
[[nodiscard]] ImageData GenerateSolidColorImage(int width, int height, std::array<std::uint8_t, 4> rgba);

/// Procedural fallback font: a minimal embedded 8x8 bitmap covering printable ASCII, where every
/// glyph cell is fully lit (a "missing glyph" block) so text remains visible without a real font.
[[nodiscard]] FontData GenerateFallbackAsciiFont();

/// Loads asset payloads asynchronously from loose files or mounted `.vpk` archives, caching
/// results behind stable `AssetHandle`s. Thread-safe.
class AssetManager {
public:
    explicit AssetManager(std::filesystem::path assetRoot = {});

    /// Mounts a `.vpk` archive so subsequent `LoadAsync` calls can resolve entries by name
    /// before falling back to loose files on disk. Returns false if the archive can't be read.
    bool MountArchive(const std::filesystem::path& archivePath);

    /// Kicks off an asynchronous load of `relativePath` and returns a future resolving to the
    /// handle once the payload (or its procedural fallback) is cached. Never fails/throws for
    /// missing assets; the fallback path is used instead.
    [[nodiscard]] std::future<AssetHandle> LoadAsync(const std::string& relativePath, AssetType type);

    [[nodiscard]] bool IsLoaded(AssetHandle handle) const;
    [[nodiscard]] std::optional<std::vector<std::byte>> GetData(AssetHandle handle) const;

private:
    [[nodiscard]] std::vector<std::byte> ResolvePayload(const std::string& relativePath, AssetType type) const;

    std::filesystem::path m_assetRoot;
    std::unordered_map<std::string, std::vector<std::byte>> m_mountedArchiveEntries;

    mutable std::mutex m_mutex;
    std::uint32_t m_nextHandleId = 1;
    std::unordered_map<std::uint32_t, std::vector<std::byte>> m_loadedAssets;
};

} // namespace voxels
