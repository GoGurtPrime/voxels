#pragma once

/**
 * @file audio_engine.hpp
 * @brief Audio device abstraction: sound effects, music streaming, and 3D spatial attenuation.
 *
 * @details Declares `IAudioEngine`, a backend-agnostic interface for playing short sound
 *          effects and streaming background music with basic 3D positional attenuation.
 *          `ProceduralAudioEngine` is the default implementation: it never touches a physical
 *          audio device, so it runs identically on desktop, headless CI, and (eventually)
 *          Dreamcast targets. When a requested sound/music file cannot be found or decoded, it
 *          synthesizes a procedural PCM waveform (square-wave "beep") instead of failing, per
 *          the project's human-in-the-loop procedural fallback policy.
 *
 *          Relation to the rest of the codebase: the app layer and world simulation call into
 *          `IAudioEngine` to react to gameplay events (footsteps, block placement, ambient
 *          music); the asset pipeline (`voxels/assets/asset_manager.hpp`) is responsible for
 *          locating/streaming the underlying audio files from disk or packed archives.
 */

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace voxels {

/// Opaque handle to a loaded short sound effect. `id == 0` is the invalid/unset handle.
struct SoundHandle {
    std::uint32_t id = 0;

    [[nodiscard]] bool IsValid() const noexcept { return id != 0; }
    [[nodiscard]] bool operator==(const SoundHandle&) const noexcept = default;
};

/// Opaque handle to a loaded streaming music track. `id == 0` is the invalid/unset handle.
struct MusicHandle {
    std::uint32_t id = 0;

    [[nodiscard]] bool IsValid() const noexcept { return id != 0; }
    [[nodiscard]] bool operator==(const MusicHandle&) const noexcept = default;
};

/// Mono 16-bit PCM sample buffer, the common currency between decoders/synthesizers and mixers.
struct PcmBuffer {
    std::vector<std::int16_t> samples;
    std::uint32_t sampleRate = 44100;
};

/// Synthesizes a mono sine wave tone, used both for procedural-fallback playback and tests.
[[nodiscard]] PcmBuffer GenerateSineWaveTone(float frequencyHz, float durationSeconds,
                                              std::uint32_t sampleRate = 44100,
                                              float amplitude = 0.5f);

/// Synthesizes a mono square wave tone; used as the "missing asset" fallback beep/click.
[[nodiscard]] PcmBuffer GenerateSquareWaveTone(float frequencyHz, float durationSeconds,
                                                std::uint32_t sampleRate = 44100,
                                                float amplitude = 0.5f);

/// Computes an inverse-square-law volume attenuation factor for a sound at `distance` units
/// from the listener. Returns 1.0 at `distance <= referenceDistance` and falls off towards 0 as
/// distance grows, matching the classic OpenAL "inverse clamped distance" model.
[[nodiscard]] float CalculateSpatialAttenuation(float distance, float referenceDistance = 1.0f,
                                                 float rolloffFactor = 1.0f) noexcept;

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    virtual bool Initialize() = 0;
    virtual SoundHandle LoadSound(const std::string& path) = 0;
    virtual MusicHandle LoadMusic(const std::string& path) = 0;
    virtual void PlaySound(SoundHandle handle, float volume, float pitch, const glm::vec3& spatialPos) = 0;
    virtual void PlayMusic(MusicHandle handle, bool loop, float fadeTimeSeconds) = 0;
    virtual void SetMasterVolume(float volume) = 0;
    virtual void SetMusicVolume(float volume) = 0;
    virtual void SetSFXVolume(float volume) = 0;
    virtual void Shutdown() = 0;
};

/// Default `IAudioEngine` implementation. Performs no real audio device I/O; it decodes/holds
/// PCM buffers in memory and records playback requests so callers (and tests) can observe
/// behavior deterministically. Missing/unreadable files fall back to a synthesized beep tone.
class ProceduralAudioEngine final : public IAudioEngine {
public:
    /// Records a single `PlaySound` invocation for introspection/testing.
    struct PlaybackEvent {
        SoundHandle sound;
        float volume = 0.0f;
        float pitch = 0.0f;
        glm::vec3 position{0.0f};
        float attenuatedVolume = 0.0f;
    };

    bool Initialize() override;
    SoundHandle LoadSound(const std::string& path) override;
    MusicHandle LoadMusic(const std::string& path) override;
    void PlaySound(SoundHandle handle, float volume, float pitch, const glm::vec3& spatialPos) override;
    void PlayMusic(MusicHandle handle, bool loop, float fadeTimeSeconds) override;
    void SetMasterVolume(float volume) override;
    void SetMusicVolume(float volume) override;
    void SetSFXVolume(float volume) override;
    void Shutdown() override;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] float GetMasterVolume() const noexcept;
    [[nodiscard]] float GetMusicVolume() const noexcept;
    [[nodiscard]] float GetSFXVolume() const noexcept;
    [[nodiscard]] const PcmBuffer* GetSoundBuffer(SoundHandle handle) const noexcept;
    [[nodiscard]] const PcmBuffer* GetMusicBuffer(MusicHandle handle) const noexcept;
    [[nodiscard]] const std::vector<PlaybackEvent>& GetPlaybackHistory() const noexcept;

private:
    bool m_initialized = false;
    float m_masterVolume = 1.0f;
    float m_musicVolume = 1.0f;
    float m_sfxVolume = 1.0f;
    std::vector<PcmBuffer> m_sounds;
    std::vector<PcmBuffer> m_music;
    std::vector<PlaybackEvent> m_playbackHistory;
};

} // namespace voxels
