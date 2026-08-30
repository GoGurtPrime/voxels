/**
 * @file texture_loader.cpp
 * @brief PNG and image decoding and encoding implementation using stb_image and stb_image_write.
 */

#include "voxels/assets/texture_loader.hpp"

#include <cstring>
#include <fstream>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996 4244 4100 4702)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "voxels/core/logger.hpp"

namespace voxels {

namespace {

void StbMemoryWriteCallback(void* context, void* data, int size) {
    auto* outVector = static_cast<std::vector<std::uint8_t>*>(context);
    if (outVector != nullptr && data != nullptr && size > 0) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        outVector->insert(outVector->end(), bytes, bytes + size);
    }
}

} // namespace

std::optional<ImageData> TextureLoader::LoadFromFile(
    const std::filesystem::path& filePath,
    bool requirePowerOfTwo,
    bool requireSquare) {
    std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
    if (!stream.is_open()) {
        Logger logger;
        logger.Warn("TextureLoader failed to open file: " + filePath.string());
        return std::nullopt;
    }

    const auto fileSize = stream.tellg();
    if (fileSize <= 0) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(fileSize));
    if (!stream.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        return std::nullopt;
    }

    return LoadFromMemory(buffer, requirePowerOfTwo, requireSquare);
}

std::optional<ImageData> TextureLoader::LoadFromMemory(
    std::span<const std::uint8_t> data,
    bool requirePowerOfTwo,
    bool requireSquare) {
    if (data.empty()) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &width,
        &height,
        &channelsInFile,
        STBI_rgb_alpha);

    if (decoded == nullptr) {
        Logger logger;
        logger.Warn("TextureLoader: stbi_load_from_memory failed: " + std::string(stbi_failure_reason()));
        return std::nullopt;
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(decoded);
        return std::nullopt;
    }

    if (requirePowerOfTwo && (!IsPowerOfTwo(width) || !IsPowerOfTwo(height))) {
        Logger logger;
        logger.Warn("TextureLoader rejected non-power-of-two image (" +
                    std::to_string(width) + "x" + std::to_string(height) + ")");
        stbi_image_free(decoded);
        return std::nullopt;
    }

    if (requireSquare && (width != height)) {
        Logger logger;
        logger.Warn("TextureLoader rejected non-square image (" +
                    std::to_string(width) + "x" + std::to_string(height) + ")");
        stbi_image_free(decoded);
        return std::nullopt;
    }

    ImageData image;
    image.width = width;
    image.height = height;
    image.channels = 4;
    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    image.pixels.resize(byteCount);
    std::memcpy(image.pixels.data(), decoded, byteCount);

    stbi_image_free(decoded);
    return image;
}

std::vector<std::uint8_t> TextureLoader::EncodePngToMemory(
    int width, int height, int channels, const std::uint8_t* pixels) {
    std::vector<std::uint8_t> result;
    if (width <= 0 || height <= 0 || channels <= 0 || pixels == nullptr) {
        return result;
    }

    stbi_write_png_to_func(
        StbMemoryWriteCallback,
        &result,
        width,
        height,
        channels,
        pixels,
        width * channels);

    return result;
}

bool TextureLoader::WritePngToFile(
    const std::filesystem::path& filePath,
    const ImageData& image) {
    if (image.width <= 0 || image.height <= 0 || image.channels <= 0 || image.pixels.empty()) {
        return false;
    }
    return WritePngToFile(filePath, image.width, image.height, image.channels, image.pixels.data());
}

bool TextureLoader::WritePngToFile(
    const std::filesystem::path& filePath,
    int width, int height, int channels, const void* pixels) {
    if (width <= 0 || height <= 0 || channels <= 0 || pixels == nullptr) {
        return false;
    }

    if (filePath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(filePath.parent_path(), ec);
    }

    const int result = stbi_write_png(
        filePath.string().c_str(),
        width,
        height,
        channels,
        pixels,
        width * channels);

    return result != 0;
}

} // namespace voxels
