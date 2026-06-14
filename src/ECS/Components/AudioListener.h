#pragma once

#include "../Component.h"

class AudioListener : public Component {
public:
    COMPONENT_TYPE(AudioListener)

    AudioListener() = default;
    ~AudioListener() override = default;

    // Lifecycle
    void Update(float dt) override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;
};
