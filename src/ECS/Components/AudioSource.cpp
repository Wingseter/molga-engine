#include "AudioSource.h"

#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "Transform.h"
#include "Common/Log.h"
#include "Core/AssetDatabase.h"
#include "Core/PathService.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#ifdef MOLGA_EDITOR
#include "../../Editor/Project.h"
#include <imgui.h>
#endif

REGISTER_COMPONENT(AudioSource)

namespace {

AudioLoadMode LoadModeForRecord(const molga::AssetRecord* record) {
    if (record && record->settings.is_object() &&
        record->settings.value("loadMode", std::string{}) == "Streaming") {
        return AudioLoadMode::Streaming;
    }
    return AudioLoadMode::DecodeOnLoad;
}

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

} // namespace

AudioSource::AudioSource() = default;

AudioSource::~AudioSource() {
    ReleaseVoice();
}

void AudioSource::SetClipPath(const std::string& path) {
    if (clipPath == path && clipGuid.empty()) return;
    ReleaseVoice();
    clipPath = path;
    clipGuid.clear();
    resolvedPath.clear();
}

void AudioSource::SetClipGuid(const std::string& guid) {
    if (clipGuid == guid) return;
    ReleaseVoice();
    clipGuid = guid;
    resolvedPath.clear();
}

void AudioSource::SetOutputBus(AudioBus bus) {
    if (static_cast<std::uint8_t>(bus) >=
        static_cast<std::uint8_t>(AudioBus::Count)) {
        bus = AudioBus::Master;
    }
    if (outputBus == bus) return;
    const bool wasPlaying = IsPlaying();
    ReleaseVoice();
    outputBus = bus;
    if (wasPlaying && EnsureVoice()) Play();
}

bool AudioSource::EnsureVoice() {
    AudioService& audio = AudioService::Get();
    if (audio.IsVoiceValid(voice)) return true;
    voice = {};
    if (!audio.IsInitialized()) return false;

    if (!clipGuid.empty()) {
        const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(clipGuid);
        if (!record || record->importer != "AudioImporter" || record->importFailed) {
            return false;
        }
        const std::filesystem::path source =
            molga::AssetDatabase::Get().AbsoluteSourcePath(clipGuid);
        if (source.empty()) return false;
        resolvedPath = source.string();
        loadMode = LoadModeForRecord(record);
        voice = audio.CreateVoiceFromGuid(clipGuid, outputBus, loadMode);
    } else if (!clipPath.empty()) {
        resolvedPath = PathService::Get().ResolveAsset(clipPath);
        loadMode = AudioLoadMode::DecodeOnLoad;
        voice = audio.CreateVoiceFromFile(resolvedPath, outputBus, loadMode);
    }

    if (!voice) return false;
    ApplyProperties();
    return true;
}

void AudioSource::ReleaseVoice() {
    if (voice) AudioService::Get().ReleaseVoice(voice);
    voice = {};
}

void AudioSource::Play() {
    if (EnsureVoice()) AudioService::Get().PlayVoice(voice);
}

void AudioSource::Stop() {
    AudioService::Get().StopVoice(voice);
}

void AudioSource::Pause() {
    AudioService::Get().PauseVoice(voice);
}

void AudioSource::Resume() {
    if (EnsureVoice()) AudioService::Get().ResumeVoice(voice);
}

bool AudioSource::IsPlaying() const {
    return AudioService::Get().IsVoicePlaying(voice);
}

void AudioSource::SetVolume(float value) {
    volume = std::clamp(FiniteOr(value, 1.0f), 0.0f, 1.0f);
    AudioService::Get().SetVoiceVolume(voice, volume);
}

void AudioSource::SetPitch(float value) {
    pitch = std::max(FiniteOr(value, 1.0f), 0.01f);
    AudioService::Get().SetVoicePitch(voice, pitch);
}

void AudioSource::SetLooping(bool value) {
    loop = value;
    AudioService::Get().SetVoiceLooping(voice, loop);
}

void AudioSource::SetSpatial(bool value) {
    spatial = value;
    AudioService::Get().SetVoiceSpatial(voice, spatial);
}

void AudioSource::SetMinDistance(float distance) {
    minDistance = std::max(FiniteOr(distance, 1.0f), 0.0f);
    AudioService::Get().SetVoiceMinDistance(voice, minDistance);
}

void AudioSource::SetMaxDistance(float distance) {
    maxDistance = std::max(FiniteOr(distance, 500.0f), 0.0f);
    AudioService::Get().SetVoiceMaxDistance(voice, maxDistance);
}

void AudioSource::ApplyProperties() {
    AudioService& audio = AudioService::Get();
    if (!audio.IsVoiceValid(voice)) return;
    audio.SetVoiceVolume(voice, volume);
    audio.SetVoicePitch(voice, pitch);
    audio.SetVoiceLooping(voice, loop);
    audio.SetVoiceSpatial(voice, spatial);
    audio.SetVoiceMinDistance(voice, minDistance);
    audio.SetVoiceMaxDistance(voice, maxDistance);
}

void AudioSource::Start() {
    if (playOnAwake) Play();
}

void AudioSource::OnEnable() {
    // Initial OnEnable runs before Start. Re-enabling an already-started source
    // recreates the released voice and honors playOnAwake.
    if (HasStarted() && playOnAwake) Play();
}

void AudioSource::OnDisable() {
    ReleaseVoice();
}

void AudioSource::Update(float dt) {
    (void)dt;
    if (!spatial || !AudioService::Get().IsVoiceValid(voice) || !gameObject) return;
    if (Transform* transform = gameObject->GetComponent<Transform>()) {
        const Vector2 position = transform->GetWorldPosition();
        AudioService::Get().SetVoicePosition(voice, position.x, position.y, 0.0f);
    }
}

void AudioSource::OnDestroy() {
    ReleaseVoice();
}

void AudioSource::ResolveAssets() {
    ReleaseVoice();
    resolvedPath.clear();

    if (clipGuid.empty() && !clipPath.empty()) {
        const std::string migrated = molga::AssetDatabase::Get().GuidForSource(clipPath);
        if (!migrated.empty()) clipGuid = migrated;
    }

    if ((clipGuid.empty() && clipPath.empty()) || EnsureVoice()) return;
    if (!AudioService::Get().IsInitialized()) {
        Log::Warn("AudioSource", "Cannot resolve assets, audio service is not initialized");
    } else {
        const std::string reference = !clipGuid.empty() ? clipGuid : clipPath;
        Log::Warn("AudioSource", "Failed to load audio clip: " + reference);
    }
}

void AudioSource::Serialize(nlohmann::json& json) const {
    json["clipPath"] = clipPath;
    json["clipGuid"] = clipGuid;
    json["volume"] = volume;
    json["pitch"] = pitch;
    json["loop"] = loop;
    json["playOnAwake"] = playOnAwake;
    json["spatial"] = spatial;
    json["minDistance"] = minDistance;
    json["maxDistance"] = maxDistance;
    json["outputBus"] = AudioBusName(outputBus);
}

void AudioSource::Deserialize(const nlohmann::json& json) {
    ReleaseVoice();
    if (json.contains("clipGuid") && json["clipGuid"].is_string()) {
        clipGuid = json["clipGuid"].get<std::string>();
    } else {
        clipGuid.clear();
    }
    if (json.contains("clipPath") && json["clipPath"].is_string()) {
        clipPath = json["clipPath"].get<std::string>();
    } else {
        clipPath.clear();
    }
    if (clipGuid.empty() && !clipPath.empty()) {
        const std::string migrated = molga::AssetDatabase::Get().GuidForSource(clipPath);
        if (!migrated.empty()) clipGuid = migrated;
    }

    volume = std::clamp(FiniteOr(json.value("volume", volume), 1.0f), 0.0f, 1.0f);
    pitch = std::max(FiniteOr(json.value("pitch", pitch), 1.0f), 0.01f);
    loop = json.value("loop", loop);
    playOnAwake = json.value("playOnAwake", playOnAwake);
    spatial = json.value("spatial", spatial);
    minDistance = std::max(FiniteOr(json.value("minDistance", minDistance), 1.0f), 0.0f);
    maxDistance = std::max(FiniteOr(json.value("maxDistance", maxDistance), 500.0f), 0.0f);

    // Legacy scenes had no routing field and historically fed the engine root.
    // Newly constructed components retain the SFX default.
    outputBus = AudioBus::Master;
    if (json.contains("outputBus") && json["outputBus"].is_string()) {
        AudioBus parsed = AudioBus::Master;
        if (TryParseAudioBus(json["outputBus"].get<std::string>(), parsed)) {
            outputBus = parsed;
        }
    }
    resolvedPath.clear();
}

void AudioSource::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    char pathBuffer[512];
    std::strncpy(pathBuffer, clipPath.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';

    ImGui::Text("Audio Clip Path");
    ImGui::SetNextItemWidth(-60);
    if (ImGui::InputText("##clipPath", pathBuffer, sizeof(pathBuffer))) {
        SetClipPath(pathBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        SetClipPath("");
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("AUDIO_PATH")) {
            const char* droppedPath = static_cast<const char*>(payload->Data);
            std::string relativePath = droppedPath ? droppedPath : "";
            if (Project::Get().IsOpen()) {
                relativePath = Project::Get().GetRelativePath(relativePath);
            }
            SetClipPath(relativePath);
            const std::string guid =
                molga::AssetDatabase::Get().GuidForSource(relativePath);
            if (!guid.empty()) SetClipGuid(guid);
            ResolveAssets();
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
            const char* data = static_cast<const char*>(payload->Data);
            const std::string guid = data ? data : "";
            const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
            if (record && record->importer == "AudioImporter") {
                const std::string path = record->sourcePath;
                SetClipPath(path);
                SetClipGuid(guid);
                ResolveAssets();
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (!clipPath.empty() || !clipGuid.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(AudioService::Get().IsVoiceValid(voice) ? "Reload" : "Load")) {
            ResolveAssets();
        }
    }

    if (AudioService::Get().IsVoiceValid(voice)) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Loaded successfully");
    } else if (!clipPath.empty() || !clipGuid.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.3f, 1.0f), "Not loaded");
    }

    const char* busNames[] = {"Master", "Music", "SFX", "Voice", "UI"};
    int busIndex = static_cast<int>(outputBus);
    if (ImGui::Combo("Output Bus", &busIndex, busNames, 5)) {
        SetOutputBus(static_cast<AudioBus>(busIndex));
    }

    float editedVolume = volume;
    if (ImGui::SliderFloat("Volume", &editedVolume, 0.0f, 1.0f)) SetVolume(editedVolume);
    float editedPitch = pitch;
    if (ImGui::SliderFloat("Pitch", &editedPitch, 0.1f, 3.0f)) SetPitch(editedPitch);
    bool editedLoop = loop;
    if (ImGui::Checkbox("Loop", &editedLoop)) SetLooping(editedLoop);
    ImGui::Checkbox("Play on Awake", &playOnAwake);

    bool editedSpatial = spatial;
    if (ImGui::Checkbox("Spatial (3D)", &editedSpatial)) SetSpatial(editedSpatial);
    if (spatial) {
        ImGui::Indent();
        float editedMin = minDistance;
        if (ImGui::DragFloat("Min Distance", &editedMin, 0.1f, 0.0f, 1000.0f)) {
            SetMinDistance(editedMin);
        }
        float editedMax = maxDistance;
        if (ImGui::DragFloat("Max Distance", &editedMax, 1.0f, 0.0f, 10000.0f)) {
            SetMaxDistance(editedMax);
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Play Preview")) Play();
    ImGui::SameLine();
    if (ImGui::Button("Stop Preview")) Stop();
#endif
}
