/**
 * @file audio_engine.cpp
 * @brief Callback-safe mixing, WAV decoding, spatial playback, and SDL output.
 */
#include "voxels/audio/audio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>

#include <SDL.h>

namespace voxels {
namespace {
constexpr float kTwoPi = 6.283185307179586476925f;
constexpr float kInt16Scale = 1.0f / 32768.0f;
[[nodiscard]] std::uint16_t ReadU16(const std::uint8_t* bytes) noexcept { return static_cast<std::uint16_t>(bytes[0]) | (static_cast<std::uint16_t>(bytes[1]) << 8U); }
[[nodiscard]] std::uint32_t ReadU32(const std::uint8_t* bytes) noexcept { return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U); }
[[nodiscard]] std::size_t CategoryIndex(AudioCategory category) noexcept { return static_cast<std::size_t>(category); }
constexpr std::array<int, 89> kImaStepTable{{
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
}};
constexpr std::array<int, 16> kImaIndexTable{{-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8}};
int DecodeImaNibble(int nibble, int& predictor, int& stepIndex) noexcept {
    const int step = kImaStepTable[static_cast<std::size_t>(stepIndex)];
    int difference = step >> 3;
    if ((nibble & 1) != 0) difference += step >> 2;
    if ((nibble & 2) != 0) difference += step >> 1;
    if ((nibble & 4) != 0) difference += step;
    predictor += (nibble & 8) != 0 ? -difference : difference;
    predictor = std::clamp(predictor, -32768, 32767);
    stepIndex = std::clamp(stepIndex + kImaIndexTable[static_cast<std::size_t>(nibble)], 0, 88);
    return predictor;
}
PcmBuffer GenerateWave(float frequencyHz, float seconds, std::uint32_t sampleRate, float amplitude, bool square) {
    PcmBuffer result;
    result.sampleRate = sampleRate;
    result.samples.resize(static_cast<std::size_t>(std::max(0.0f, seconds) * static_cast<float>(sampleRate)));
    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const float phase = kTwoPi * frequencyHz * static_cast<float>(index) / static_cast<float>(sampleRate);
        const float value = square ? (std::sin(phase) >= 0.0f ? 1.0f : -1.0f) : std::sin(phase);
        result.samples[index] = static_cast<std::int16_t>(value * std::clamp(amplitude, 0.0f, 1.0f) * 32767.0f);
    }
    return result;
}
PcmBuffer FallbackFor(const std::filesystem::path& path) { return GenerateSineWaveTone(180.0f + static_cast<float>(std::hash<std::string>{}(path.string()) % 280U), 0.18f, 44100, 0.22f); }
void WriteU16(std::ofstream& stream, std::uint16_t value) { stream.put(static_cast<char>(value & 0xffU)); stream.put(static_cast<char>((value >> 8U) & 0xffU)); }
void WriteU32(std::ofstream& stream, std::uint32_t value) { WriteU16(stream, static_cast<std::uint16_t>(value & 0xffffU)); WriteU16(stream, static_cast<std::uint16_t>(value >> 16U)); }
bool WriteWavFile(const std::filesystem::path& path, const PcmBuffer& clip) {
    if (clip.samples.empty() || clip.channels != 1) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    const std::uint32_t byteCount = static_cast<std::uint32_t>(clip.samples.size() * sizeof(std::int16_t));
    stream.write("RIFF", 4); WriteU32(stream, 36U + byteCount); stream.write("WAVEfmt ", 8); WriteU32(stream, 16);
    WriteU16(stream, 1); WriteU16(stream, 1); WriteU32(stream, clip.sampleRate); WriteU32(stream, clip.sampleRate * 2U);
    WriteU16(stream, 2); WriteU16(stream, 16); stream.write("data", 4); WriteU32(stream, byteCount);
    stream.write(reinterpret_cast<const char*>(clip.samples.data()), static_cast<std::streamsize>(byteCount));
    return static_cast<bool>(stream);
}
PcmBuffer ForgeTone(float startFrequency, float endFrequency, float duration, float amplitude) {
    PcmBuffer clip;
    clip.samples.resize(static_cast<std::size_t>(duration * 44100.0f));
    std::uint32_t random = static_cast<std::uint32_t>(startFrequency * 97.0f);
    for (std::size_t index = 0; index < clip.samples.size(); ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(clip.samples.size());
        const float frequency = startFrequency + (endFrequency - startFrequency) * t;
        random = random * 1664525U + 1013904223U;
        const float noise = static_cast<float>((random >> 16U) & 0xffffU) / 32767.5f - 1.0f;
        const float envelope = std::sin(std::min(1.0f, t * 14.0f) * 1.5707963f) * (1.0f - t);
        const float wave = std::sin(kTwoPi * frequency * static_cast<float>(index) / 44100.0f);
        clip.samples[index] = static_cast<std::int16_t>((wave * 0.65f + noise * 0.35f) * envelope * amplitude * 32767.0f);
    }
    return clip;
}
} // namespace

PcmBuffer GenerateSineWaveTone(float frequencyHz, float seconds, std::uint32_t rate, float amplitude) { return GenerateWave(frequencyHz, seconds, rate, amplitude, false); }
PcmBuffer GenerateSquareWaveTone(float frequencyHz, float seconds, std::uint32_t rate, float amplitude) { return GenerateWave(frequencyHz, seconds, rate, amplitude, true); }
bool DecodeWavFile(const std::filesystem::path& path, PcmBuffer& output, std::string* error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { if (error != nullptr) *error = "could not open WAV"; return false; }
    const std::streamsize size = file.tellg();
    if (size < 44) { if (error != nullptr) *error = "WAV too small"; return false; }
    file.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size) || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) { if (error != nullptr) *error = "invalid RIFF/WAVE"; return false; }
    std::uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0, samplesPerBlock = 0;
    std::uint32_t rate = 0;
    const std::uint8_t* data = nullptr;
    std::size_t dataSize = 0;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const std::uint32_t chunkSize = ReadU32(bytes.data() + offset + 4);
        const std::size_t end = offset + 8U + chunkSize;
        if (end > bytes.size()) { if (error != nullptr) *error = "truncated WAV"; return false; }
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && chunkSize >= 16) {
            format = ReadU16(bytes.data() + offset + 8); channels = ReadU16(bytes.data() + offset + 10); rate = ReadU32(bytes.data() + offset + 12);
            blockAlign = ReadU16(bytes.data() + offset + 20); bits = ReadU16(bytes.data() + offset + 22);
            if (format == 0x11 && chunkSize >= 20) samplesPerBlock = ReadU16(bytes.data() + offset + 26);
        }
        else if (std::memcmp(bytes.data() + offset, "data", 4) == 0) { data = bytes.data() + offset + 8; dataSize = chunkSize; }
        offset = end + (chunkSize & 1U);
    }
    if (data == nullptr || (channels != 1 && channels != 2) || rate == 0 || dataSize == 0) { if (error != nullptr) *error = "WAV must be mono or stereo PCM/IMA ADPCM"; return false; }
    output = {};
    output.sampleRate = rate;
    output.channels = static_cast<std::uint8_t>(channels);
    if (format == 1 && bits == 16) {
        output.samples.resize(dataSize / sizeof(std::int16_t));
        std::memcpy(output.samples.data(), data, dataSize);
    } else if (format == 0x11 && bits == 4 && blockAlign >= static_cast<std::uint16_t>(4U * channels) && samplesPerBlock > 0) {
        const std::size_t blockCount = (dataSize + blockAlign - 1U) / blockAlign;
        output.samples.reserve(blockCount * static_cast<std::size_t>(samplesPerBlock) * channels);
        for (std::size_t block = 0; block < blockCount; ++block) {
            const std::size_t blockOffset = block * blockAlign;
            const std::size_t blockBytes = std::min<std::size_t>(blockAlign, dataSize - std::min(blockOffset, dataSize));
            if (blockBytes < static_cast<std::size_t>(4U * channels)) break;
            std::array<int, 2> predictors{};
            std::array<int, 2> stepIndices{};
            std::array<std::vector<std::int16_t>, 2> decodedChannels;
            for (std::uint16_t channel = 0; channel < channels; ++channel) {
                const std::uint8_t* header = data + blockOffset + channel * 4U;
                predictors[channel] = static_cast<std::int16_t>(ReadU16(header));
                stepIndices[channel] = std::clamp<int>(header[2], 0, 88);
                decodedChannels[channel].reserve(samplesPerBlock);
                decodedChannels[channel].push_back(static_cast<std::int16_t>(predictors[channel]));
            }
            const std::size_t payloadOffset = 4U * channels;
            const std::size_t groups = (blockBytes - payloadOffset) / (4U * channels);
            for (std::size_t group = 0; group < groups; ++group) {
                for (std::uint16_t channel = 0; channel < channels; ++channel) {
                    const std::uint8_t* encoded = data + blockOffset + payloadOffset + group * 4U * channels + channel * 4U;
                    for (std::size_t byte = 0; byte < 4; ++byte) {
                        const std::uint8_t packed = encoded[byte];
                        if (decodedChannels[channel].size() < samplesPerBlock) decodedChannels[channel].push_back(static_cast<std::int16_t>(DecodeImaNibble(packed & 0x0f, predictors[channel], stepIndices[channel])));
                        if (decodedChannels[channel].size() < samplesPerBlock) decodedChannels[channel].push_back(static_cast<std::int16_t>(DecodeImaNibble(packed >> 4, predictors[channel], stepIndices[channel])));
                    }
                }
            }
            const std::size_t decodedFrames = decodedChannels[0].size();
            for (std::size_t frame = 0; frame < decodedFrames; ++frame) {
                for (std::uint16_t channel = 0; channel < channels; ++channel) output.samples.push_back(decodedChannels[channel][frame]);
            }
        }
    } else {
        if (error != nullptr) *error = "WAV must be 16-bit PCM or 4-bit IMA ADPCM";
        return false;
    }
    return true;
}
std::size_t ForgeDefaultAudioAssets(const std::filesystem::path& assetRoot, bool overwrite) {
    struct AssetSpec { const char* name; float start; float end; float duration; float gain; };
    constexpr std::array<AssetSpec, 28> assets{{
        {"break_stone", 130.0f, 62.0f, 0.38f, 0.48f}, {"break_dirt", 260.0f, 110.0f, 0.30f, 0.38f},
        {"break_grass", 520.0f, 260.0f, 0.22f, 0.26f}, {"break_sand", 850.0f, 380.0f, 0.26f, 0.26f},
        {"break_gravel", 380.0f, 150.0f, 0.30f, 0.34f},
        {"break_wood", 180.0f, 95.0f, 0.32f, 0.42f}, {"break_glass", 1900.0f, 760.0f, 0.20f, 0.22f},
        {"place_stone", 115.0f, 90.0f, 0.10f, 0.40f}, {"place_dirt", 200.0f, 150.0f, 0.10f, 0.30f},
        {"place_grass", 340.0f, 280.0f, 0.10f, 0.25f}, {"place_sand", 700.0f, 450.0f, 0.10f, 0.20f},
        {"place_gravel", 300.0f, 170.0f, 0.10f, 0.28f},
        {"place_wood", 150.0f, 125.0f, 0.10f, 0.35f}, {"place_glass", 1450.0f, 1100.0f, 0.10f, 0.18f},
        {"step_stone", 105.0f, 82.0f, 0.13f, 0.26f}, {"step_dirt", 190.0f, 130.0f, 0.13f, 0.22f},
        {"step_grass", 390.0f, 250.0f, 0.13f, 0.16f}, {"step_sand", 760.0f, 450.0f, 0.13f, 0.18f},
        {"step_gravel", 330.0f, 190.0f, 0.13f, 0.24f}, {"step_glass", 1380.0f, 980.0f, 0.13f, 0.15f},
        {"step_wood", 140.0f, 105.0f, 0.13f, 0.26f}, {"step_water", 620.0f, 210.0f, 0.19f, 0.18f},
        {"jump", 280.0f, 420.0f, 0.14f, 0.22f}, {"land", 120.0f, 65.0f, 0.18f, 0.35f},
        {"splash", 780.0f, 160.0f, 0.42f, 0.24f}, {"ui_click", 820.0f, 1200.0f, 0.08f, 0.16f},
        {"ui_hover", 920.0f, 1100.0f, 0.05f, 0.08f}, {"menu_theme", 220.0f, 246.0f, 8.0f, 0.08f},
    }};
    std::error_code error;
    std::filesystem::create_directories(assetRoot, error);
    if (error) return 0;
    std::size_t written = 0;
    for (const AssetSpec& asset : assets) {
        const std::filesystem::path target = assetRoot / (std::string(asset.name) + ".wav");
        if (!overwrite && std::filesystem::exists(target)) continue;
        if (WriteWavFile(target, ForgeTone(asset.start, asset.end, asset.duration, asset.gain))) ++written;
    }
    return written;
}
float CalculateSpatialAttenuation(float distance, float reference, float rolloff) noexcept { const float ref = std::max(reference, 0.001f); return distance <= ref ? 1.0f : ref / (ref + std::max(rolloff, 0.0f) * (distance - ref)); }
AudioSpatialParams CalculateSpatialParams(const glm::vec3& listener, const glm::vec3& forward, const glm::vec3& sound, float maxDistance) noexcept {
    const glm::vec3 delta = sound - listener;
    const float distance = glm::length(delta);
    if (distance >= std::max(maxDistance, 0.001f)) return {.gain = 0.0f, .pan = 0.0f, .audible = false};
    const glm::vec3 direction = distance > 0.0001f ? delta / distance : glm::vec3{0.0f};
    const glm::vec3 unitForward = glm::length(forward) > 0.0001f ? glm::normalize(forward) : glm::vec3{0.0f, 0.0f, -1.0f};
    return {.gain = CalculateSpatialAttenuation(distance), .pan = std::clamp(glm::dot(direction, glm::normalize(glm::vec3{-unitForward.z, 0.0f, unitForward.x})), -1.0f, 1.0f), .audible = true};
}
SoundHandle AudioMixer::AddClip(PcmBuffer clip, AudioCategory category) { if (clip.samples.empty() || clip.channels == 0) return {}; m_clips.push_back({std::move(clip), category}); return {static_cast<std::uint32_t>(m_clips.size())}; }
VoiceHandle AudioMixer::Play(SoundHandle sound, float gain, float pitch, float pan, bool loop) noexcept {
    if (!sound.IsValid() || sound.id > m_clips.size()) return {};
    const std::size_t write = m_commandWrite.load(std::memory_order_relaxed), next = (write + 1U) % kCommandCapacity;
    if (next == m_commandRead.load(std::memory_order_acquire)) return {};
    const std::size_t firstSlot = m_nextVoiceSlot.fetch_add(1U, std::memory_order_relaxed) % kMaxVoices;
    std::size_t slot = kMaxVoices;
    for (std::size_t offset = 0; offset < kMaxVoices; ++offset) {
        const std::size_t candidate = (firstSlot + offset) % kMaxVoices;
        bool available = false;
        if (m_voiceReserved[candidate].compare_exchange_strong(available, true, std::memory_order_acq_rel)) {
            slot = candidate;
            break;
        }
    }
    if (slot == kMaxVoices) {
        const auto category = static_cast<std::uint8_t>(m_clips[sound.id - 1U].category);
        for (std::size_t offset = 0; offset < kMaxVoices; ++offset) {
            const std::size_t candidate = (firstSlot + offset) % kMaxVoices;
            if (m_voiceCategories[candidate].load(std::memory_order_acquire) == category) {
                slot = candidate;
                break;
            }
        }
        if (slot == kMaxVoices) return {};
    }
    m_voiceCategories[slot].store(static_cast<std::uint8_t>(m_clips[sound.id - 1U].category),
                                  std::memory_order_release);
    const std::uint32_t generation = m_voiceGenerations[slot].fetch_add(1U, std::memory_order_relaxed) + 1U;
    m_commands[write] = {CommandType::Play, static_cast<std::uint32_t>(slot), generation, sound.id,
                         std::clamp(gain, 0.0f, 2.0f), std::clamp(pitch, 0.25f, 4.0f), std::clamp(pan, -1.0f, 1.0f), loop, 0};
    m_commandWrite.store(next, std::memory_order_release);
    return {static_cast<std::uint32_t>(slot), generation};
}
void AudioMixer::SetVoiceEnvelope(VoiceHandle voice, float targetGain, float durationSeconds) noexcept {
    if (!voice.IsValid()) return;
    const std::size_t write = m_commandWrite.load(std::memory_order_relaxed), next = (write + 1U) % kCommandCapacity;
    if (next == m_commandRead.load(std::memory_order_acquire)) return;
    const auto frames = static_cast<std::uint32_t>(std::max(0.0f, durationSeconds) * static_cast<float>(m_outputSampleRate.load(std::memory_order_relaxed)));
    m_commands[write] = {CommandType::Envelope, voice.slot, voice.generation, 0, std::clamp(targetGain, 0.0f, 2.0f), 1.0f, 0.0f, false, frames};
    m_commandWrite.store(next, std::memory_order_release);
}
void AudioMixer::StopVoice(VoiceHandle voice, float fadeOutSeconds) noexcept { SetVoiceEnvelope(voice, 0.0f, fadeOutSeconds); }
void AudioMixer::DrainCommands() noexcept {
    std::size_t read = m_commandRead.load(std::memory_order_relaxed);
    const std::size_t write = m_commandWrite.load(std::memory_order_acquire);
    while (read != write) {
        const Command& command = m_commands[read];
        Voice& voice = m_voices[command.slot % kMaxVoices];
        if (command.type == CommandType::Play) {
            voice = {command.clipId, command.generation, 0.0f, command.gain, command.gain, 0, command.pitch, command.pan, command.loop, true};
        } else if (voice.generation == command.generation && voice.active) {
            voice.targetGain = command.gain;
            voice.envelopeFramesRemaining = command.envelopeFrames;
            if (command.envelopeFrames == 0) voice.gain = command.gain;
        }
        read = (read + 1U) % kCommandCapacity;
    }
    m_commandRead.store(read, std::memory_order_release);
}
float AudioMixer::CategoryGain(AudioCategory category) const noexcept { return m_categoryGains[CategoryIndex(category)].load(std::memory_order_relaxed); }
void AudioMixer::Mix(float* output, std::size_t frames) noexcept {
    if (output == nullptr) return;
    std::fill_n(output, frames * 2U, 0.0f); DrainCommands();
    for (Voice& voice : m_voices) {
        if (!voice.active || voice.clipId == 0 || voice.clipId > m_clips.size()) continue;
        const Clip& clip = m_clips[voice.clipId - 1U]; const std::size_t clipFrames = clip.pcm.samples.size() / clip.pcm.channels;
        const float increment = voice.pitch * static_cast<float>(clip.pcm.sampleRate) / static_cast<float>(m_outputSampleRate.load(std::memory_order_relaxed));
        for (std::size_t frame = 0; frame < frames && voice.active; ++frame) {
            if (voice.envelopeFramesRemaining > 0) {
                voice.gain += (voice.targetGain - voice.gain) / static_cast<float>(voice.envelopeFramesRemaining);
                --voice.envelopeFramesRemaining;
            }
            if (static_cast<std::size_t>(voice.cursor) >= clipFrames) { if (!voice.loop) { voice.active = false; m_voiceReserved[&voice - m_voices.data()].store(false, std::memory_order_release); break; } voice.cursor = 0.0f; }
            const std::size_t index = static_cast<std::size_t>(voice.cursor) * clip.pcm.channels;
            const float sample = clip.pcm.channels == 1 ? static_cast<float>(clip.pcm.samples[index]) * kInt16Scale : (static_cast<float>(clip.pcm.samples[index]) + static_cast<float>(clip.pcm.samples[index + 1U])) * (0.5f * kInt16Scale);
            const float gain = voice.gain * CategoryGain(clip.category) * m_masterGain.load(std::memory_order_relaxed);
            output[frame * 2U] += sample * gain * std::sqrt(0.5f * (1.0f - voice.pan));
            output[frame * 2U + 1U] += sample * gain * std::sqrt(0.5f * (1.0f + voice.pan));
            voice.cursor += increment;
            if (voice.envelopeFramesRemaining == 0 && voice.targetGain <= 0.0f && voice.gain <= 0.0001f) {
                voice.active = false;
                m_voiceReserved[&voice - m_voices.data()].store(false, std::memory_order_release);
            }
        }
    }
    for (std::size_t index = 0; index < frames * 2U; ++index) output[index] /= 1.0f + std::abs(output[index]);
}
void AudioMixer::SetCategoryGain(AudioCategory category, float gain) noexcept { m_categoryGains[CategoryIndex(category)].store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed); }
void AudioMixer::SetMasterGain(float gain) noexcept { m_masterGain.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed); }
void AudioMixer::SetOutputSampleRate(std::uint32_t sampleRate) noexcept { m_outputSampleRate.store(std::max(sampleRate, 1U), std::memory_order_relaxed); }
std::size_t AudioMixer::ActiveVoiceCount() const noexcept { return static_cast<std::size_t>(std::count_if(m_voices.begin(), m_voices.end(), [](const Voice& voice) { return voice.active; })); }
const PcmBuffer* AudioMixer::GetClip(SoundHandle sound) const noexcept { return !sound.IsValid() || sound.id > m_clips.size() ? nullptr : &m_clips[sound.id - 1U].pcm; }
SDLAudioDevice::~SDLAudioDevice() { Close(); }
void SDLAudioDevice::AudioCallback(void* userdata, std::uint8_t* stream, int bytes) { if (userdata != nullptr && stream != nullptr && bytes > 0) static_cast<AudioMixer*>(userdata)->Mix(reinterpret_cast<float*>(stream), static_cast<std::size_t>(bytes) / (sizeof(float) * 2U)); }
bool SDLAudioDevice::Open(AudioMixer& mixer, std::string& error) {
    Close();
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0U) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            error = SDL_GetError();
            return false;
        }
        m_initializedAudioSubsystem = true;
    }
    SDL_AudioSpec requested{};
    requested.freq = 44100;
    requested.format = AUDIO_F32SYS;
    requested.channels = 2;
    requested.samples = 1024;
    requested.callback = AudioCallback;
    requested.userdata = &mixer;
    SDL_AudioSpec obtained{};
    m_deviceId = SDL_OpenAudioDevice(nullptr, 0, &requested, &obtained, 0);
    if (m_deviceId == 0) {
        error = SDL_GetError();
        if (m_initializedAudioSubsystem) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            m_initializedAudioSubsystem = false;
        }
        return false;
    }
    mixer.SetOutputSampleRate(static_cast<std::uint32_t>(obtained.freq));
    SDL_PauseAudioDevice(m_deviceId, 0);
    return true;
}
void SDLAudioDevice::Close() noexcept {
    if (m_deviceId != 0) SDL_CloseAudioDevice(m_deviceId);
    m_deviceId = 0;
    if (m_initializedAudioSubsystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_initializedAudioSubsystem = false;
    }
}
bool SDLAudioDevice::IsOpen() const noexcept { return m_deviceId != 0; }
bool AudioEngine::Initialize(std::string& error) { m_audible = m_device.Open(m_mixer, error); return m_audible; }
SoundHandle AudioEngine::LoadSound(const std::filesystem::path& path, AudioCategory category) { PcmBuffer clip; if (!DecodeWavFile(path, clip)) clip = FallbackFor(path); return m_mixer.AddClip(std::move(clip), category); }
MusicHandle AudioEngine::LoadMusic(const std::filesystem::path& path, AudioCategory category) { return {LoadSound(path, category).id}; }
void AudioEngine::PlaySound(SoundHandle sound, float gain, float pitch, const glm::vec3& position, bool loop) noexcept { const AudioSpatialParams spatial = CalculateSpatialParams(m_listenerPosition, m_listenerForward, position); if (spatial.audible) (void)m_mixer.Play(sound, gain * spatial.gain, pitch, spatial.pan, loop); }
VoiceHandle AudioEngine::PlayMusic(MusicHandle music, bool loop, float initialGain) noexcept { return m_mixer.Play({music.id}, initialGain, 1.0f, 0.0f, loop); }
void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward) noexcept { m_listenerPosition = position; m_listenerForward = forward; }
void AudioEngine::ApplyVolumes(float master, float music, float sfx, float ambience) noexcept { m_mixer.SetMasterGain(master); m_mixer.SetCategoryGain(AudioCategory::Music, music); m_mixer.SetCategoryGain(AudioCategory::Sfx, sfx); m_mixer.SetCategoryGain(AudioCategory::Ambience, ambience); m_mixer.SetCategoryGain(AudioCategory::Ui, sfx); }
void AudioEngine::Shutdown() noexcept { m_device.Close(); m_audible = false; }
} // namespace voxels