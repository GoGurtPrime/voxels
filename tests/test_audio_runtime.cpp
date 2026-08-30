/**
 * @file test_audio_runtime.cpp
 * @brief Behavior tests for callback-safe mixing and spatial audio routing.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "voxels/audio/audio_engine.hpp"
#include "voxels/world/block.hpp"

namespace {
voxels::PcmBuffer ConstantClip(std::int16_t sample = 16000) {
    voxels::PcmBuffer clip;
    clip.samples.assign(256, sample);
    return clip;
}
}

TEST_CASE("Mixer.MixesMultipleVoicesWithoutClipping", "[audio][mixer]") {
    voxels::AudioMixer mixer;
    const voxels::SoundHandle clip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Sfx);
    for (int index = 0; index < 16; ++index) mixer.Play(clip);
    std::array<float, 256> output{};
    mixer.Mix(output.data(), output.size() / 2U);
    const float peak = *std::max_element(output.begin(), output.end(), [](float left, float right) { return std::abs(left) < std::abs(right); });
    REQUIRE(std::abs(peak) <= 1.0f);
    REQUIRE(std::abs(peak) > 0.5f);
}

TEST_CASE("Spatial.AttenuationAndPanningMatchExpectedCurve", "[audio][spatial]") {
    const glm::vec3 listener{0.0f};
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};
    const auto left = voxels::CalculateSpatialParams(listener, forward, glm::vec3{-4.0f, 0.0f, 0.0f});
    const auto right = voxels::CalculateSpatialParams(listener, forward, glm::vec3{4.0f, 0.0f, 0.0f});
    const auto far = voxels::CalculateSpatialParams(listener, forward, glm::vec3{33.0f, 0.0f, 0.0f});
    REQUIRE(left.audible);
    REQUIRE(right.audible);
    REQUIRE(left.pan < -0.9f);
    REQUIRE(right.pan > 0.9f);
    REQUIRE(left.gain == Catch::Approx(voxels::CalculateSpatialAttenuation(4.0f)));
    REQUIRE_FALSE(far.audible);
}

TEST_CASE("Mixer.CategoryGainsApplyImmediately", "[audio][mixer]") {
    voxels::AudioMixer mixer;
    const voxels::SoundHandle clip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Sfx);
    std::array<float, 64> audible{};
    mixer.Play(clip);
    mixer.Mix(audible.data(), audible.size() / 2U);
    mixer.SetCategoryGain(voxels::AudioCategory::Sfx, 0.0f);
    std::array<float, 64> muted{};
    mixer.Play(clip);
    mixer.Mix(muted.data(), muted.size() / 2U);
    REQUIRE(std::abs(audible[0]) > 0.01f);
    REQUIRE(muted[0] == Catch::Approx(0.0f));
}

TEST_CASE("Decode.WavRoundTripsGeneratedFile", "[audio][decode]") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "voxels_audio_forge_test";
    std::filesystem::remove_all(root);
    REQUIRE(voxels::ForgeDefaultAudioAssets(root, true) == 28);
    voxels::PcmBuffer decoded;
    REQUIRE(voxels::DecodeWavFile(root / "break_stone.wav", decoded));
    REQUIRE(decoded.sampleRate == 44100);
    REQUIRE(decoded.channels == 1);
    REQUIRE(decoded.samples.size() > 1000);
    std::filesystem::remove_all(root);
}

TEST_CASE("SoundBank.ResolvesBlockSoundIdsToShippedFiles", "[audio][sound_bank]") {
    const voxels::BlockRegistry registry = voxels::CreateDefaultBlockRegistry();
    const std::filesystem::path audioRoot = std::filesystem::path(VOXELS_SOURCE_DIR) / "app" / "assets" / "audio";
    for (const auto& [id, definition] : registry.GetAllDefinitions()) {
        (void)id;
        for (const std::string* soundId : {&definition.sounds.breakSound, &definition.sounds.placeSound, &definition.sounds.stepSound}) {
            if (soundId->empty()) continue;
            const std::filesystem::path clip = audioRoot / (soundId->substr(soundId->find('/') + 1) + ".wav");
            REQUIRE(std::filesystem::exists(clip));
        }
    }
}