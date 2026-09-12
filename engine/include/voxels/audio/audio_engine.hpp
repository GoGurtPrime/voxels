/**
 * @file audio_engine.hpp
 * @brief SDL-backed audio output, fixed-voice mixing, WAV decoding, and spatial playback.
 *
 * @details Implements the audio flow in DIAGRAMS.md section 13. The main thread owns clips and
 * submits commands; the audio callback consumes commands and mixes without allocation or locks.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace voxels {

struct SoundHandle { std::uint32_t id = 0; [[nodiscard]] bool IsValid() const noexcept { return id != 0; } };
struct MusicHandle { std::uint32_t id = 0; [[nodiscard]] bool IsValid() const noexcept { return id != 0; } };
struct VoiceHandle {
    static constexpr std::uint32_t kInvalidSlot = UINT32_MAX;
    std::uint32_t slot = kInvalidSlot;
    std::uint32_t generation = 0;
    [[nodiscard]] bool IsValid() const noexcept { return slot != kInvalidSlot && generation != 0; }
};
struct PcmBuffer { std::vector<std::int16_t> samples; std::uint32_t sampleRate = 44100; std::uint8_t channels = 1; };
enum class AudioCategory : std::uint8_t { Sfx, Music, Ambience, Ui };
struct AudioSpatialParams { float gain = 1.0f; float pan = 0.0f; bool audible = true; };

[[nodiscard]] PcmBuffer GenerateSineWaveTone(float frequencyHz, float durationSeconds, std::uint32_t sampleRate = 44100, float amplitude = 0.5f);
[[nodiscard]] PcmBuffer GenerateSquareWaveTone(float frequencyHz, float durationSeconds, std::uint32_t sampleRate = 44100, float amplitude = 0.5f);
[[nodiscard]] bool DecodeWavFile(const std::filesystem::path& path, PcmBuffer& output, std::string* error = nullptr);
[[nodiscard]] std::size_t ForgeDefaultAudioAssets(const std::filesystem::path& assetRoot, bool overwrite = false);
[[nodiscard]] float CalculateSpatialAttenuation(float distance, float referenceDistance = 1.0f, float rolloffFactor = 1.0f) noexcept;
[[nodiscard]] AudioSpatialParams CalculateSpatialParams(const glm::vec3& listenerPosition, const glm::vec3& listenerForward, const glm::vec3& soundPosition, float maxDistance = 32.0f) noexcept;

class AudioMixer final {
public:
    static constexpr std::size_t kMaxVoices = 32;
    SoundHandle AddClip(PcmBuffer clip, AudioCategory category);
    [[nodiscard]] VoiceHandle Play(SoundHandle sound, float gain = 1.0f, float pitch = 1.0f, float pan = 0.0f, bool loop = false) noexcept;
    void SetVoiceEnvelope(VoiceHandle voice, float targetGain, float durationSeconds) noexcept;
    void StopVoice(VoiceHandle voice, float fadeOutSeconds = 0.1f) noexcept;
    void Mix(float* stereoOutput, std::size_t frames) noexcept;
    void SetCategoryGain(AudioCategory category, float gain) noexcept;
    void SetMasterGain(float gain) noexcept;
    void SetOutputSampleRate(std::uint32_t sampleRate) noexcept;
    [[nodiscard]] std::size_t ActiveVoiceCount() const noexcept;
    [[nodiscard]] const PcmBuffer* GetClip(SoundHandle sound) const noexcept;

private:
    struct Clip { PcmBuffer pcm; AudioCategory category = AudioCategory::Sfx; };
    struct Voice { std::uint32_t clipId = 0; std::uint32_t generation = 0; float cursor = 0.0f; float gain = 0.0f; float targetGain = 0.0f; std::uint32_t envelopeFramesRemaining = 0; float pitch = 1.0f; float pan = 0.0f; bool loop = false; bool active = false; };
    enum class CommandType : std::uint8_t { Play, Envelope };
    struct Command { CommandType type = CommandType::Play; std::uint32_t slot = 0; std::uint32_t generation = 0; std::uint32_t clipId = 0; float gain = 1.0f; float pitch = 1.0f; float pan = 0.0f; bool loop = false; std::uint32_t envelopeFrames = 0; };
    static constexpr std::size_t kCommandCapacity = 128;
    void DrainCommands() noexcept;
    [[nodiscard]] float CategoryGain(AudioCategory category) const noexcept;
    std::vector<Clip> m_clips;
    std::array<Voice, kMaxVoices> m_voices{};
    std::array<Command, kCommandCapacity> m_commands{};
    std::array<std::atomic<std::uint32_t>, kMaxVoices> m_voiceGenerations{};
    std::array<std::atomic<bool>, kMaxVoices> m_voiceReserved{};
    std::array<std::atomic<std::uint8_t>, kMaxVoices> m_voiceCategories{};
    std::atomic<std::size_t> m_nextVoiceSlot{0};
    std::atomic<std::size_t> m_commandRead{0};
    std::atomic<std::size_t> m_commandWrite{0};
    std::atomic<float> m_masterGain{1.0f};
    std::array<std::atomic<float>, 4> m_categoryGains{{1.0f, 1.0f, 1.0f, 1.0f}};
    std::atomic<std::uint32_t> m_outputSampleRate{44100};
};

class IAudioDevice { public: virtual ~IAudioDevice() = default; virtual bool Open(AudioMixer& mixer, std::string& error) = 0; virtual void Close() noexcept = 0; [[nodiscard]] virtual bool IsOpen() const noexcept = 0; };
class NullAudioDevice final : public IAudioDevice { public: bool Open(AudioMixer&, std::string&) override { return true; } void Close() noexcept override {} [[nodiscard]] bool IsOpen() const noexcept override { return true; } };
class SDLAudioDevice final : public IAudioDevice {
public:
    ~SDLAudioDevice() override;
    bool Open(AudioMixer& mixer, std::string& error) override;
    void Close() noexcept override;
    [[nodiscard]] bool IsOpen() const noexcept override;
private:
    static void AudioCallback(void* userdata, std::uint8_t* stream, int byteCount);
    std::uint32_t m_deviceId = 0;
    bool m_initializedAudioSubsystem = false;
};

class AudioEngine final {
public:
    explicit AudioEngine(IAudioDevice& device) : m_device(device) {}
    bool Initialize(std::string& error);
    SoundHandle LoadSound(const std::filesystem::path& path, AudioCategory category = AudioCategory::Sfx);
    MusicHandle LoadMusic(const std::filesystem::path& path, AudioCategory category = AudioCategory::Music);
    void PlaySound(SoundHandle sound, float gain, float pitch, const glm::vec3& position, bool loop = false) noexcept;
    [[nodiscard]] VoiceHandle PlayMusic(MusicHandle music, bool loop = true, float initialGain = 1.0f) noexcept;
    void SetListener(const glm::vec3& position, const glm::vec3& forward) noexcept;
    void ApplyVolumes(float master, float music, float sfx, float ambience) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsAudible() const noexcept { return m_audible; }
    [[nodiscard]] AudioMixer& GetMixer() noexcept { return m_mixer; }
private:
    AudioMixer m_mixer;
    IAudioDevice& m_device;
    glm::vec3 m_listenerPosition{0.0f};
    glm::vec3 m_listenerForward{0.0f, 0.0f, -1.0f};
    bool m_audible = false;
};
} // namespace voxels