/**
 * @file test_audio_assets.cpp
 * @brief Automated regression tests for the audio engine and asset management pipeline.
 *
 * @details Covers procedural PCM tone synthesis, inverse-square-law 3D spatial attenuation,
 *          `ProceduralAudioEngine` playback bookkeeping, and `.vpk` asset archive packing /
 *          unpacking plus `AssetManager` asynchronous loading with procedural fallbacks.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

#include "voxels/assets/asset_manager.hpp"
#include "voxels/audio/audio_engine.hpp"

TEST_CASE("Audio.ProceduralToneGeneration", "[audio]") {
    constexpr float frequencyHz = 440.0f;
    constexpr float durationSeconds = 0.1f;
    constexpr std::uint32_t sampleRate = 44100;

    const voxels::PcmBuffer buffer = voxels::GenerateSineWaveTone(frequencyHz, durationSeconds, sampleRate);

    REQUIRE(buffer.sampleRate == sampleRate);
    const auto expectedSamples = static_cast<std::size_t>(durationSeconds * static_cast<float>(sampleRate));
    REQUIRE(buffer.samples.size() == expectedSamples);

    bool hasNonZeroSample = false;
    for (const std::int16_t sample : buffer.samples) {
        if (sample != 0) {
            hasNonZeroSample = true;
            break;
        }
    }
    REQUIRE(hasNonZeroSample);

    // A sine wave at `frequencyHz` crosses zero twice per period; count sign changes and compare
    // against the theoretical expectation, allowing some tolerance for discrete sampling.
    int zeroCrossings = 0;
    for (std::size_t i = 1; i < buffer.samples.size(); ++i) {
        const bool prevNonNegative = buffer.samples[i - 1] >= 0;
        const bool currNonNegative = buffer.samples[i] >= 0;
        if (prevNonNegative != currNonNegative) {
            ++zeroCrossings;
        }
    }
    const double expectedCrossings = 2.0 * frequencyHz * durationSeconds;
    REQUIRE(static_cast<double>(zeroCrossings) == Catch::Approx(expectedCrossings).margin(2.0));
}

TEST_CASE("Audio.3DSpatialAttenuation", "[audio]") {
    const float atZero = voxels::CalculateSpatialAttenuation(0.0f);
    const float atFifty = voxels::CalculateSpatialAttenuation(50.0f);

    REQUIRE(atZero == Catch::Approx(1.0f));
    REQUIRE(atFifty > 0.0f);
    REQUIRE(atFifty < atZero);

    // Inverse square law falloff: doubling distance beyond the reference distance should roughly
    // halve (or more) the attenuation under the clamped inverse-distance model.
    const float atTwenty = voxels::CalculateSpatialAttenuation(20.0f);
    const float atForty = voxels::CalculateSpatialAttenuation(40.0f);
    REQUIRE(atForty < atTwenty);
}

TEST_CASE("Audio.ProceduralEngineLifecycleAndPlayback", "[audio]") {
    voxels::ProceduralAudioEngine engine;
    REQUIRE_FALSE(engine.IsInitialized());
    REQUIRE(engine.Initialize());
    REQUIRE(engine.IsInitialized());

    const voxels::SoundHandle missingSound = engine.LoadSound("nonexistent_sound_file.wav");
    REQUIRE(missingSound.IsValid());
    REQUIRE(engine.GetSoundBuffer(missingSound) != nullptr);

    engine.SetMasterVolume(0.5f);
    engine.SetSFXVolume(0.5f);
    REQUIRE(engine.GetMasterVolume() == Catch::Approx(0.5f));
    REQUIRE(engine.GetSFXVolume() == Catch::Approx(0.5f));

    engine.PlaySound(missingSound, 1.0f, 1.0f, glm::vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(engine.GetPlaybackHistory().size() == 1);
    REQUIRE(engine.GetPlaybackHistory().back().attenuatedVolume == Catch::Approx(0.25f));

    engine.Shutdown();
    REQUIRE_FALSE(engine.IsInitialized());
}

TEST_CASE("AssetManager.PackingAndUnpacking", "[assets]") {
    const std::filesystem::path archivePath =
        std::filesystem::temp_directory_path() / "voxels_test_archive.vpk";

    std::vector<voxels::AssetArchiveEntry> entries;
    entries.push_back({"textures/stone.raw", {std::byte{1}, std::byte{2}, std::byte{3}}});
    entries.push_back({"sounds/block_break.pcm", {std::byte{4}, std::byte{5}}});
    entries.push_back({"models/item.mdl", {}});

    REQUIRE(voxels::AssetArchive::WriteArchive(archivePath, entries));

    const std::optional<std::vector<voxels::AssetArchiveEntry>> loaded =
        voxels::AssetArchive::ReadArchive(archivePath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == entries.size());

    for (std::size_t i = 0; i < entries.size(); ++i) {
        REQUIRE((*loaded)[i].name == entries[i].name);
        REQUIRE((*loaded)[i].data == entries[i].data);
    }

    std::filesystem::remove(archivePath);
}

TEST_CASE("AssetManager.ReadArchiveMissingFileFails", "[assets]") {
    const std::optional<std::vector<voxels::AssetArchiveEntry>> loaded =
        voxels::AssetArchive::ReadArchive("this_archive_does_not_exist.vpk");
    REQUIRE_FALSE(loaded.has_value());
}

TEST_CASE("AssetManager.AsyncLoadWithProceduralFallback", "[assets]") {
    voxels::AssetManager manager;

    std::future<voxels::AssetHandle> future =
        manager.LoadAsync("textures/nonexistent_missing_texture.raw", voxels::AssetType::Texture);
    const voxels::AssetHandle handle = future.get();

    REQUIRE(handle.IsValid());
    REQUIRE(handle.type == voxels::AssetType::Texture);
    REQUIRE(manager.IsLoaded(handle));

    const std::optional<std::vector<std::byte>> data = manager.GetData(handle);
    REQUIRE(data.has_value());
    // 16x16 RGBA solid-color fallback texture.
    REQUIRE(data->size() == static_cast<std::size_t>(16 * 16 * 4));
}

TEST_CASE("AssetManager.MountedArchiveResolvesBeforeFallback", "[assets]") {
    const std::filesystem::path archivePath =
        std::filesystem::temp_directory_path() / "voxels_test_mount_archive.vpk";

    const std::vector<voxels::AssetArchiveEntry> entries = {
        {"shaders/basic.spv", {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}},
    };
    REQUIRE(voxels::AssetArchive::WriteArchive(archivePath, entries));

    voxels::AssetManager manager;
    REQUIRE(manager.MountArchive(archivePath));

    std::future<voxels::AssetHandle> future = manager.LoadAsync("shaders/basic.spv", voxels::AssetType::Shader);
    const voxels::AssetHandle handle = future.get();

    const std::optional<std::vector<std::byte>> data = manager.GetData(handle);
    REQUIRE(data.has_value());
    REQUIRE(*data == entries[0].data);

    std::filesystem::remove(archivePath);
}

TEST_CASE("Assets.ProceduralFallbackGenerators", "[assets]") {
    const voxels::ImageData image = voxels::GenerateSolidColorImage(4, 4, {10, 20, 30, 255});
    REQUIRE(image.width == 4);
    REQUIRE(image.height == 4);
    REQUIRE(image.pixels.size() == 4u * 4u * 4u);
    REQUIRE(image.pixels[0] == 10);
    REQUIRE(image.pixels[1] == 20);
    REQUIRE(image.pixels[2] == 30);
    REQUIRE(image.pixels[3] == 255);

    const voxels::FontData font = voxels::GenerateFallbackAsciiFont();
    REQUIRE(font.glyphCount == 0x5F);
    REQUIRE(font.bitmap.size() ==
            static_cast<std::size_t>(font.glyphCount) * static_cast<std::size_t>(font.glyphWidth) *
                static_cast<std::size_t>(font.glyphHeight));
}
