#include "AudioListener.h"
#include "../../external/miniaudio/miniaudio.h"
#include "../../Systems/Audio.h"
#include "../GameObject.h"
#include "Transform.h"
#include "../ComponentFactory.h"
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

REGISTER_COMPONENT(AudioListener)

void AudioListener::Update(float dt) {
    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 pos = transform->GetWorldPosition();
            ma_engine* engine = Audio::GetEngine();
            if (engine) {
                ma_engine_listener_set_position(engine, 0, pos.x, pos.y, 0.0f);
            }
        }
    }
}

void AudioListener::Serialize(nlohmann::json& j) const {
    // No parameters to serialize
}

void AudioListener::Deserialize(const nlohmann::json& j) {
    // No parameters to deserialize
}

void AudioListener::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    ImGui::Text("Audio Listener");
    ImGui::TextDisabled("Updates the 3D audio listener position based on this object's Transform.");
#endif
}
