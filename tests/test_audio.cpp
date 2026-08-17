#include "Systems/Audio.h"
#include "ECS/GameObject.h"
#include "ECS/Components/AudioSource.h"
#include "ECS/Components/AudioListener.h"
#include "Core/AssetDatabase.h"
#include "Core/PathService.h"
#include "Core/ProjectSettings.h"
#include "Core/SceneSerializer.h"
#include "Core/World.h"
#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

void WriteU16(std::ofstream& file, std::uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)
    };
    file.write(bytes, sizeof(bytes));
}

void WriteU32(std::ofstream& file, std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)
    };
    file.write(bytes, sizeof(bytes));
}

void WriteTestWav(const fs::path& path, float seconds = 2.0f,
                  std::uint32_t sampleRate = 48000,
                  std::uint16_t channels = 1) {
    fs::create_directories(path.parent_path());
    const std::uint32_t frameCount =
        static_cast<std::uint32_t>(seconds * static_cast<float>(sampleRate));
    const std::uint32_t dataBytes = frameCount * channels * sizeof(std::int16_t);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write("RIFF", 4);
    WriteU32(file, 36u + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    WriteU32(file, 16);
    WriteU16(file, 1); // PCM
    WriteU16(file, channels);
    WriteU32(file, sampleRate);
    WriteU32(file, sampleRate * channels * sizeof(std::int16_t));
    WriteU16(file, static_cast<std::uint16_t>(channels * sizeof(std::int16_t)));
    WriteU16(file, 16);
    file.write("data", 4);
    WriteU32(file, dataBytes);
    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const float phase = static_cast<float>(frame % 200u) / 200.0f;
        const std::int16_t sample = static_cast<std::int16_t>(
            std::sin(phase * 6.28318530718f) * 1000.0f);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
            WriteU16(file, static_cast<std::uint16_t>(sample));
        }
    }
}

struct AudioTestScope {
    AudioTestScope() {
        Audio::Shutdown();
        ProjectSettings::Get().SetDefaults();
        started = Audio::InitNoDevice();
    }
    ~AudioTestScope() {
        molga::AssetDatabase::Get().Clear();
        Audio::Shutdown();
        ProjectSettings::Get().SetDefaults();
    }
    bool started = false;
};

struct TempAudioProject {
    explicit TempAudioProject(const std::string& name)
        : root(fs::temp_directory_path() / name),
          assets(root / "Assets"), wav(assets / "tone.wav") {
        std::error_code error;
        fs::remove_all(root, error);
        WriteTestWav(wav);
    }
    ~TempAudioProject() {
        molga::AssetDatabase::Get().Clear();
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path root;
    fs::path assets;
    fs::path wav;
};

} // namespace

TEST_CASE("AudioSource serializes properties, GUID and explicit output bus") {
    auto object = std::make_shared<GameObject>("AudioObject");
    AudioSource* source = object->AddComponent<AudioSource>();

    REQUIRE(source != nullptr);
    CHECK(source->GetOutputBus() == AudioBus::SFX); // new component default
    source->SetClipPath("Assets/audio/test.wav");
    source->SetClipGuid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    source->SetVolume(0.5f);
    source->SetPitch(1.5f);
    source->SetLooping(true);
    source->SetPlayOnAwake(true);
    source->SetSpatial(true);
    source->SetMinDistance(2.0f);
    source->SetMaxDistance(300.0f);
    source->SetOutputBus(AudioBus::Voice);

    const std::string serialized = SceneSerializer::SerializeGameObject(object.get());
    auto restored = SceneSerializer::DeserializeGameObject(serialized);
    REQUIRE(restored != nullptr);
    AudioSource* restoredSource = restored->GetComponent<AudioSource>();
    REQUIRE(restoredSource != nullptr);
    CHECK(restoredSource->GetClipPath() == "Assets/audio/test.wav");
    CHECK(restoredSource->GetClipGuid() == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(restoredSource->GetVolume() == doctest::Approx(0.5f));
    CHECK(restoredSource->GetPitch() == doctest::Approx(1.5f));
    CHECK(restoredSource->IsLooping());
    CHECK(restoredSource->GetPlayOnAwake());
    CHECK(restoredSource->IsSpatial());
    CHECK(restoredSource->GetMinDistance() == doctest::Approx(2.0f));
    CHECK(restoredSource->GetMaxDistance() == doctest::Approx(300.0f));
    CHECK(restoredSource->GetOutputBus() == AudioBus::Voice);

    AudioSource legacy;
    legacy.Deserialize({{"clipPath", "Assets/audio/legacy.wav"}});
    CHECK(legacy.GetOutputBus() == AudioBus::Master);
}

TEST_CASE("AudioListener serializes through the scene component contract") {
    auto object = std::make_shared<GameObject>("ListenerObject");
    REQUIRE(object->AddComponent<AudioListener>() != nullptr);
    const std::string serialized = SceneSerializer::SerializeGameObject(object.get());
    auto restored = SceneSerializer::DeserializeGameObject(serialized);
    REQUIRE(restored != nullptr);
    CHECK(restored->GetComponent<AudioListener>() != nullptr);
}

TEST_CASE("AudioService no-device voices overlap and stale generations stay invalid") {
    TempAudioProject project("molga_audio_voice_test");
    AudioTestScope audioScope;
    REQUIRE(audioScope.started);
    AudioService& audio = AudioService::Get();
    CHECK(audio.IsNoDeviceBackend());

    const VoiceHandle first = audio.PlayOneShotPath(project.wav.string());
    const VoiceHandle second = audio.PlayOneShotPath(project.wav.string());
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    CHECK(audio.LiveVoiceCount() == 2);

    REQUIRE(audio.ReleaseVoice(first));
    CHECK_FALSE(audio.IsVoiceValid(first));
    const VoiceHandle recycled = audio.CreateVoiceFromFile(project.wav.string());
    REQUIRE(recycled);
    CHECK(recycled.index == first.index);
    CHECK(recycled.generation != first.generation);
    CHECK_FALSE(audio.IsVoiceValid(first));
    CHECK(audio.LiveVoiceCount() == 2);

    audio.ReleaseVoice(second);
    audio.ReleaseVoice(recycled);
    CHECK(audio.LiveVoiceCount() == 0);
}

TEST_CASE("AudioService routes GUID one-shots and AudioSource releases on disable and destroy") {
    TempAudioProject project("molga_audio_lifetime_test");
    AudioTestScope audioScope;
    REQUIRE(audioScope.started);

    molga::AssetDatabase::Get().ScanProject(project.assets);
    const std::string guid =
        molga::AssetDatabase::Get().GuidForSource("Assets/tone.wav");
    REQUIRE_FALSE(guid.empty());
    const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
    REQUIRE(record != nullptr);
    CHECK_FALSE(record->importFailed);

    AudioService& audio = AudioService::Get();
    const VoiceHandle oneShot = audio.PlayOneShot(guid, AudioBus::UI);
    REQUIRE(oneShot);
    CHECK(audio.GetVoiceBus(oneShot) == AudioBus::UI);
    audio.ReleaseVoice(oneShot);

    World world;
    auto object = std::make_shared<GameObject>("Source");
    AudioSource* source = object->AddComponent<AudioSource>();
    source->SetClipGuid(guid);
    world.Add(object);
    source->ResolveAssets();
    REQUIRE(audio.IsVoiceValid(source->GetVoiceHandle()));
    CHECK(audio.LiveVoiceCount() == 1);

    source->SetEnabled(false);
    CHECK(audio.LiveVoiceCount() == 0);
    source->SetEnabled(true);
    source->ResolveAssets();
    CHECK(audio.LiveVoiceCount() == 1);

    world.Shutdown();
    CHECK(audio.LiveVoiceCount() == 0);
}

TEST_CASE("Fixed buses preserve stored volume, mute and deterministic fades") {
    AudioTestScope audioScope;
    REQUIRE(audioScope.started);
    AudioService& audio = AudioService::Get();

    audio.SetBusVolume(AudioBus::Master, 0.8f);
    audio.SetBusVolume(AudioBus::SFX, 0.6f);
    CHECK(audio.GetEffectiveBusGain(AudioBus::SFX) == doctest::Approx(0.48f));
    CHECK(audio.GetEngine() != nullptr);

    audio.SetBusMuted(AudioBus::Master, true);
    CHECK(audio.GetEffectiveBusGain(AudioBus::SFX) == doctest::Approx(0.0f));
    CHECK(audio.GetBusVolume(AudioBus::Master) == doctest::Approx(0.8f));
    audio.SetBusMuted(AudioBus::Master, false);

    audio.FadeBus(AudioBus::SFX, 0.2f, 1.0f);
    audio.Update(0.5f);
    CHECK(audio.GetBusVolume(AudioBus::SFX) == doctest::Approx(0.4f));
    audio.Update(0.5f);
    CHECK(audio.GetBusVolume(AudioBus::SFX) == doctest::Approx(0.2f));
}

TEST_CASE("Music crossfade retains two voices then releases the outgoing voice") {
    TempAudioProject project("molga_audio_music_test");
    AudioTestScope audioScope;
    REQUIRE(audioScope.started);
    AudioService& audio = AudioService::Get();

    const VoiceHandle first = audio.PlayMusicPath(project.wav.string(), true);
    REQUIRE(first);
    CHECK(audio.LiveVoiceCount() == 1);
    const VoiceHandle second =
        audio.CrossFadeMusicPath(project.wav.string(), 1.0f, true);
    REQUIRE(second);
    CHECK(audio.CurrentMusicHandle() == second);
    CHECK(audio.LiveVoiceCount() == 2);

    audio.Update(0.5f);
    CHECK(audio.IsVoiceValid(first));
    CHECK(audio.LiveVoiceCount() == 2);
    audio.Update(0.5f);
    CHECK_FALSE(audio.IsVoiceValid(first));
    CHECK(audio.IsVoiceValid(second));
    CHECK(audio.LiveVoiceCount() == 1);

    audio.StopMusic(0.5f);
    audio.Update(0.5f);
    CHECK(audio.LiveVoiceCount() == 0);
    CHECK_FALSE(audio.IsVoiceValid(second));
}

TEST_CASE("Legacy static sound facade now creates overlapping one-shots") {
    TempAudioProject project("molga_audio_legacy_test");
    AudioTestScope audioScope;
    REQUIRE(audioScope.started);

    REQUIRE(Audio::LoadSound("hit", project.wav.string()));
    Audio::PlaySound("hit");
    Audio::PlaySound("hit");
    CHECK(Audio::LiveVoiceCount() == 2);
    Audio::StopSound("hit");
    CHECK(Audio::LiveVoiceCount() == 0);
}

TEST_CASE("ProjectSettings round-trips all fixed audio bus defaults") {
    ProjectSettings settings;
    settings.audioBusSettings[static_cast<std::size_t>(AudioBus::Master)] = {0.75f, false};
    settings.audioBusSettings[static_cast<std::size_t>(AudioBus::Music)] = {0.35f, true};
    settings.audioBusSettings[static_cast<std::size_t>(AudioBus::SFX)] = {0.65f, false};
    settings.audioBusSettings[static_cast<std::size_t>(AudioBus::Voice)] = {0.55f, true};
    settings.audioBusSettings[static_cast<std::size_t>(AudioBus::UI)] = {0.45f, false};

    const nlohmann::json json = settings.Serialize();
    REQUIRE(json.contains("audio"));
    REQUIRE(json["audio"].contains("buses"));
    ProjectSettings restored;
    restored.Deserialize(json);
    for (std::size_t i = 0; i < ProjectSettings::AudioBusCount; ++i) {
        CHECK(restored.audioBusSettings[i].volume ==
              doctest::Approx(settings.audioBusSettings[i].volume));
        CHECK(restored.audioBusSettings[i].muted == settings.audioBusSettings[i].muted);
    }
}
