#pragma once

#include "../Component.h"
#include "../../Systems/Audio.h"
#include <string>
#include <memory>

struct ma_sound;

class AudioSource : public Component {
public:
    COMPONENT_TYPE(AudioSource)

    AudioSource();
    ~AudioSource() override;

    // Methods
    void Play();
    void Stop();
    void Pause();
    void Resume();
    bool IsPlaying() const;

    void SetVolume(float vol);
    float GetVolume() const { return volume; }

    void SetPitch(float p);
    float GetPitch() const { return pitch; }

    void SetLooping(bool l);
    bool IsLooping() const { return loop; }

    void SetClipPath(const std::string& path) { clipPath = path; }
    const std::string& GetClipPath() const { return clipPath; }

    void SetPlayOnAwake(bool val) { playOnAwake = val; }
    bool GetPlayOnAwake() const { return playOnAwake; }

    void SetSpatial(bool val);
    bool IsSpatial() const { return spatial; }

    void SetMinDistance(float dist);
    float GetMinDistance() const { return minDistance; }

    void SetMaxDistance(float dist);
    float GetMaxDistance() const { return maxDistance; }

    // Lifecycle
    void Start() override;
    void Update(float dt) override;
    void OnDestroy() override;
    void ResolveAssets() override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    void ApplyProperties();

    std::string clipPath;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnAwake = false;
    bool spatial = false;
    float minDistance = 1.0f;
    float maxDistance = 500.0f;

    // Runtime miniaudio state
    std::unique_ptr<ma_sound, MaSoundDeleter> sound;
};
