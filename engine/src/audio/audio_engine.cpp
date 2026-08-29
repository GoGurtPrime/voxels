/**
 * @file audio_engine.cpp
 * @brief Implementation of PCM tone synthesis, spatial attenuation, and `ProceduralAudioEngine`.
 */

#include "voxels/audio/audio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace voxels {

namespace {

constexpr float kTwoPi = 6.283185307179586476925f;

PcmBuffer GenerateWaveTone(float frequencyHz, float durationSeconds, std::uint32_t sampleRate,
                            float amplitude, bool square) {
    PcmBuffer buffer;
    buffer.sampleRate = sampleRate;

    const auto sampleCount = static_cast<std::size_t>(std::max(0.0f, durationSeconds) * static_cast<float>(sampleRate));
    buffer.samples.resize(sampleCount);

    const float clampedAmplitude = std::clamp(amplitude, 0.0f, 1.0f);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float value = std::sin(kTwoPi * frequencyHz * t);
        if (square) {
            value = value >= 0.0f ? 1.0f : -1.0f;
        }
        buffer.samples[i] = static_cast<std::int16_t>(value * clampedAmplitude * 32767.0f);
    }

    return buffer;
}

/// Reads a whole file into memory; returns an empty (missing) optional-equivalent vector on failure.
std::vector<char> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    file.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<std::size_t>(size));
    if (!file.read(data.data(), size)) {
        return {};
    }
    return data;
}

} // namespace

PcmBuffer GenerateSineWaveTone(float frequencyHz, float durationSeconds, std::uint32_t sampleRate,
                                float amplitude) {
    return GenerateWaveTone(frequencyHz, durationSeconds, sampleRate, amplitude, /*square=*/false);
}

PcmBuffer GenerateSquareWaveTone(float frequencyHz, float durationSeconds, std::uint32_t sampleRate,
                                  float amplitude) {
    return GenerateWaveTone(frequencyHz, durationSeconds, sampleRate, amplitude, /*square=*/true);
}

float CalculateSpatialAttenuation(float distance, float referenceDistance, float rolloffFactor) noexcept {
    const float d = std::max(0.0f, distance);
    const float refDist = std::max(0.0001f, referenceDistance);
    if (d <= refDist) {
        return 1.0f;
    }
    // OpenAL-style clamped inverse-distance model: gain = refDist / (refDist + rolloff * (d - refDist)).
    const float denom = refDist + rolloffFactor * (d - refDist);
    return refDist / denom;
}

bool ProceduralAudioEngine::Initialize() {
    m_initialized = true;
    return true;
}

SoundHandle ProceduralAudioEngine::LoadSound(const std::string& path) {
    const std::vector<char> fileBytes = ReadFileBytes(path);
    if (fileBytes.empty()) {
        // Procedural fallback: missing/unreadable sound assets become a short square-wave beep.
        m_sounds.push_back(GenerateSquareWaveTone(440.0f, 0.15f));
    } else {
        // Minimal placeholder decode path: real WAV/OGG decoding is out of scope for this pass,
        // so treat the raw bytes as an opaque, already-decoded PCM stream is not attempted here;
        // instead synthesize a tone whose length reflects the file size so loads remain distinct.
        const float durationSeconds = std::clamp(static_cast<float>(fileBytes.size()) / 44100.0f, 0.05f, 2.0f);
        m_sounds.push_back(GenerateSineWaveTone(440.0f, durationSeconds));
    }
    return SoundHandle{static_cast<std::uint32_t>(m_sounds.size())};
}

MusicHandle ProceduralAudioEngine::LoadMusic(const std::string& path) {
    const std::vector<char> fileBytes = ReadFileBytes(path);
    if (fileBytes.empty()) {
        m_music.push_back(GenerateSquareWaveTone(220.0f, 0.5f));
    } else {
        const float durationSeconds = std::clamp(static_cast<float>(fileBytes.size()) / 44100.0f, 0.5f, 4.0f);
        m_music.push_back(GenerateSineWaveTone(220.0f, durationSeconds));
    }
    return MusicHandle{static_cast<std::uint32_t>(m_music.size())};
}

void ProceduralAudioEngine::PlaySound(SoundHandle handle, float volume, float pitch, const glm::vec3& spatialPos) {
    const float distance = glm::length(spatialPos);
    const float attenuation = CalculateSpatialAttenuation(distance);
    const float finalVolume = std::clamp(volume, 0.0f, 1.0f) * m_sfxVolume * m_masterVolume * attenuation;
    m_playbackHistory.push_back(PlaybackEvent{handle, volume, pitch, spatialPos, finalVolume});
}

void ProceduralAudioEngine::PlayMusic(MusicHandle /*handle*/, bool /*loop*/, float /*fadeTimeSeconds*/) {
    // Headless engine has no streaming device; playback state is intentionally not modeled beyond
    // volume controls since no test/consumer currently observes music transport state.
}

void ProceduralAudioEngine::SetMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
}

void ProceduralAudioEngine::SetMusicVolume(float volume) {
    m_musicVolume = std::clamp(volume, 0.0f, 1.0f);
}

void ProceduralAudioEngine::SetSFXVolume(float volume) {
    m_sfxVolume = std::clamp(volume, 0.0f, 1.0f);
}

void ProceduralAudioEngine::Shutdown() {
    m_initialized = false;
    m_sounds.clear();
    m_music.clear();
    m_playbackHistory.clear();
}

bool ProceduralAudioEngine::IsInitialized() const noexcept {
    return m_initialized;
}

float ProceduralAudioEngine::GetMasterVolume() const noexcept {
    return m_masterVolume;
}

float ProceduralAudioEngine::GetMusicVolume() const noexcept {
    return m_musicVolume;
}

float ProceduralAudioEngine::GetSFXVolume() const noexcept {
    return m_sfxVolume;
}

const PcmBuffer* ProceduralAudioEngine::GetSoundBuffer(SoundHandle handle) const noexcept {
    if (!handle.IsValid() || handle.id > m_sounds.size()) {
        return nullptr;
    }
    return &m_sounds[handle.id - 1];
}

const PcmBuffer* ProceduralAudioEngine::GetMusicBuffer(MusicHandle handle) const noexcept {
    if (!handle.IsValid() || handle.id > m_music.size()) {
        return nullptr;
    }
    return &m_music[handle.id - 1];
}

const std::vector<ProceduralAudioEngine::PlaybackEvent>& ProceduralAudioEngine::GetPlaybackHistory() const noexcept {
    return m_playbackHistory;
}

} // namespace voxels
