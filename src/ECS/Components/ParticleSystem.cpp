#include "ParticleSystem.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Sprite.h"
#include <glad/glad.h>
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

REGISTER_COMPONENT(ParticleSystem)

namespace {
    void SerializeConfig(nlohmann::json& j, const ParticleConfig& c) {
        j["spawnRate"] = c.spawnRate;
        j["maxParticles"] = c.maxParticles;
        j["spawnRadius"] = c.spawnRadius;
        j["minSpeed"] = c.minSpeed;
        j["maxSpeed"] = c.maxSpeed;
        j["minAngle"] = c.minAngle;
        j["maxAngle"] = c.maxAngle;
        j["gravityX"] = c.gravityX;
        j["gravityY"] = c.gravityY;
        j["startSize"] = c.startSize;
        j["endSize"] = c.endSize;
        j["sizeVariance"] = c.sizeVariance;
        j["minRotationSpeed"] = c.minRotationSpeed;
        j["maxRotationSpeed"] = c.maxRotationSpeed;
        j["minLife"] = c.minLife;
        j["maxLife"] = c.maxLife;
        j["startColor"] = { c.startR, c.startG, c.startB, c.startA };
        j["endColor"] = { c.endR, c.endG, c.endB, c.endA };
    }

    void DeserializeConfig(const nlohmann::json& j, ParticleConfig& c) {
        if (j.contains("spawnRate")) c.spawnRate = j["spawnRate"];
        if (j.contains("maxParticles")) c.maxParticles = j["maxParticles"];
        if (j.contains("spawnRadius")) c.spawnRadius = j["spawnRadius"];
        if (j.contains("minSpeed")) c.minSpeed = j["minSpeed"];
        if (j.contains("maxSpeed")) c.maxSpeed = j["maxSpeed"];
        if (j.contains("minAngle")) c.minAngle = j["minAngle"];
        if (j.contains("maxAngle")) c.maxAngle = j["maxAngle"];
        if (j.contains("gravityX")) c.gravityX = j["gravityX"];
        if (j.contains("gravityY")) c.gravityY = j["gravityY"];
        if (j.contains("startSize")) c.startSize = j["startSize"];
        if (j.contains("endSize")) c.endSize = j["endSize"];
        if (j.contains("sizeVariance")) c.sizeVariance = j["sizeVariance"];
        if (j.contains("minRotationSpeed")) c.minRotationSpeed = j["minRotationSpeed"];
        if (j.contains("maxRotationSpeed")) c.maxRotationSpeed = j["maxRotationSpeed"];
        if (j.contains("minLife")) c.minLife = j["minLife"];
        if (j.contains("maxLife")) c.maxLife = j["maxLife"];
        if (j.contains("startColor") && j["startColor"].is_array() && j["startColor"].size() == 4) {
            c.startR = j["startColor"][0];
            c.startG = j["startColor"][1];
            c.startB = j["startColor"][2];
            c.startA = j["startColor"][3];
        }
        if (j.contains("endColor") && j["endColor"].is_array() && j["endColor"].size() == 4) {
            c.endR = j["endColor"][0];
            c.endG = j["endColor"][1];
            c.endB = j["endColor"][2];
            c.endA = j["endColor"][3];
        }
    }
}

ParticleSystem::ParticleSystem() {
    emitter.SetConfig(config);
}

void ParticleSystem::Play() {
    emissionTime = 0.0f;
    emitter.Start();
}

void ParticleSystem::Stop() {
    emitter.Stop();
}

void ParticleSystem::Emit(int count) {
    emitter.Burst(count);
}

void ParticleSystem::Start() {
    if (playOnAwake) {
        Play();
    }
}

void ParticleSystem::Update(float dt) {
    if (!gameObject) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (transform) {
        Vector2 pos = transform->GetWorldPosition();
        emitter.SetPosition(pos.x, pos.y);
    }

    emitter.Update(dt);

    if (emitter.IsEmitting()) {
        emissionTime += dt;
        if (!looping && emissionTime >= config.maxLife) {
            Stop();
        }
    }
}

void ParticleSystem::RenderSprite(Renderer* renderer) {
    if (!gameObject || !enabled) return;

    if (useAdditiveBlending) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    for (const auto& p : emitter.GetParticles()) {
        if (!p.active) continue;

        Sprite sprite;
        sprite.SetPosition(p.x - p.size / 2, p.y - p.size / 2);
        sprite.SetSize(p.size, p.size);
        sprite.SetColor(p.r, p.g, p.b, p.a);
        sprite.SetRotation(p.rotation);
        renderer->DrawSprite(&sprite);
    }

    if (useAdditiveBlending) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

void ParticleSystem::Serialize(nlohmann::json& j) const {
    Component::Serialize(j);
    j["playOnAwake"] = playOnAwake;
    j["looping"] = looping;
    j["presetName"] = presetName;
    j["useAdditiveBlending"] = useAdditiveBlending;
    j["sortingOrder"] = sortingOrder;
    nlohmann::json configJson;
    SerializeConfig(configJson, config);
    j["config"] = configJson;
}

void ParticleSystem::Deserialize(const nlohmann::json& j) {
    Component::Deserialize(j);
    if (j.contains("playOnAwake")) playOnAwake = j["playOnAwake"];
    if (j.contains("looping")) looping = j["looping"];
    if (j.contains("presetName")) presetName = j["presetName"];
    if (j.contains("useAdditiveBlending")) useAdditiveBlending = j["useAdditiveBlending"];
    if (j.contains("sortingOrder")) sortingOrder = j["sortingOrder"];
    if (j.contains("config")) {
        DeserializeConfig(j["config"], config);
    }
    emitter.SetConfig(config);
}

void ParticleSystem::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    // Preset combo box
    const char* presets[] = { "Custom", "Fire", "Smoke", "Spark", "Snow", "Explosion" };
    int currentPreset = 0;
    for (int i = 0; i < 6; i++) {
        if (presetName == presets[i]) {
            currentPreset = i;
            break;
        }
    }

    if (ImGui::Combo("Preset", &currentPreset, presets, 6)) {
        presetName = presets[currentPreset];
        if (presetName == "Fire") {
            config = ParticlePresets::Fire();
            emitter.SetConfig(config);
        } else if (presetName == "Smoke") {
            config = ParticlePresets::Smoke();
            emitter.SetConfig(config);
        } else if (presetName == "Spark") {
            config = ParticlePresets::Spark();
            emitter.SetConfig(config);
        } else if (presetName == "Snow") {
            config = ParticlePresets::Snow();
            emitter.SetConfig(config);
        } else if (presetName == "Explosion") {
            config = ParticlePresets::Explosion();
            emitter.SetConfig(config);
        }
    }

    ImGui::Spacing();
    ImGui::Text("General Settings");
    ImGui::Separator();
    
    ImGui::Checkbox("Play on Awake", &playOnAwake);
    ImGui::Checkbox("Looping", &looping);
    ImGui::Checkbox("Use Additive Blending", &useAdditiveBlending);
    ImGui::DragInt("Sorting Order", &sortingOrder, 1);

    bool changed = false;

    if (ImGui::CollapsingHeader("Spawn Settings")) {
        if (ImGui::DragFloat("Spawn Rate", &config.spawnRate, 0.5f, 0.0f, 1000.0f)) changed = true;
        int maxP = config.maxParticles;
        if (ImGui::DragInt("Max Particles", &maxP, 1, 1, 10000)) {
            config.maxParticles = maxP;
            emitter.SetConfig(config); // Resize particles
            changed = true;
        }
        if (ImGui::DragFloat("Spawn Radius", &config.spawnRadius, 0.5f, 0.0f, 1000.0f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Velocity Settings")) {
        if (ImGui::DragFloat("Min Speed", &config.minSpeed, 0.5f, 0.0f, 1000.0f)) changed = true;
        if (ImGui::DragFloat("Max Speed", &config.maxSpeed, 0.5f, 0.0f, 1000.0f)) changed = true;
        if (ImGui::SliderAngle("Min Angle", &config.minAngle, -360.0f, 360.0f)) changed = true;
        if (ImGui::SliderAngle("Max Angle", &config.maxAngle, -360.0f, 360.0f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Gravity Settings")) {
        if (ImGui::DragFloat("Gravity X", &config.gravityX, 0.5f, -1000.0f, 1000.0f)) changed = true;
        if (ImGui::DragFloat("Gravity Y", &config.gravityY, 0.5f, -1000.0f, 1000.0f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Size Settings")) {
        if (ImGui::DragFloat("Start Size", &config.startSize, 0.5f, 0.0f, 500.0f)) changed = true;
        if (ImGui::DragFloat("End Size", &config.endSize, 0.5f, 0.0f, 500.0f)) changed = true;
        if (ImGui::DragFloat("Size Variance", &config.sizeVariance, 0.5f, 0.0f, 500.0f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Rotation Settings")) {
        if (ImGui::DragFloat("Min Rotation Speed", &config.minRotationSpeed, 0.1f, -100.0f, 100.0f)) changed = true;
        if (ImGui::DragFloat("Max Rotation Speed", &config.maxRotationSpeed, 0.1f, -100.0f, 100.0f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Life Settings")) {
        if (ImGui::DragFloat("Min Life", &config.minLife, 0.1f, 0.0f, 100.0f)) changed = true;
        if (ImGui::DragFloat("Max Life", &config.maxLife, 0.1f, 0.0f, 100.0f)) changed = true;
    }

    if (ImGui::CollapsingHeader("Color Settings")) {
        float startColor[4] = { config.startR, config.startG, config.startB, config.startA };
        if (ImGui::ColorEdit4("Start Color", startColor)) {
            config.startR = startColor[0];
            config.startG = startColor[1];
            config.startB = startColor[2];
            config.startA = startColor[3];
            changed = true;
        }
        float endColor[4] = { config.endR, config.endG, config.endB, config.endA };
        if (ImGui::ColorEdit4("End Color", endColor)) {
            config.endR = endColor[0];
            config.endG = endColor[1];
            config.endB = endColor[2];
            config.endA = endColor[3];
            changed = true;
        }
    }

    if (changed) {
        presetName = "Custom";
        emitter.config = config;
    }

    ImGui::Spacing();
    ImGui::Text("Editor Preview");
    ImGui::Separator();
    if (ImGui::Button("Play")) {
        Play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        Stop();
    }
    ImGui::SameLine();
    if (ImGui::Button("Burst (Emit 10)")) {
        Emit(10);
    }
    ImGui::Text("Active Particles: %d / %d", emitter.GetActiveCount(), config.maxParticles);
#endif
}
