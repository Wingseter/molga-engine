#define MINIAUDIO_IMPLEMENTATION
#include "../../external/miniaudio/miniaudio.h"

#include "Audio.h"

#include "Core/AssetDatabase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kBusCount = static_cast<std::size_t>(AudioBus::Count);
static_assert(kBusCount == 5, "The P1 mixer has exactly five fixed buses");

std::size_t BusIndex(AudioBus bus) {
    const auto index = static_cast<std::size_t>(bus);
    return index < kBusCount ? index : 0;
}

float ClampVolume(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

float ClampPitch(float value) {
    if (!std::isfinite(value)) return 1.0f;
    return std::max(value, 0.01f);
}

float ClampDistance(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::max(value, 0.0f);
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

AudioLoadMode LoadModeFromRecord(const molga::AssetRecord* record,
                                 AudioLoadMode fallback) {
    if (!record || !record->settings.is_object()) return fallback;
    const std::string mode = record->settings.value("loadMode", std::string{});
    if (mode == "Streaming") return AudioLoadMode::Streaming;
    if (mode == "DecodeOnLoad") return AudioLoadMode::DecodeOnLoad;
    return fallback;
}

std::unordered_map<std::string, std::string> gLegacySoundPaths;
std::unordered_map<std::string, std::vector<VoiceHandle>> gLegacySoundVoices;
std::string gLegacyMusicPath;
float gLegacyMusicVolume = 1.0f;

} // namespace

void MaEngineDeleter::operator()(ma_engine* engine) {
    if (!engine) return;
    ma_engine_uninit(engine);
    delete engine;
}

void MaSoundDeleter::operator()(ma_sound* sound) {
    if (!sound) return;
    ma_sound_uninit(sound);
    delete sound;
}

const char* AudioBusName(AudioBus bus) {
    switch (bus) {
        case AudioBus::Master: return "Master";
        case AudioBus::Music: return "Music";
        case AudioBus::SFX: return "SFX";
        case AudioBus::Voice: return "Voice";
        case AudioBus::UI: return "UI";
        case AudioBus::Count: break;
    }
    return "Master";
}

bool TryParseAudioBus(const std::string& name, AudioBus& busOut) {
    const std::string normalized = Lowercase(name);
    if (normalized == "master") busOut = AudioBus::Master;
    else if (normalized == "music") busOut = AudioBus::Music;
    else if (normalized == "sfx") busOut = AudioBus::SFX;
    else if (normalized == "voice") busOut = AudioBus::Voice;
    else if (normalized == "ui") busOut = AudioBus::UI;
    else return false;
    return true;
}

struct AudioService::Impl {
    enum class VoiceKind {
        Persistent,
        OneShot,
        Music
    };

    struct FadeState {
        bool active = false;
        float start = 1.0f;
        float target = 1.0f;
        float elapsed = 0.0f;
        float duration = 0.0f;
        bool releaseWhenDone = false;
    };

    struct VoiceSlot {
        std::uint32_t generation = 1;
        std::unique_ptr<ma_sound, MaSoundDeleter> sound;
        AudioBus bus = AudioBus::SFX;
        VoiceKind kind = VoiceKind::Persistent;
        float volume = 1.0f;
        float fadeGain = 1.0f;
        bool paused = false;
        FadeState fade;
    };

    struct BusState {
        float volume = 1.0f;
        bool muted = false;
        FadeState fade;
    };

    std::unique_ptr<ma_engine, MaEngineDeleter> engine;
    std::array<ma_sound_group, kBusCount> groups{};
    std::array<bool, kBusCount> groupInitialized{};
    std::array<BusState, kBusCount> buses{};
    std::vector<VoiceSlot> voices;
    std::vector<std::uint32_t> freeSlots;
    VoiceHandle currentMusic;
    bool initialized = false;
    bool noDevice = false;

    ma_sound_group* Group(AudioBus bus) {
        const std::size_t index = BusIndex(bus);
        return groupInitialized[index] ? &groups[index] : nullptr;
    }

    VoiceSlot* Slot(VoiceHandle handle) {
        if (!handle.IsValid() || handle.index >= voices.size()) return nullptr;
        VoiceSlot& slot = voices[handle.index];
        if (!slot.sound || slot.generation != handle.generation) return nullptr;
        return &slot;
    }

    const VoiceSlot* Slot(VoiceHandle handle) const {
        if (!handle.IsValid() || handle.index >= voices.size()) return nullptr;
        const VoiceSlot& slot = voices[handle.index];
        if (!slot.sound || slot.generation != handle.generation) return nullptr;
        return &slot;
    }

    VoiceHandle Adopt(std::unique_ptr<ma_sound, MaSoundDeleter> sound,
                      AudioBus bus) {
        std::uint32_t index = 0;
        if (!freeSlots.empty()) {
            index = freeSlots.back();
            freeSlots.pop_back();
        } else {
            index = static_cast<std::uint32_t>(voices.size());
            voices.emplace_back();
        }

        VoiceSlot& slot = voices[index];
        slot.sound = std::move(sound);
        slot.bus = bus;
        slot.kind = VoiceKind::Persistent;
        slot.volume = 1.0f;
        slot.fadeGain = 1.0f;
        slot.paused = false;
        slot.fade = {};
        return {index, slot.generation};
    }

    void ApplyVoiceGain(VoiceSlot& slot) {
        if (slot.sound) {
            ma_sound_set_volume(slot.sound.get(),
                                std::max(0.0f, slot.volume * slot.fadeGain));
        }
    }

    void BeginVoiceFade(VoiceHandle handle, float target, float duration,
                        bool releaseWhenDone) {
        VoiceSlot* slot = Slot(handle);
        if (!slot) return;
        slot->fade.active = duration > 0.0f;
        slot->fade.start = slot->fadeGain;
        slot->fade.target = ClampVolume(target);
        slot->fade.elapsed = 0.0f;
        slot->fade.duration = std::max(duration, 0.0f);
        slot->fade.releaseWhenDone = releaseWhenDone;
        if (!slot->fade.active) {
            slot->fadeGain = slot->fade.target;
            ApplyVoiceGain(*slot);
        }
    }

    bool Release(VoiceHandle handle) {
        VoiceSlot* slot = Slot(handle);
        if (!slot) return false;
        ma_sound_stop(slot->sound.get());
        slot->sound.reset();
        slot->fade = {};
        slot->fadeGain = 1.0f;
        slot->paused = false;
        ++slot->generation;
        if (slot->generation == 0) ++slot->generation;
        freeSlots.push_back(handle.index);
        if (currentMusic == handle) currentMusic = {};
        return true;
    }

    void ApplyBusGain(AudioBus bus) {
        const std::size_t index = BusIndex(bus);
        if (groupInitialized[index]) {
            const float gain = buses[index].muted ? 0.0f : buses[index].volume;
            ma_sound_group_set_volume(&groups[index], gain);
        }
    }

    void UninitGroups() {
        for (std::size_t i = kBusCount; i > 0; --i) {
            const std::size_t index = i - 1;
            if (groupInitialized[index]) {
                ma_sound_group_uninit(&groups[index]);
                groupInitialized[index] = false;
            }
        }
    }
};

AudioService& AudioService::Get() {
    static AudioService service;
    return service;
}

AudioService::AudioService() : impl_(std::make_unique<Impl>()) {}

AudioService::~AudioService() {
    Shutdown();
}

bool AudioService::Init(bool noDevice) {
    if (impl_->initialized && impl_->noDevice == noDevice) return true;
    if (impl_->initialized) Shutdown();

    ma_engine_config config = ma_engine_config_init();
    if (noDevice) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = 48000;
    }

    auto rawEngine = std::make_unique<ma_engine>();
    if (ma_engine_init(&config, rawEngine.get()) != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
        return false;
    }
    impl_->engine = std::unique_ptr<ma_engine, MaEngineDeleter>(rawEngine.release());

    if (ma_sound_group_init(impl_->engine.get(), 0, nullptr,
                            &impl_->groups[BusIndex(AudioBus::Master)]) != MA_SUCCESS) {
        impl_->engine.reset();
        return false;
    }
    impl_->groupInitialized[BusIndex(AudioBus::Master)] = true;

    for (AudioBus bus : {AudioBus::Music, AudioBus::SFX,
                         AudioBus::Voice, AudioBus::UI}) {
        if (ma_sound_group_init(impl_->engine.get(), 0,
                                impl_->Group(AudioBus::Master),
                                &impl_->groups[BusIndex(bus)]) != MA_SUCCESS) {
            impl_->UninitGroups();
            impl_->engine.reset();
            return false;
        }
        impl_->groupInitialized[BusIndex(bus)] = true;
    }

    impl_->initialized = true;
    impl_->noDevice = noDevice;
    ma_engine_set_volume(impl_->engine.get(), 1.0f);
    ApplyProjectSettings();
    return true;
}

void AudioService::Shutdown() {
    if (!impl_->initialized && !impl_->engine) return;

    for (std::uint32_t index = 0; index < impl_->voices.size(); ++index) {
        Impl::VoiceSlot& slot = impl_->voices[index];
        if (slot.sound) impl_->Release({index, slot.generation});
    }
    impl_->currentMusic = {};
    impl_->UninitGroups();
    impl_->engine.reset();
    impl_->initialized = false;
    impl_->noDevice = false;
}

void AudioService::Update(float dt) {
    if (!impl_->initialized) return;
    const float delta = std::isfinite(dt) ? std::max(dt, 0.0f) : 0.0f;

    for (std::size_t i = 0; i < kBusCount; ++i) {
        Impl::BusState& state = impl_->buses[i];
        if (!state.fade.active) continue;
        state.fade.elapsed += delta;
        const float t = state.fade.duration <= 0.0f
            ? 1.0f : std::min(state.fade.elapsed / state.fade.duration, 1.0f);
        state.volume = state.fade.start + (state.fade.target - state.fade.start) * t;
        impl_->ApplyBusGain(static_cast<AudioBus>(i));
        if (t >= 1.0f) state.fade.active = false;
    }

    std::vector<VoiceHandle> releaseAfterUpdate;
    for (std::uint32_t index = 0; index < impl_->voices.size(); ++index) {
        Impl::VoiceSlot& slot = impl_->voices[index];
        if (!slot.sound || !slot.fade.active) continue;
        slot.fade.elapsed += delta;
        const float t = slot.fade.duration <= 0.0f
            ? 1.0f : std::min(slot.fade.elapsed / slot.fade.duration, 1.0f);
        slot.fadeGain = slot.fade.start + (slot.fade.target - slot.fade.start) * t;
        impl_->ApplyVoiceGain(slot);
        if (t >= 1.0f) {
            const bool release = slot.fade.releaseWhenDone;
            slot.fade.active = false;
            if (release) releaseAfterUpdate.push_back({index, slot.generation});
        }
    }

    if (impl_->noDevice && delta > 0.0f && impl_->engine) {
        const ma_uint32 channels = ma_engine_get_channels(impl_->engine.get());
        const ma_uint32 sampleRate = ma_engine_get_sample_rate(impl_->engine.get());
        ma_uint64 remaining = static_cast<ma_uint64>(
            std::ceil(delta * static_cast<float>(sampleRate)));
        constexpr ma_uint64 kChunkFrames = 1024;
        std::vector<float> scratch(static_cast<std::size_t>(kChunkFrames * channels));
        while (remaining > 0) {
            const ma_uint64 frames = std::min(remaining, kChunkFrames);
            ma_uint64 framesRead = 0;
            if (ma_engine_read_pcm_frames(impl_->engine.get(), scratch.data(),
                                          frames, &framesRead) != MA_SUCCESS) {
                break;
            }
            if (framesRead == 0) break;
            remaining -= framesRead;
        }
    }

    for (std::uint32_t index = 0; index < impl_->voices.size(); ++index) {
        Impl::VoiceSlot& slot = impl_->voices[index];
        if (!slot.sound || slot.paused || ma_sound_is_looping(slot.sound.get())) continue;
        if ((slot.kind == Impl::VoiceKind::OneShot ||
             slot.kind == Impl::VoiceKind::Music) &&
            ma_sound_at_end(slot.sound.get())) {
            releaseAfterUpdate.push_back({index, slot.generation});
        }
    }

    for (VoiceHandle handle : releaseAfterUpdate) impl_->Release(handle);
}

bool AudioService::IsInitialized() const { return impl_->initialized; }
bool AudioService::IsNoDeviceBackend() const { return impl_->noDevice; }

VoiceHandle AudioService::CreateVoiceFromFile(const std::string& filepath,
                                              AudioBus bus,
                                              AudioLoadMode loadMode) {
    if (!impl_->initialized || filepath.empty()) return {};
    bus = static_cast<AudioBus>(BusIndex(bus));

    auto rawSound = std::make_unique<ma_sound>();
    const ma_uint32 flags = loadMode == AudioLoadMode::Streaming
        ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (ma_sound_init_from_file(impl_->engine.get(), filepath.c_str(), flags,
                                impl_->Group(bus), nullptr,
                                rawSound.get()) != MA_SUCCESS) {
        return {};
    }

    ma_sound_set_spatialization_enabled(rawSound.get(), MA_FALSE);
    return impl_->Adopt(
        std::unique_ptr<ma_sound, MaSoundDeleter>(rawSound.release()), bus);
}

VoiceHandle AudioService::CreateVoiceFromGuid(const std::string& guid,
                                              AudioBus bus,
                                              AudioLoadMode fallbackMode) {
    const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
    if (!record || record->importer != "AudioImporter" || record->importFailed) return {};
    const std::filesystem::path source =
        molga::AssetDatabase::Get().AbsoluteSourcePath(guid);
    if (source.empty()) return {};
    return CreateVoiceFromFile(source.string(), bus,
                               LoadModeFromRecord(record, fallbackMode));
}

VoiceHandle AudioService::PlayOneShot(const std::string& guid, AudioBus bus,
                                      float volume, float pitch) {
    VoiceHandle handle = CreateVoiceFromGuid(guid, bus, AudioLoadMode::DecodeOnLoad);
    if (!handle) return {};
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    slot->kind = Impl::VoiceKind::OneShot;
    SetVoiceVolume(handle, volume);
    SetVoicePitch(handle, pitch);
    if (!PlayVoice(handle)) {
        ReleaseVoice(handle);
        return {};
    }
    return handle;
}

VoiceHandle AudioService::PlayOneShotPath(const std::string& filepath,
                                          AudioBus bus, float volume,
                                          float pitch, AudioLoadMode loadMode) {
    VoiceHandle handle = CreateVoiceFromFile(filepath, bus, loadMode);
    if (!handle) return {};
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    slot->kind = Impl::VoiceKind::OneShot;
    SetVoiceVolume(handle, volume);
    SetVoicePitch(handle, pitch);
    if (!PlayVoice(handle)) {
        ReleaseVoice(handle);
        return {};
    }
    return handle;
}

VoiceHandle AudioService::PlayMusic(const std::string& guid, bool loop,
                                    float fadeInSeconds) {
    VoiceHandle next = CreateVoiceFromGuid(guid, AudioBus::Music,
                                           AudioLoadMode::Streaming);
    if (!next) return {};
    if (IsVoiceValid(impl_->currentMusic)) ReleaseVoice(impl_->currentMusic);
    Impl::VoiceSlot* slot = impl_->Slot(next);
    slot->kind = Impl::VoiceKind::Music;
    SetVoiceLooping(next, loop);
    slot->fadeGain = fadeInSeconds > 0.0f ? 0.0f : 1.0f;
    impl_->ApplyVoiceGain(*slot);
    impl_->currentMusic = next;
    PlayVoice(next);
    if (fadeInSeconds > 0.0f) impl_->BeginVoiceFade(next, 1.0f, fadeInSeconds, false);
    return next;
}

VoiceHandle AudioService::PlayMusicPath(const std::string& filepath, bool loop,
                                        float fadeInSeconds,
                                        AudioLoadMode loadMode) {
    VoiceHandle next = CreateVoiceFromFile(filepath, AudioBus::Music, loadMode);
    if (!next) return {};
    if (IsVoiceValid(impl_->currentMusic)) ReleaseVoice(impl_->currentMusic);
    Impl::VoiceSlot* slot = impl_->Slot(next);
    slot->kind = Impl::VoiceKind::Music;
    SetVoiceLooping(next, loop);
    slot->fadeGain = fadeInSeconds > 0.0f ? 0.0f : 1.0f;
    impl_->ApplyVoiceGain(*slot);
    impl_->currentMusic = next;
    PlayVoice(next);
    if (fadeInSeconds > 0.0f) impl_->BeginVoiceFade(next, 1.0f, fadeInSeconds, false);
    return next;
}

VoiceHandle AudioService::CrossFadeMusic(const std::string& guid,
                                         float durationSeconds, bool loop) {
    VoiceHandle next = CreateVoiceFromGuid(guid, AudioBus::Music,
                                           AudioLoadMode::Streaming);
    if (!next) return {};
    const VoiceHandle previous = impl_->currentMusic;
    Impl::VoiceSlot* slot = impl_->Slot(next);
    slot->kind = Impl::VoiceKind::Music;
    SetVoiceLooping(next, loop);
    const float duration = std::max(durationSeconds, 0.0f);
    slot->fadeGain = duration > 0.0f ? 0.0f : 1.0f;
    impl_->ApplyVoiceGain(*slot);
    impl_->currentMusic = next;
    PlayVoice(next);
    if (duration > 0.0f) {
        impl_->BeginVoiceFade(next, 1.0f, duration, false);
        if (IsVoiceValid(previous)) impl_->BeginVoiceFade(previous, 0.0f, duration, true);
    } else if (IsVoiceValid(previous)) {
        ReleaseVoice(previous);
    }
    return next;
}

VoiceHandle AudioService::CrossFadeMusicPath(const std::string& filepath,
                                             float durationSeconds, bool loop,
                                             AudioLoadMode loadMode) {
    VoiceHandle next = CreateVoiceFromFile(filepath, AudioBus::Music, loadMode);
    if (!next) return {};
    const VoiceHandle previous = impl_->currentMusic;
    Impl::VoiceSlot* slot = impl_->Slot(next);
    slot->kind = Impl::VoiceKind::Music;
    SetVoiceLooping(next, loop);
    const float duration = std::max(durationSeconds, 0.0f);
    slot->fadeGain = duration > 0.0f ? 0.0f : 1.0f;
    impl_->ApplyVoiceGain(*slot);
    impl_->currentMusic = next;
    PlayVoice(next);
    if (duration > 0.0f) {
        impl_->BeginVoiceFade(next, 1.0f, duration, false);
        if (IsVoiceValid(previous)) impl_->BeginVoiceFade(previous, 0.0f, duration, true);
    } else if (IsVoiceValid(previous)) {
        ReleaseVoice(previous);
    }
    return next;
}

void AudioService::StopMusic(float fadeOutSeconds) {
    const VoiceHandle current = impl_->currentMusic;
    if (!IsVoiceValid(current)) return;
    if (fadeOutSeconds > 0.0f) {
        impl_->BeginVoiceFade(current, 0.0f, fadeOutSeconds, true);
    } else {
        ReleaseVoice(current);
    }
}

void AudioService::PauseMusic() { PauseVoice(impl_->currentMusic); }
void AudioService::ResumeMusic() { ResumeVoice(impl_->currentMusic); }
bool AudioService::IsMusicPlaying() const {
    return IsVoicePlaying(impl_->currentMusic);
}
VoiceHandle AudioService::CurrentMusicHandle() const { return impl_->currentMusic; }

bool AudioService::PlayVoice(VoiceHandle handle) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    if (ma_sound_at_end(slot->sound.get())) ma_sound_seek_to_pcm_frame(slot->sound.get(), 0);
    slot->paused = false;
    return ma_sound_start(slot->sound.get()) == MA_SUCCESS;
}

bool AudioService::StopVoice(VoiceHandle handle) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_stop(slot->sound.get());
    ma_sound_seek_to_pcm_frame(slot->sound.get(), 0);
    slot->paused = false;
    slot->fade = {};
    slot->fadeGain = 1.0f;
    impl_->ApplyVoiceGain(*slot);
    return true;
}

bool AudioService::PauseVoice(VoiceHandle handle) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_stop(slot->sound.get());
    slot->paused = true;
    return true;
}

bool AudioService::ResumeVoice(VoiceHandle handle) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    slot->paused = false;
    return ma_sound_start(slot->sound.get()) == MA_SUCCESS;
}

bool AudioService::ReleaseVoice(VoiceHandle handle) { return impl_->Release(handle); }
bool AudioService::IsVoiceValid(VoiceHandle handle) const {
    return impl_->Slot(handle) != nullptr;
}
bool AudioService::IsVoicePlaying(VoiceHandle handle) const {
    const Impl::VoiceSlot* slot = impl_->Slot(handle);
    return slot && ma_sound_is_playing(slot->sound.get()) == MA_TRUE;
}

bool AudioService::SetVoiceVolume(VoiceHandle handle, float volume) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    slot->volume = ClampVolume(volume);
    impl_->ApplyVoiceGain(*slot);
    return true;
}

float AudioService::GetVoiceVolume(VoiceHandle handle) const {
    const Impl::VoiceSlot* slot = impl_->Slot(handle);
    return slot ? slot->volume : 0.0f;
}

bool AudioService::SetVoicePitch(VoiceHandle handle, float pitch) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_set_pitch(slot->sound.get(), ClampPitch(pitch));
    return true;
}

bool AudioService::SetVoiceLooping(VoiceHandle handle, bool loop) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_set_looping(slot->sound.get(), loop ? MA_TRUE : MA_FALSE);
    return true;
}

bool AudioService::SetVoiceSpatial(VoiceHandle handle, bool spatial) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_set_spatialization_enabled(slot->sound.get(), spatial ? MA_TRUE : MA_FALSE);
    return true;
}

bool AudioService::SetVoiceMinDistance(VoiceHandle handle, float distance) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_set_min_distance(slot->sound.get(), ClampDistance(distance));
    return true;
}

bool AudioService::SetVoiceMaxDistance(VoiceHandle handle, float distance) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_set_max_distance(slot->sound.get(), ClampDistance(distance));
    return true;
}

bool AudioService::SetVoicePosition(VoiceHandle handle, float x, float y, float z) {
    Impl::VoiceSlot* slot = impl_->Slot(handle);
    if (!slot) return false;
    ma_sound_set_position(slot->sound.get(), x, y, z);
    return true;
}

AudioBus AudioService::GetVoiceBus(VoiceHandle handle) const {
    const Impl::VoiceSlot* slot = impl_->Slot(handle);
    return slot ? slot->bus : AudioBus::Master;
}

void AudioService::SetBusVolume(AudioBus bus, float volume) {
    const std::size_t index = BusIndex(bus);
    Impl::BusState& state = impl_->buses[index];
    state.volume = ClampVolume(volume);
    state.fade = {};
    impl_->ApplyBusGain(static_cast<AudioBus>(index));
}

float AudioService::GetBusVolume(AudioBus bus) const {
    return impl_->buses[BusIndex(bus)].volume;
}

void AudioService::SetBusMuted(AudioBus bus, bool muted) {
    const std::size_t index = BusIndex(bus);
    impl_->buses[index].muted = muted;
    impl_->ApplyBusGain(static_cast<AudioBus>(index));
}

bool AudioService::IsBusMuted(AudioBus bus) const {
    return impl_->buses[BusIndex(bus)].muted;
}

float AudioService::GetEffectiveBusGain(AudioBus bus) const {
    const std::size_t index = BusIndex(bus);
    const Impl::BusState& own = impl_->buses[index];
    float gain = own.muted ? 0.0f : own.volume;
    if (index != BusIndex(AudioBus::Master)) {
        const Impl::BusState& master = impl_->buses[BusIndex(AudioBus::Master)];
        gain *= master.muted ? 0.0f : master.volume;
    }
    return gain;
}

void AudioService::FadeBus(AudioBus bus, float targetVolume,
                           float durationSeconds) {
    const std::size_t index = BusIndex(bus);
    Impl::BusState& state = impl_->buses[index];
    const float duration = std::max(durationSeconds, 0.0f);
    if (duration <= 0.0f) {
        SetBusVolume(static_cast<AudioBus>(index), targetVolume);
        return;
    }
    state.fade.active = true;
    state.fade.start = state.volume;
    state.fade.target = ClampVolume(targetVolume);
    state.fade.elapsed = 0.0f;
    state.fade.duration = duration;
    state.fade.releaseWhenDone = false;
}

std::size_t AudioService::LiveVoiceCount() const {
    return static_cast<std::size_t>(std::count_if(
        impl_->voices.begin(), impl_->voices.end(),
        [](const Impl::VoiceSlot& slot) { return slot.sound != nullptr; }));
}

ma_engine* AudioService::GetEngine() { return impl_->engine.get(); }
const ma_engine* AudioService::GetEngine() const { return impl_->engine.get(); }

bool Audio::Init(bool noDevice) { return AudioService::Get().Init(noDevice); }
bool Audio::InitNoDevice() { return AudioService::Get().InitNoDevice(); }

void Audio::Shutdown() {
    gLegacySoundVoices.clear();
    gLegacySoundPaths.clear();
    gLegacyMusicPath.clear();
    gLegacyMusicVolume = 1.0f;
    AudioService::Get().Shutdown();
}

void Audio::Update(float dt) {
    AudioService::Get().Update(dt);
    for (auto& entry : gLegacySoundVoices) {
        auto& handles = entry.second;
        handles.erase(std::remove_if(handles.begin(), handles.end(),
            [](VoiceHandle handle) {
                return !AudioService::Get().IsVoiceValid(handle);
            }), handles.end());
    }
}

bool Audio::LoadSound(const std::string& name, const std::string& filepath) {
    if (name.empty() || filepath.empty()) return false;
    VoiceHandle probe = AudioService::Get().CreateVoiceFromFile(
        filepath, AudioBus::SFX, AudioLoadMode::DecodeOnLoad);
    if (!probe) return false;
    AudioService::Get().ReleaseVoice(probe);
    StopSound(name);
    gLegacySoundPaths[name] = filepath;
    return true;
}

void Audio::PlaySound(const std::string& name, float volume) {
    const auto found = gLegacySoundPaths.find(name);
    if (found == gLegacySoundPaths.end()) return;
    VoiceHandle handle = AudioService::Get().PlayOneShotPath(
        found->second, AudioBus::SFX, volume, 1.0f,
        AudioLoadMode::DecodeOnLoad);
    if (handle) gLegacySoundVoices[name].push_back(handle);
}

void Audio::StopSound(const std::string& name) {
    const auto found = gLegacySoundVoices.find(name);
    if (found == gLegacySoundVoices.end()) return;
    for (VoiceHandle handle : found->second) {
        AudioService::Get().ReleaseVoice(handle);
    }
    gLegacySoundVoices.erase(found);
}

bool Audio::LoadMusic(const std::string& filepath) {
    if (filepath.empty()) return false;
    VoiceHandle probe = AudioService::Get().CreateVoiceFromFile(
        filepath, AudioBus::Music, AudioLoadMode::Streaming);
    if (!probe) return false;
    AudioService::Get().ReleaseVoice(probe);
    AudioService::Get().StopMusic();
    gLegacyMusicPath = filepath;
    return true;
}

void Audio::PlayMusic(bool loop) {
    if (gLegacyMusicPath.empty()) return;
    VoiceHandle handle = AudioService::Get().PlayMusicPath(
        gLegacyMusicPath, loop, 0.0f, AudioLoadMode::Streaming);
    if (handle) AudioService::Get().SetVoiceVolume(handle, gLegacyMusicVolume);
}

VoiceHandle Audio::PlayMusic(const std::string& guid, bool loop,
                             float fadeInSeconds) {
    return AudioService::Get().PlayMusic(guid, loop, fadeInSeconds);
}

VoiceHandle Audio::CrossFadeMusic(const std::string& guid,
                                  float durationSeconds, bool loop) {
    return AudioService::Get().CrossFadeMusic(guid, durationSeconds, loop);
}

void Audio::StopMusic(float fadeOutSeconds) {
    AudioService::Get().StopMusic(fadeOutSeconds);
}
void Audio::PauseMusic() { AudioService::Get().PauseMusic(); }
void Audio::ResumeMusic() { AudioService::Get().ResumeMusic(); }

void Audio::SetMusicVolume(float volume) {
    gLegacyMusicVolume = ClampVolume(volume);
    AudioService::Get().SetVoiceVolume(
        AudioService::Get().CurrentMusicHandle(), gLegacyMusicVolume);
}

bool Audio::IsMusicPlaying() { return AudioService::Get().IsMusicPlaying(); }

VoiceHandle Audio::PlayOneShot(const std::string& guid, AudioBus bus,
                               float volume, float pitch) {
    return AudioService::Get().PlayOneShot(guid, bus, volume, pitch);
}

void Audio::SetBusVolume(AudioBus bus, float volume) {
    AudioService::Get().SetBusVolume(bus, volume);
}
float Audio::GetBusVolume(AudioBus bus) {
    return AudioService::Get().GetBusVolume(bus);
}
void Audio::SetBusMuted(AudioBus bus, bool muted) {
    AudioService::Get().SetBusMuted(bus, muted);
}
bool Audio::IsBusMuted(AudioBus bus) {
    return AudioService::Get().IsBusMuted(bus);
}
void Audio::FadeBus(AudioBus bus, float targetVolume, float durationSeconds) {
    AudioService::Get().FadeBus(bus, targetVolume, durationSeconds);
}
void Audio::ApplyProjectSettings() { AudioService::Get().ApplyProjectSettings(); }

void Audio::SetMasterVolume(float volume) {
    SetBusVolume(AudioBus::Master, volume);
}
float Audio::GetMasterVolume() { return GetBusVolume(AudioBus::Master); }

std::size_t Audio::LiveVoiceCount() { return AudioService::Get().LiveVoiceCount(); }
ma_engine* Audio::GetEngine() { return AudioService::Get().GetEngine(); }
