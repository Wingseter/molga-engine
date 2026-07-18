#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

struct ma_engine;
struct ma_sound;

// Custom deleters remain public for source compatibility with older engine
// integrations. Runtime components no longer own ma_sound directly.
struct MaEngineDeleter {
    void operator()(ma_engine* engine);
};

struct MaSoundDeleter {
    void operator()(ma_sound* sound);
};

enum class AudioBus : std::uint8_t {
    Master = 0,
    Music,
    SFX,
    Voice,
    UI,
    Count
};

const char* AudioBusName(AudioBus bus);
bool TryParseAudioBus(const std::string& name, AudioBus& busOut);

enum class AudioLoadMode : std::uint8_t {
    DecodeOnLoad = 0,
    Streaming
};

struct VoiceHandle {
    static constexpr std::uint32_t InvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = InvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const { return index != InvalidIndex && generation != 0; }
    explicit operator bool() const { return IsValid(); }

    bool operator==(const VoiceHandle& other) const {
        return index == other.index && generation == other.generation;
    }
    bool operator!=(const VoiceHandle& other) const { return !(*this == other); }
};

// Owns the miniaudio engine, fixed mixer hierarchy and every live voice. A
// component only stores VoiceHandle, so service shutdown can always release
// voices before the engine and stale handles cannot bind to recycled slots.
class AudioService {
public:
    static AudioService& Get();

    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;

    bool Init(bool noDevice = false);
    bool InitNoDevice() { return Init(true); }
    void Shutdown();
    void Update(float dt);

    bool IsInitialized() const;
    bool IsNoDeviceBackend() const;

    VoiceHandle CreateVoiceFromFile(
        const std::string& filepath,
        AudioBus bus = AudioBus::SFX,
        AudioLoadMode loadMode = AudioLoadMode::DecodeOnLoad);
    VoiceHandle CreateVoiceFromGuid(
        const std::string& guid,
        AudioBus bus = AudioBus::SFX,
        AudioLoadMode fallbackMode = AudioLoadMode::DecodeOnLoad);

    VoiceHandle PlayOneShot(
        const std::string& guid,
        AudioBus bus = AudioBus::SFX,
        float volume = 1.0f,
        float pitch = 1.0f);
    VoiceHandle PlayOneShotPath(
        const std::string& filepath,
        AudioBus bus = AudioBus::SFX,
        float volume = 1.0f,
        float pitch = 1.0f,
        AudioLoadMode loadMode = AudioLoadMode::DecodeOnLoad);

    VoiceHandle PlayMusic(const std::string& guid, bool loop = true,
                          float fadeInSeconds = 0.0f);
    VoiceHandle PlayMusicPath(const std::string& filepath, bool loop = true,
                              float fadeInSeconds = 0.0f,
                              AudioLoadMode loadMode = AudioLoadMode::Streaming);
    VoiceHandle CrossFadeMusic(const std::string& guid, float durationSeconds,
                               bool loop = true);
    VoiceHandle CrossFadeMusicPath(
        const std::string& filepath, float durationSeconds, bool loop = true,
        AudioLoadMode loadMode = AudioLoadMode::Streaming);
    void StopMusic(float fadeOutSeconds = 0.0f);
    void PauseMusic();
    void ResumeMusic();
    bool IsMusicPlaying() const;
    VoiceHandle CurrentMusicHandle() const;

    bool PlayVoice(VoiceHandle handle);
    bool StopVoice(VoiceHandle handle);
    bool PauseVoice(VoiceHandle handle);
    bool ResumeVoice(VoiceHandle handle);
    bool ReleaseVoice(VoiceHandle handle);
    bool IsVoiceValid(VoiceHandle handle) const;
    bool IsVoicePlaying(VoiceHandle handle) const;

    bool SetVoiceVolume(VoiceHandle handle, float volume);
    float GetVoiceVolume(VoiceHandle handle) const;
    bool SetVoicePitch(VoiceHandle handle, float pitch);
    bool SetVoiceLooping(VoiceHandle handle, bool loop);
    bool SetVoiceSpatial(VoiceHandle handle, bool spatial);
    bool SetVoiceMinDistance(VoiceHandle handle, float distance);
    bool SetVoiceMaxDistance(VoiceHandle handle, float distance);
    bool SetVoicePosition(VoiceHandle handle, float x, float y, float z = 0.0f);
    AudioBus GetVoiceBus(VoiceHandle handle) const;

    void SetBusVolume(AudioBus bus, float volume);
    float GetBusVolume(AudioBus bus) const;
    void SetBusMuted(AudioBus bus, bool muted);
    bool IsBusMuted(AudioBus bus) const;
    float GetEffectiveBusGain(AudioBus bus) const;
    void FadeBus(AudioBus bus, float targetVolume, float durationSeconds);
    void ApplyProjectSettings();

    std::size_t LiveVoiceCount() const;
    ma_engine* GetEngine();
    const ma_engine* GetEngine() const;

private:
    AudioService();
    ~AudioService();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Backward-compatible static facade. Legacy path/name APIs now delegate to the
// central service, so repeated PlaySound calls create independent one-shots.
class Audio {
public:
    static bool Init(bool noDevice = false);
    static bool InitNoDevice();
    static void Shutdown();
    static void Update(float dt);

    static bool LoadSound(const std::string& name, const std::string& filepath);
    static void PlaySound(const std::string& name, float volume = 1.0f);
    static void StopSound(const std::string& name);

    static bool LoadMusic(const std::string& filepath);
    static void PlayMusic(bool loop = true);
    static VoiceHandle PlayMusic(const std::string& guid, bool loop = true,
                                 float fadeInSeconds = 0.0f);
    // Prevent a string literal from selecting the legacy PlayMusic(bool)
    // overload through its standard pointer-to-bool conversion.
    static VoiceHandle PlayMusic(const char* guid, bool loop = true,
                                 float fadeInSeconds = 0.0f) {
        return PlayMusic(std::string(guid ? guid : ""), loop, fadeInSeconds);
    }
    static VoiceHandle CrossFadeMusic(const std::string& guid,
                                      float durationSeconds, bool loop = true);
    static void StopMusic(float fadeOutSeconds = 0.0f);
    static void PauseMusic();
    static void ResumeMusic();
    static void SetMusicVolume(float volume);
    static bool IsMusicPlaying();

    static VoiceHandle PlayOneShot(const std::string& guid,
                                   AudioBus bus = AudioBus::SFX,
                                   float volume = 1.0f,
                                   float pitch = 1.0f);

    static void SetBusVolume(AudioBus bus, float volume);
    static float GetBusVolume(AudioBus bus);
    static void SetBusMuted(AudioBus bus, bool muted);
    static bool IsBusMuted(AudioBus bus);
    static void FadeBus(AudioBus bus, float targetVolume, float durationSeconds);
    static void ApplyProjectSettings();

    static void SetMasterVolume(float volume);
    static float GetMasterVolume();

    static std::size_t LiveVoiceCount();
    static ma_engine* GetEngine();
};
