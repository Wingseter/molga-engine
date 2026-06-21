#include "AudioSource.h"
#include "../../external/miniaudio/miniaudio.h"
#include "../GameObject.h"
#include "Transform.h"
#include "../ComponentFactory.h"
#include "../../Core/PathService.h"
#include "../../Common/Log.h"
#ifdef MOLGA_EDITOR
#include "../../Editor/Project.h"
#include <imgui.h>
#endif

REGISTER_COMPONENT(AudioSource)

AudioSource::AudioSource() = default;
AudioSource::~AudioSource() = default;

void AudioSource::Play() {
    if (sound) {
        ma_sound_start(sound.get());
    }
}

void AudioSource::Stop() {
    if (sound) {
        ma_sound_stop(sound.get());
        ma_sound_seek_to_pcm_frame(sound.get(), 0);
    }
}

void AudioSource::Pause() {
    if (sound) {
        ma_sound_stop(sound.get());
    }
}

void AudioSource::Resume() {
    if (sound) {
        ma_sound_start(sound.get());
    }
}

bool AudioSource::IsPlaying() const {
    if (sound) {
        return ma_sound_is_playing(sound.get()) == MA_TRUE;
    }
    return false;
}

void AudioSource::SetVolume(float vol) {
    volume = vol;
    if (sound) {
        ma_sound_set_volume(sound.get(), volume);
    }
}

void AudioSource::SetPitch(float p) {
    pitch = p;
    if (sound) {
        ma_sound_set_pitch(sound.get(), pitch);
    }
}

void AudioSource::SetLooping(bool l) {
    loop = l;
    if (sound) {
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
    }
}

void AudioSource::SetSpatial(bool val) {
    spatial = val;
    if (sound) {
        ma_sound_set_spatialization_enabled(sound.get(), spatial ? MA_TRUE : MA_FALSE);
    }
}

void AudioSource::SetMinDistance(float dist) {
    minDistance = dist;
    if (sound) {
        ma_sound_set_min_distance(sound.get(), minDistance);
    }
}

void AudioSource::SetMaxDistance(float dist) {
    maxDistance = dist;
    if (sound) {
        ma_sound_set_max_distance(sound.get(), maxDistance);
    }
}

void AudioSource::ApplyProperties() {
    if (!sound) return;
    ma_sound_set_volume(sound.get(), volume);
    ma_sound_set_pitch(sound.get(), pitch);
    ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(sound.get(), spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_min_distance(sound.get(), minDistance);
    ma_sound_set_max_distance(sound.get(), maxDistance);
}

void AudioSource::Start() {
    if (playOnAwake) {
        Play();
    }
}

void AudioSource::Update(float dt) {
    if (spatial && sound && gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 pos = transform->GetWorldPosition();
            ma_sound_set_position(sound.get(), pos.x, pos.y, 0.0f);
        }
    }
}

void AudioSource::OnDestroy() {
    Stop();
    sound.reset();
}

void AudioSource::ResolveAssets() {
    if (clipPath.empty()) return;

    sound.reset();

    std::string absPath = PathService::Get().ResolveAsset(clipPath);
    ma_engine* engine = Audio::GetEngine();
    if (!engine) {
        Log::Warn("AudioSource", "Cannot resolve assets, audio engine not initialized");
        return;
    }

    auto raw = new ma_sound();
    ma_result result = ma_sound_init_from_file(engine, absPath.c_str(), 0, nullptr, nullptr, raw);
    if (result != MA_SUCCESS) {
        Log::Warn("AudioSource", "Failed to load sound file: " + absPath);
        delete raw;
        return;
    }

    sound = std::unique_ptr<ma_sound, MaSoundDeleter>(raw);
    ApplyProperties();
}

void AudioSource::Serialize(nlohmann::json& j) const {
    j["clipPath"] = clipPath;
    j["volume"] = volume;
    j["pitch"] = pitch;
    j["loop"] = loop;
    j["playOnAwake"] = playOnAwake;
    j["spatial"] = spatial;
    j["minDistance"] = minDistance;
    j["maxDistance"] = maxDistance;
}

void AudioSource::Deserialize(const nlohmann::json& j) {
    if (j.contains("clipPath")) clipPath = j["clipPath"].get<std::string>();
    if (j.contains("volume")) volume = j["volume"].get<float>();
    if (j.contains("pitch")) pitch = j["pitch"].get<float>();
    if (j.contains("loop")) loop = j["loop"].get<bool>();
    if (j.contains("playOnAwake")) playOnAwake = j["playOnAwake"].get<bool>();
    if (j.contains("spatial")) spatial = j["spatial"].get<bool>();
    if (j.contains("minDistance")) minDistance = j["minDistance"].get<float>();
    if (j.contains("maxDistance")) maxDistance = j["maxDistance"].get<float>();
}

void AudioSource::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    // Clip path edit field
    char pathBuffer[512];
    strncpy(pathBuffer, clipPath.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';

    ImGui::Text("Audio Clip Path");
    ImGui::SetNextItemWidth(-60);
    if (ImGui::InputText("##clipPath", pathBuffer, sizeof(pathBuffer))) {
        clipPath = pathBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        clipPath.clear();
        Stop();
        sound.reset();
    }

    // Drop target for audio file
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("AUDIO_PATH")) {
            const char* droppedPath = static_cast<const char*>(payload->Data);
            std::string relativePath = droppedPath;
            if (Project::Get().IsOpen()) {
                relativePath = Project::Get().GetRelativePath(droppedPath);
            }
            clipPath = relativePath;
            ResolveAssets();
        }
        ImGui::EndDragDropTarget();
    }

    // Load/Reload button if path is not empty
    if (!clipPath.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(sound ? "Reload" : "Load")) {
            ResolveAssets();
        }
    }

    if (sound) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Loaded successfully");
    } else if (!clipPath.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.3f, 1.0f), "Not loaded");
    }

    ImGui::Spacing();

    // Volume
    float vol = volume;
    if (ImGui::SliderFloat("Volume", &vol, 0.0f, 1.0f)) {
        SetVolume(vol);
    }

    // Pitch
    float p = pitch;
    if (ImGui::SliderFloat("Pitch", &p, 0.1f, 3.0f)) {
        SetPitch(p);
    }

    // Loop
    bool l = loop;
    if (ImGui::Checkbox("Loop", &l)) {
        SetLooping(l);
    }

    // Play on Awake
    ImGui::Checkbox("Play on Awake", &playOnAwake);

    // Spatial
    bool sp = spatial;
    if (ImGui::Checkbox("Spatial (3D)", &sp)) {
        SetSpatial(sp);
    }

    if (spatial) {
        ImGui::Indent();
        float minDist = minDistance;
        if (ImGui::DragFloat("Min Distance", &minDist, 0.1f, 0.0f, 1000.0f)) {
            SetMinDistance(minDist);
        }
        float maxDist = maxDistance;
        if (ImGui::DragFloat("Max Distance", &maxDist, 1.0f, 0.0f, 10000.0f)) {
            SetMaxDistance(maxDist);
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Play Preview")) {
        if (!sound && !clipPath.empty()) {
            ResolveAssets();
        }
        Play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop Preview")) {
        Stop();
    }
#endif
}
