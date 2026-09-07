/**
 * @file texture_atlas.cpp
 * @brief GL 2D array texture atlas implementation and dump exporter.
 */

#include "voxels/render/texture_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <utility>

#include "voxels/assets/texture_loader.hpp"
#include "voxels/core/logger.hpp"
#include "voxels/render/texture_forge.hpp"

namespace voxels {

std::string TextureAtlas::NormalizeTextureName(std::string_view name) {
    std::string s(name);
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (s.ends_with(".png")) {
        s = s.substr(0, s.size() - 4);
    }
    return s;
}

TextureAtlas::TextureAtlas(int tileWidth, int tileHeight)
    : m_tileWidth(tileWidth), m_tileHeight(tileHeight) {
    ImageData missing = TextureForge::GenerateMissingTexture(m_tileWidth, m_tileHeight);
    m_rawTextures[kMissingTextureKey] = std::move(missing);
}

TextureAtlas::~TextureAtlas() {
    Shutdown();
}

TextureAtlas::TextureAtlas(TextureAtlas&& other) noexcept
    : m_tileWidth(other.m_tileWidth)
    , m_tileHeight(other.m_tileHeight)
    , m_textureHandle(other.m_textureHandle)
    , m_isBuilt(other.m_isBuilt)
    , m_rawTextures(std::move(other.m_rawTextures))
    , m_layers(std::move(other.m_layers))
    , m_layerNames(std::move(other.m_layerNames))
    , m_nameToLayer(std::move(other.m_nameToLayer)) {
    other.m_textureHandle = 0;
    other.m_isBuilt = false;
}

TextureAtlas& TextureAtlas::operator=(TextureAtlas&& other) noexcept {
    if (this != &other) {
        Shutdown();
        m_tileWidth = other.m_tileWidth;
        m_tileHeight = other.m_tileHeight;
        m_textureHandle = other.m_textureHandle;
        m_isBuilt = other.m_isBuilt;
        m_rawTextures = std::move(other.m_rawTextures);
        m_layers = std::move(other.m_layers);
        m_layerNames = std::move(other.m_layerNames);
        m_nameToLayer = std::move(other.m_nameToLayer);
        other.m_textureHandle = 0;
        other.m_isBuilt = false;
    }
    return *this;
}

void TextureAtlas::RegisterTexture(std::string_view name, const ImageData& image) {
    if (image.width != m_tileWidth || image.height != m_tileHeight || image.channels != 4) {
        Logger logger;
        logger.Warn("TextureAtlas::RegisterTexture rejected image '" + std::string(name) +
                    "' due to dimension mismatch (" + std::to_string(image.width) + "x" +
                    std::to_string(image.height) + " != " + std::to_string(m_tileWidth) + "x" +
                    std::to_string(m_tileHeight) + ")");
        return;
    }

    const std::string normName = NormalizeTextureName(name);
    m_rawTextures[normName] = image;
}

bool TextureAtlas::RegisterTextureFromFile(std::string_view name, const std::filesystem::path& filePath) {
    const auto loaded = TextureLoader::LoadFromFile(filePath, true, true);
    if (!loaded.has_value()) {
        return false;
    }
    RegisterTexture(name, *loaded);
    return true;
}

void TextureAtlas::PopulateFromBlockRegistry(
    const BlockRegistry& registry,
    const std::filesystem::path& texturesRoot) {
    const auto texturePaths = registry.GetReferencedTextureNames();

    for (const auto& relPath : texturePaths) {
        if (relPath.empty()) {
            continue;
        }

        const std::string norm = NormalizeTextureName(relPath);
        std::string filenameOnly = norm;
        if (filenameOnly.rfind("blocks/", 0) == 0) {
            filenameOnly = filenameOnly.substr(7);
        }

        // Try candidate file locations
        std::vector<std::filesystem::path> candidates = {
            texturesRoot / (norm + ".png"),
            texturesRoot / (filenameOnly + ".png"),
            texturesRoot / "blocks" / (filenameOnly + ".png"),
            texturesRoot / "textures" / "blocks" / (filenameOnly + ".png"),
            std::filesystem::path("app/assets/textures/blocks") / (filenameOnly + ".png"),
            std::filesystem::path("assets/textures/blocks") / (filenameOnly + ".png"),
#ifdef VOXELS_SOURCE_DIR
            std::filesystem::path(VOXELS_SOURCE_DIR) / "app" / "assets" / "textures" / "blocks" / (filenameOnly + ".png"),
#endif
        };

        bool found = false;
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                if (RegisterTextureFromFile(norm, candidate)) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            Logger logger;
            logger.Warn("TextureAtlas: texture '" + norm +
                        "' not found on disk; synthesizing procedural fallback.");
            const ImageData procedural = TextureForge::GenerateTexture(norm);
            RegisterTexture(norm, procedural);
        }
    }
}

bool TextureAtlas::BuildGLTexture() {
    // 1. Build deterministic layer list
    m_layers.clear();
    m_layerNames.clear();
    m_nameToLayer.clear();

    // Layer 0 is always __missing__
    auto missingIt = m_rawTextures.find(kMissingTextureKey);
    if (missingIt != m_rawTextures.end()) {
        m_layers.push_back(missingIt->second);
    } else {
        m_layers.push_back(TextureForge::GenerateMissingTexture(m_tileWidth, m_tileHeight));
    }
    m_layerNames.push_back(kMissingTextureKey);
    m_nameToLayer[kMissingTextureKey] = 0;

    // Iterate through remaining textures in alphabetical order (std::map guarantees this)
    for (const auto& [name, img] : m_rawTextures) {
        if (name == kMissingTextureKey) {
            continue;
        }
        const int layerIndex = static_cast<int>(m_layers.size());
        m_layers.push_back(img);
        m_layerNames.push_back(name);
        m_nameToLayer[name] = layerIndex;

        // Also map alternate alias forms
        if (name.rfind("blocks/", 0) == 0) {
            m_nameToLayer[name.substr(7)] = layerIndex;
        } else {
            m_nameToLayer["blocks/" + name] = layerIndex;
        }
    }

    m_isBuilt = true;

    // 2. Upload to OpenGL 2D array texture if GL context is available
    if (glGenTextures == nullptr || glTexImage3D == nullptr) {
        return true;
    }

    if (m_textureHandle != 0) {
        glDeleteTextures(1, &m_textureHandle);
        m_textureHandle = 0;
    }

    const GLsizei layerCount = static_cast<GLsizei>(m_layers.size());
    glGenTextures(1, &m_textureHandle);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureHandle);

    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_SRGB8_ALPHA8,
        m_tileWidth,
        m_tileHeight,
        layerCount,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);

    for (int i = 0; i < layerCount; ++i) {
        glTexSubImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            0,
            0,
            i,
            m_tileWidth,
            m_tileHeight,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            m_layers[static_cast<std::size_t>(i)].pixels.data());
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Greedy-merged quads store whole-tile UVs beyond [0, 1]; repeat preserves each block's
    // tile instead of clamping one edge texel across the full merged surface.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return true;
}

void TextureAtlas::Shutdown() {
    if (m_textureHandle != 0) {
        if (glDeleteTextures != nullptr) {
            glDeleteTextures(1, &m_textureHandle);
        }
        m_textureHandle = 0;
    }
    m_isBuilt = false;
}

int TextureAtlas::LayerFor(std::string_view textureName) const {
    const std::string norm = NormalizeTextureName(textureName);
    const auto it = m_nameToLayer.find(norm);
    if (it != m_nameToLayer.end()) {
        return it->second;
    }

    Logger logger;
    logger.Warn("TextureAtlas::LayerFor: unknown texture '" + std::string(textureName) +
                "', resolving to missing checker (layer 0).");
    return 0;
}

bool TextureAtlas::HasTexture(std::string_view textureName) const {
    const std::string norm = NormalizeTextureName(textureName);
    return m_nameToLayer.find(norm) != m_nameToLayer.end();
}

const ImageData* TextureAtlas::GetLayerImage(int layerIndex) const {
    if (layerIndex >= 0 && layerIndex < static_cast<int>(m_layers.size())) {
        return &m_layers[static_cast<std::size_t>(layerIndex)];
    }
    return nullptr;
}

bool TextureAtlas::DumpAtlasToPng(const std::filesystem::path& outputPath) const {
    if (m_layers.empty()) {
        return false;
    }

    const int layerCount = static_cast<int>(m_layers.size());
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(layerCount)))));
    const int rows = std::max(1, static_cast<int>(std::ceil(static_cast<float>(layerCount) / static_cast<float>(cols))));

    const int sheetWidth = cols * m_tileWidth;
    const int sheetHeight = rows * m_tileHeight;

    ImageData sheet;
    sheet.width = sheetWidth;
    sheet.height = sheetHeight;
    sheet.channels = 4;
    sheet.pixels.assign(static_cast<std::size_t>(sheetWidth * sheetHeight * 4), 0);

    for (int i = 0; i < layerCount; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int startX = col * m_tileWidth;
        const int startY = row * m_tileHeight;

        const auto& layerImg = m_layers[static_cast<std::size_t>(i)];
        for (int py = 0; py < m_tileHeight; ++py) {
            for (int px = 0; px < m_tileWidth; ++px) {
                const std::size_t srcIdx = (static_cast<std::size_t>(py) * m_tileWidth + px) * 4U;
                const std::size_t dstIdx = (static_cast<std::size_t>(startY + py) * sheetWidth + (startX + px)) * 4U;
                sheet.pixels[dstIdx + 0] = layerImg.pixels[srcIdx + 0];
                sheet.pixels[dstIdx + 1] = layerImg.pixels[srcIdx + 1];
                sheet.pixels[dstIdx + 2] = layerImg.pixels[srcIdx + 2];
                sheet.pixels[dstIdx + 3] = layerImg.pixels[srcIdx + 3];
            }
        }
    }

    return TextureLoader::WritePngToFile(outputPath, sheet);
}

} // namespace voxels
