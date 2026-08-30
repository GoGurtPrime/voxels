/**
 * @file asset_bundler.cpp
 * @brief Authored content validation and deterministic VPK assembly.
 */

#include "voxels/assets/asset_bundler.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <span>

#include <nlohmann/json.hpp>

#include "voxels/assets/texture_loader.hpp"
#include "voxels/assets/vmdl_codec.hpp"

namespace voxels {

namespace {

[[nodiscard]] AssetType Classify(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") return AssetType::Texture;
    if (extension == ".wav" || extension == ".ogg") return AssetType::Sound;
    if (extension == ".vmdl") return AssetType::Model;
    if (extension == ".glsl" || extension == ".vert" || extension == ".frag") return AssetType::Shader;
    if (extension == ".json") return AssetType::Json;
    if (extension == ".ttf" || extension == ".otf") return AssetType::Font;
    return AssetType::Unknown;
}

[[nodiscard]] std::vector<std::byte> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open() || input.tellg() < 0) return {};
    const std::size_t size = static_cast<std::size_t>(input.tellg());
    std::vector<std::byte> result(size);
    input.seekg(0);
    if (size != 0 && !input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size))) return {};
    return result;
}

[[nodiscard]] bool ValidateBlocks(const std::filesystem::path& root, std::string& error) {
    const std::filesystem::path blocksPath = root / "data" / "blocks.json";
    std::ifstream input(blocksPath);
    if (!input.is_open()) return true;
    nlohmann::json document;
    try { input >> document; } catch (const nlohmann::json::exception& exception) { error = "invalid blocks.json: " + std::string(exception.what()); return false; }
    std::set<int> numericIds;
    int maximumId = -1;
    for (const auto& block : document.at("blocks")) {
        const std::string blockId = block.at("id").get<std::string>();
        const int numericId = block.at("numeric_id").get<int>();
        if (!numericIds.insert(numericId).second) { error = "duplicate numeric block id for " + blockId; return false; }
        maximumId = std::max(maximumId, numericId);
        const nlohmann::json textures = block.value("textures", nlohmann::json::object());
        for (const auto& [face, texture] : textures.items()) {
            static_cast<void>(face);
            const std::filesystem::path texturePath = root / "textures" / (texture.get<std::string>() + ".png");
            if (!std::filesystem::exists(texturePath)) { error = "block " + blockId + " references missing texture " + texturePath.generic_string(); return false; }
            if (!TextureLoader::LoadFromFile(texturePath)) { error = "block " + blockId + " references invalid texture " + texturePath.generic_string(); return false; }
        }
        if (!block["model_id"].is_null()) {
            std::filesystem::path modelPath = root / block["model_id"].get<std::string>();
            if (modelPath.extension() != ".vmdl") modelPath += ".vmdl";
            try {
                const auto bytes = ReadFile(modelPath);
                const VoxelModel model = VmdlCodec::Load(std::span<const std::byte>(bytes));
                static_cast<void>(model);
            }
            catch (const std::exception& exception) { error = "block " + blockId + " references invalid model " + modelPath.generic_string() + ": " + exception.what(); return false; }
        }
    }
    if (maximumId + 1 != static_cast<int>(numericIds.size())) { error = "block numeric ids must be dense starting at zero"; return false; }
    return true;
}

} // namespace

bool AssetBundler::Bundle(const std::filesystem::path& inputDirectory, const std::filesystem::path& outputPath,
                          AssetBundleReport& report, std::string& error) {
    report = {};
    if (!std::filesystem::is_directory(inputDirectory)) { error = "bundle input is not a directory: " + inputDirectory.string(); return false; }
    if (!ValidateBlocks(inputDirectory, error)) return false;
    std::vector<AssetArchiveEntry> entries;
    std::error_code filesystemError;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(inputDirectory, filesystemError)) {
        if (filesystemError) { error = "could not enumerate bundle input"; return false; }
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".vpk") continue;
        const auto relativePath = std::filesystem::relative(entry.path(), inputDirectory, filesystemError).generic_string();
        if (filesystemError) { error = "could not determine content path"; return false; }
        const auto payload = ReadFile(entry.path());
        if (payload.empty() && std::filesystem::file_size(entry.path(), filesystemError) != 0) { error = "could not read content file " + relativePath; return false; }
        const AssetType type = Classify(entry.path());
        if (type == AssetType::Unknown) report.warnings.push_back("unclassified asset: " + relativePath);
        entries.push_back({relativePath, payload, type});
    }
    if (entries.empty()) { error = "bundle input contains no files"; return false; }
    const std::string manifest = R"({"pack_name":"core","pack_version":1,"engine_compatibility":"0.1","format":"VPK1"})";
    std::vector<std::byte> manifestBytes(manifest.size());
    for (std::size_t index = 0; index < manifest.size(); ++index) {
        manifestBytes[index] = static_cast<std::byte>(manifest[index]);
    }
    entries.push_back({"manifest.json", std::move(manifestBytes), AssetType::Json});
    return VpkArchive::Write(outputPath, std::move(entries), &report.archive, &error);
}

} // namespace voxels