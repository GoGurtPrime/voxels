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
    for (int index = 0; index < 16; ++index) (void)mixer.Play(clip);
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
    (void)mixer.Play(clip);
    mixer.Mix(audible.data(), audible.size() / 2U);
    mixer.SetCategoryGain(voxels::AudioCategory::Sfx, 0.0f);
    std::array<float, 64> muted{};
    (void)mixer.Play(clip);
    mixer.Mix(muted.data(), muted.size() / 2U);
    REQUIRE(std::abs(audible[0]) > 0.01f);
    REQUIRE(muted[0] == Catch::Approx(0.0f));
}

TEST_CASE("Mixer.VoiceHandlesRejectStaleEnvelopeCommands", "[audio][mixer][music]") {
    voxels::AudioMixer mixer;
    const voxels::SoundHandle clip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Music);
    std::array<voxels::VoiceHandle, voxels::AudioMixer::kMaxVoices> firstVoices{};
    for (auto& voice : firstVoices) voice = mixer.Play(clip, 1.0f, 1.0f, 0.0f, true);
    std::array<float, 64> initial{};
    mixer.Mix(initial.data(), initial.size() / 2U);

    for (std::size_t index = 0; index < firstVoices.size(); ++index) (void)mixer.Play(clip, 1.0f, 1.0f, 0.0f, true);
    mixer.SetVoiceEnvelope(firstVoices.front(), 0.0f, 0.0f);
    std::array<float, 64> replacement{};
    mixer.Mix(replacement.data(), replacement.size() / 2U);

    REQUIRE(mixer.ActiveVoiceCount() == voxels::AudioMixer::kMaxVoices);
    REQUIRE(std::abs(replacement[0]) > 0.01f);
}

TEST_CASE("Mixer.VoiceEnvelopeReachesSilenceBeforeReclaim", "[audio][mixer][music]") {
    voxels::AudioMixer mixer;
    const voxels::SoundHandle clip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Music);
    const voxels::VoiceHandle voice = mixer.Play(clip, 1.0f, 1.0f, 0.0f, true);
    std::array<float, 8> initial{};
    mixer.Mix(initial.data(), initial.size() / 2U);
    mixer.SetVoiceEnvelope(voice, 0.0f, 4.0f / 44100.0f);

    std::array<float, 8> fading{};
    mixer.Mix(fading.data(), fading.size() / 2U);
    REQUIRE(std::abs(fading[0]) > std::abs(fading[6]));
    REQUIRE(std::abs(fading[6]) == Catch::Approx(0.0f));
    REQUIRE(mixer.ActiveVoiceCount() == 0);
}

TEST_CASE("Mixer.SequentialMusicVoicesDoNotOverlapOutsideCrossfade", "[audio][mixer][music]") {
    voxels::AudioMixer mixer;
    const voxels::SoundHandle firstClip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Music);
    const voxels::SoundHandle secondClip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Music);
    const voxels::VoiceHandle firstVoice = mixer.Play(firstClip, 1.0f, 1.0f, -1.0f, true);
    std::array<float, 8> firstOutput{};
    mixer.Mix(firstOutput.data(), firstOutput.size() / 2U);
    mixer.SetVoiceEnvelope(firstVoice, 0.0f, 4.0f / 44100.0f);
    std::array<float, 8> silenceBoundary{};
    mixer.Mix(silenceBoundary.data(), silenceBoundary.size() / 2U);

    const voxels::VoiceHandle secondVoice = mixer.Play(secondClip, 0.0f, 1.0f, 1.0f, true);
    mixer.SetVoiceEnvelope(secondVoice, 1.0f, 4.0f / 44100.0f);
    std::array<float, 8> secondOutput{};
    mixer.Mix(secondOutput.data(), secondOutput.size() / 2U);

    REQUIRE(std::abs(silenceBoundary[6]) == Catch::Approx(0.0f));
    REQUIRE(std::abs(secondOutput[0]) == Catch::Approx(0.0f));
    REQUIRE(std::abs(secondOutput[1]) > 0.01f);
}

TEST_CASE("Mixer.ActiveMusicVoiceIsNotStolenBySfx", "[audio][mixer][music]") {
    voxels::AudioMixer mixer;
    const voxels::SoundHandle musicClip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Music);
    const voxels::SoundHandle sfxClip = mixer.AddClip(ConstantClip(), voxels::AudioCategory::Sfx);
    mixer.SetCategoryGain(voxels::AudioCategory::Sfx, 0.0f);
    const voxels::VoiceHandle musicVoice = mixer.Play(musicClip, 1.0f, 1.0f, 0.0f, true);
    REQUIRE(musicVoice.IsValid());

    std::array<float, 8> initial{};
    mixer.Mix(initial.data(), initial.size() / 2U);
    for (std::size_t index = 0; index < voxels::AudioMixer::kMaxVoices; ++index) {
        (void)mixer.Play(sfxClip, 1.0f, 1.0f, 0.0f, true);
    }

    std::array<float, 8> afterSfxBurst{};
    mixer.Mix(afterSfxBurst.data(), afterSfxBurst.size() / 2U);
    REQUIRE(mixer.ActiveVoiceCount() == voxels::AudioMixer::kMaxVoices);
    REQUIRE(std::abs(afterSfxBurst[0]) > 0.01f);
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

TEST_CASE("Decode.ImaAdpcmMusicAssetPreservesFormatAndDuration", "[audio][decode][music]") {
    const std::filesystem::path asset = std::filesystem::path(VOXELS_SOURCE_DIR) / "app" / "assets" / "audio" / "playlist_ingame" / "Calm Fields.wav";
    voxels::PcmBuffer decoded;
    REQUIRE(voxels::DecodeWavFile(asset, decoded));
    REQUIRE(decoded.sampleRate == 44100);
    REQUIRE(decoded.channels == 2);
    REQUIRE(decoded.samples.size() > 14'000'000);
    REQUIRE(decoded.samples.size() < 15'000'000);
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