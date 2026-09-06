/**
 * @file web_ui_manifest.cpp
 * @brief Hash-verified local web UI manifest parser.
 *
 * @details Keeps UI asset validation in the native layer so modified or incomplete browser files
 * fail before a browser runtime is initialized. CEF remains isolated to its future compositor.
 */

#include "voxels/ui/web_ui_manifest.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>

namespace voxels {
namespace {
constexpr std::array<std::uint32_t, 64> kRoundConstants{0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

[[nodiscard]] std::uint32_t RotateRight(const std::uint32_t value, const int count) noexcept { return (value >> count) | (value << (32 - count)); }

[[nodiscard]] std::string Sha256File(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "Unable to read web UI asset: " + path.string(); return {}; }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8U;
    bytes.push_back(0x80U);
    while ((bytes.size() % 64U) != 56U) { bytes.push_back(0U); }
    for (int shift = 56; shift >= 0; shift -= 8) { bytes.push_back(static_cast<std::uint8_t>(bitLength >> shift)); }
    std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const std::size_t byte = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(bytes[byte]) << 24U) | (static_cast<std::uint32_t>(bytes[byte + 1U]) << 16U) | (static_cast<std::uint32_t>(bytes[byte + 2U]) << 8U) | static_cast<std::uint32_t>(bytes[byte + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const auto small0 = RotateRight(words[index - 15U], 7) ^ RotateRight(words[index - 15U], 18) ^ (words[index - 15U] >> 3U);
            const auto small1 = RotateRight(words[index - 2U], 17) ^ RotateRight(words[index - 2U], 19) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + small0 + words[index - 7U] + small1;
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto temp1 = h + (RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25)) + ((e & f) ^ (~e & g)) + kRoundConstants[index] + words[index];
            const auto temp2 = (RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    std::ostringstream digest;
    for (const auto value : state) { digest << std::hex << std::setfill('0') << std::setw(8) << value; }
    return digest.str();
}

[[nodiscard]] bool IsSafeRelativePath(const std::filesystem::path& path) {
    return !path.empty() && !path.is_absolute() && path.lexically_normal().begin()->string() != "..";
}
} // namespace

bool LoadAndVerifyWebUiManifest(const std::filesystem::path& manifestPath, WebUiManifest& manifest, std::string& error) {
    manifest = {};
    try {
        std::ifstream input(manifestPath);
        if (!input) { error = "Web UI manifest is missing: " + manifestPath.string(); return false; }
        const nlohmann::json json = nlohmann::json::parse(input);
        if (json.at("schema_version").get<int>() != 1) { error = "Unsupported web UI manifest schema version."; return false; }
        manifest.entryHtml = json.at("entry_html").get<std::string>();
        manifest.sourceRevision = json.at("source_revision").get<std::string>();
        manifest.sourceSha256 = json.at("source_sha256").get<std::string>();
        for (const auto& [assetPath, metadata] : json.at("assets").items()) {
            WebUiAsset asset{assetPath, metadata.at("sha256").get<std::string>(), metadata.at("bytes").get<std::uintmax_t>()};
            if (!IsSafeRelativePath(asset.path) || asset.sha256.size() != 64U) { error = "Web UI manifest contains an invalid asset entry: " + assetPath; return false; }
            const auto fullPath = manifestPath.parent_path() / asset.path;
            if (!std::filesystem::is_regular_file(fullPath) || std::filesystem::file_size(fullPath) != asset.bytes) { error = "Web UI asset is missing or has an unexpected size: " + assetPath; return false; }
            const std::string actualHash = Sha256File(fullPath, error);
            if (actualHash.empty() || actualHash != asset.sha256) { error = "Web UI asset hash verification failed: " + assetPath; return false; }
            manifest.assets.push_back(std::move(asset));
        }
        if (!IsSafeRelativePath(manifest.entryHtml) || !json.at("assets").contains(manifest.entryHtml.string())) { error = "Web UI manifest entry HTML is not a declared emitted asset."; return false; }
        return true;
    } catch (const std::exception& exception) {
        error = "Web UI manifest parsing failed: " + std::string(exception.what());
        return false;
    }
}
} // namespace voxels