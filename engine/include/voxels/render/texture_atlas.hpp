#pragma once

/**
 * @file texture_atlas.hpp
 * @brief GL 2D array texture atlas, deterministic layer assignment, and atlas dump utility.
 *
 * @details Implements a data-driven texture atlas backed by an OpenGL 2D array texture
 *          (GL_TEXTURE_2D_ARRAY), eliminating mip bleeding across block tiles. Supports
 *          procedural fallbacks for missing assets, deterministic layer sorting across runs,
 *          and debug PNG atlas export.
 *          Reference ARCHITECTURE.md §6.6 and work_items/04_block_definitions_textures_and_atlas.md.
 */

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glad/glad.h>

#include "voxels/assets/asset_manager.hpp"
#include "voxels/world/block.hpp"

namespace voxels {

/// Name -> array-layer block texture atlas. Layer 0 is always the missing-texture checker and
/// layer order is deterministic across runs. The GL sampler wraps with GL_REPEAT because greedy
/// chunk UVs exceed [0,1] (the chunk shader applies fract); GL calls are null-guarded (headless).
class TextureAtlas {
public:
    /// Reserved name of layer 0, the magenta/black checker returned for unregistered lookups.
    static constexpr const char* kMissingTextureKey = "__missing__";

    explicit TextureAtlas(int tileWidth = 16, int tileHeight = 16);
    ~TextureAtlas();

    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;
    TextureAtlas(TextureAtlas&& other) noexcept;
    TextureAtlas& operator=(TextureAtlas&& other) noexcept;

    /// Registers an image under the given texture identifier.
    void RegisterTexture(std::string_view name, const ImageData& image);

    /// Registers a texture from a PNG file on disk. Returns false if file cannot be loaded.
    bool RegisterTextureFromFile(std::string_view name, const std::filesystem::path& filePath);

    /// Scans a block registry, loading all referenced textures from loose files under texturesRoot
    /// or falling back to procedural generation.
    void PopulateFromBlockRegistry(
        const BlockRegistry& registry,
        const std::filesystem::path& texturesRoot);

    /// Compiles all registered layers into a deterministic sequence and uploads to a GL_TEXTURE_2D_ARRAY.
    /// Returns true on success (or true in headless environment without GL).
    bool BuildGLTexture();

    /// Releases the GPU texture object.
    void Shutdown();

    /// Returns the layer index in the 2D array texture for a given texture name.
    /// If texture is not registered, returns 0 (the missing-texture checker layer) and logs a warning.
    [[nodiscard]] int LayerFor(std::string_view textureName) const;

    /// Returns true if the exact texture name is registered in the atlas.
    [[nodiscard]] bool HasTexture(std::string_view textureName) const;

    /// Returns the OpenGL texture handle (0 if not built).
    [[nodiscard]] GLuint GetTextureHandle() const noexcept { return m_textureHandle; }

    /// Returns total number of layers (including layer 0 __missing__).
    [[nodiscard]] int GetLayerCount() const noexcept { return static_cast<int>(m_layers.size()); }

    [[nodiscard]] int GetTileWidth() const noexcept { return m_tileWidth; }
    [[nodiscard]] int GetTileHeight() const noexcept { return m_tileHeight; }

    /// Returns the layer ImageData for a given layer index.
    [[nodiscard]] const ImageData* GetLayerImage(int layerIndex) const;

    /// Stitches all layers into a combined 2D image and saves it to outputPath as a PNG file.
    [[nodiscard]] bool DumpAtlasToPng(const std::filesystem::path& outputPath) const;

private:
    [[nodiscard]] static std::string NormalizeTextureName(std::string_view name);

    int m_tileWidth = 16;
    int m_tileHeight = 16;
    GLuint m_textureHandle = 0;
    bool m_isBuilt = false;

    // Ordered map of name -> ImageData to ensure deterministic iteration order
    std::map<std::string, ImageData> m_rawTextures;

    // Compiled deterministic layer list: layer 0 is always __missing__
    std::vector<ImageData> m_layers;
    std::vector<std::string> m_layerNames;
    std::unordered_map<std::string, int> m_nameToLayer;
};

} // namespace voxels
