#include "ParticleSystem.h"

#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "Core/SpriteResolver.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Sprite.h"

#include <algorithm>
#include <cmath>
#include <glad/glad.h>

#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

REGISTER_COMPONENT(ParticleSystem)

namespace {

nlohmann::json SerializeFloatCurve(const FloatCurve& source) {
    FloatCurve curve = source;
    curve.Normalize();
    nlohmann::json result;
    result["keys"] = nlohmann::json::array();
    for (const auto& key : curve.keys) {
        result["keys"].push_back({{"time", key.time}, {"value", key.value}});
    }
    return result;
}

FloatCurve DeserializeFloatCurve(const nlohmann::json& value) {
    FloatCurve result;
    if (!value.is_object() || !value.contains("keys") || !value["keys"].is_array()) {
        return result;
    }
    for (const auto& key : value["keys"]) {
        if (!key.is_object()) continue;
        result.keys.push_back({key.value("time", 0.0f), key.value("value", 0.0f)});
    }
    result.Normalize();
    return result;
}

nlohmann::json SerializeColorGradient(const ColorGradient& source) {
    ColorGradient gradient = source;
    gradient.Normalize();
    nlohmann::json result;
    result["keys"] = nlohmann::json::array();
    for (const auto& key : gradient.keys) {
        result["keys"].push_back({
            {"time", key.time},
            {"color", {key.color.r, key.color.g, key.color.b, key.color.a}}
        });
    }
    return result;
}

ColorGradient DeserializeColorGradient(const nlohmann::json& value) {
    ColorGradient result;
    if (!value.is_object() || !value.contains("keys") || !value["keys"].is_array()) {
        return result;
    }
    for (const auto& key : value["keys"]) {
        if (!key.is_object() || !key.contains("color") || !key["color"].is_array() ||
            key["color"].size() != 4U) {
            continue;
        }
        result.keys.push_back({
            key.value("time", 0.0f),
            {key["color"][0].get<float>(), key["color"][1].get<float>(),
             key["color"][2].get<float>(), key["color"][3].get<float>()}
        });
    }
    result.Normalize();
    return result;
}

void SerializeConfig(nlohmann::json& json, const ParticleConfig& source) {
    ParticleConfig config = source;
    config.Normalize();
    json["spawnRate"] = config.spawnRate;
    json["maxParticles"] = config.maxParticles;
    json["spawnRadius"] = config.spawnRadius;
    json["minSpeed"] = config.minSpeed;
    json["maxSpeed"] = config.maxSpeed;
    json["minAngle"] = config.minAngle;
    json["maxAngle"] = config.maxAngle;
    json["gravityX"] = config.gravityX;
    json["gravityY"] = config.gravityY;
    json["startSize"] = config.startSize;
    json["endSize"] = config.endSize;
    json["sizeVariance"] = config.sizeVariance;
    json["sizeOverLife"] = SerializeFloatCurve(config.sizeOverLife);
    json["minRotationSpeed"] = config.minRotationSpeed;
    json["maxRotationSpeed"] = config.maxRotationSpeed;
    json["minLife"] = config.minLife;
    json["maxLife"] = config.maxLife;
    json["startColor"] = {config.startR, config.startG, config.startB, config.startA};
    json["endColor"] = {config.endR, config.endG, config.endB, config.endA};
    json["colorOverLife"] = SerializeColorGradient(config.colorOverLife);
}

void DeserializeConfig(const nlohmann::json& json, ParticleConfig& config) {
    if (!json.is_object()) return;
    if (json.contains("spawnRate")) config.spawnRate = json["spawnRate"];
    if (json.contains("maxParticles")) config.maxParticles = json["maxParticles"];
    if (json.contains("spawnRadius")) config.spawnRadius = json["spawnRadius"];
    if (json.contains("minSpeed")) config.minSpeed = json["minSpeed"];
    if (json.contains("maxSpeed")) config.maxSpeed = json["maxSpeed"];
    if (json.contains("minAngle")) config.minAngle = json["minAngle"];
    if (json.contains("maxAngle")) config.maxAngle = json["maxAngle"];
    if (json.contains("gravityX")) config.gravityX = json["gravityX"];
    if (json.contains("gravityY")) config.gravityY = json["gravityY"];
    if (json.contains("startSize")) config.startSize = json["startSize"];
    if (json.contains("endSize")) config.endSize = json["endSize"];
    if (json.contains("sizeVariance")) config.sizeVariance = json["sizeVariance"];
    if (json.contains("sizeOverLife")) {
        config.sizeOverLife = DeserializeFloatCurve(json["sizeOverLife"]);
    }
    if (json.contains("minRotationSpeed")) {
        config.minRotationSpeed = json["minRotationSpeed"];
    }
    if (json.contains("maxRotationSpeed")) {
        config.maxRotationSpeed = json["maxRotationSpeed"];
    }
    if (json.contains("minLife")) config.minLife = json["minLife"];
    if (json.contains("maxLife")) config.maxLife = json["maxLife"];
    if (json.contains("startColor") && json["startColor"].is_array() &&
        json["startColor"].size() == 4U) {
        config.startR = json["startColor"][0];
        config.startG = json["startColor"][1];
        config.startB = json["startColor"][2];
        config.startA = json["startColor"][3];
    }
    if (json.contains("endColor") && json["endColor"].is_array() &&
        json["endColor"].size() == 4U) {
        config.endR = json["endColor"][0];
        config.endG = json["endColor"][1];
        config.endB = json["endColor"][2];
        config.endA = json["endColor"][3];
    }
    if (json.contains("colorOverLife")) {
        config.colorOverLife = DeserializeColorGradient(json["colorOverLife"]);
    }
}

} // namespace

ParticleSystem::ParticleSystem() {
    emitter.SetConfig(config);
}

void ParticleSystem::Play() {
    emissionTime = 0.0f;
    emitter.Start();
}

void ParticleSystem::Pause() {
    emitter.Pause();
}

void ParticleSystem::Resume() {
    emitter.Resume();
}

void ParticleSystem::Stop() {
    emitter.Stop();
}

void ParticleSystem::Clear() {
    emitter.Clear();
}

void ParticleSystem::Emit(int count) {
    emitter.Burst(count);
}

float ParticleSystem::EffectiveDuration() const {
    const float duration = durationSeconds >= 0.0f ? durationSeconds : config.maxLife;
    return std::isfinite(duration) ? std::max(0.0f, duration) : 0.0f;
}

void ParticleSystem::SyncEmitterPosition(ParticleEmitter& target) const {
    if (!gameObject) return;
    if (Transform* transform = gameObject->GetComponent<Transform>()) {
        const Vector2 position = transform->GetWorldPosition();
        target.SetPosition(position.x, position.y);
    }
}

void ParticleSystem::Start() {
    if (playOnAwake) Play();
}

void ParticleSystem::Update(float dt) {
    if (!gameObject || !std::isfinite(dt) || dt <= 0.0f) return;
    SyncEmitterPosition(emitter);
    emitter.SetSimulationSpace(config.simulationSpace);

    if (emitter.IsPaused() || !emitter.IsEmitting()) {
        emitter.Update(dt);
        return;
    }

    const float duration = EffectiveDuration();
    if (looping) {
        emitter.Update(dt);
        emissionTime += dt;
        if (duration > 0.0f && emissionTime >= duration) {
            emissionTime = std::fmod(emissionTime, duration);
        }
        return;
    }

    const float remaining = std::max(0.0f, duration - emissionTime);
    const float emittingStep = std::min(dt, remaining);
    if (emittingStep > 0.0f) {
        emitter.Update(emittingStep);
        emissionTime += emittingStep;
    }
    if (emissionTime >= duration) emitter.Stop();
    const float particleOnlyStep = dt - emittingStep;
    if (particleOnlyStep > 0.0f) emitter.Update(particleOnlyStep);
}

void ParticleSystem::ResolveAssets() {
    resolvedSpriteRefs_ = config.sprites;
    resolvedSprites_.clear();
    resolvedSprites_.reserve(config.sprites.size());
    for (const auto& reference : config.sprites) {
        resolvedSprites_.push_back(molga::SpriteResolver::Resolve(reference));
    }
}

void ParticleSystem::EnsureResolvedSprites() {
    if (resolvedSpriteRefs_ != config.sprites ||
        resolvedSprites_.size() != config.sprites.size()) {
        ResolveAssets();
    }
}

void ParticleSystem::RenderSprite(Renderer* renderer) {
    if (!renderer || !gameObject || !enabled) return;
    EnsureResolvedSprites();
    if (useAdditiveBlending) glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    const bool textured = !config.sprites.empty();
    const auto& particles = emitter.GetParticles();
    for (const Particle& particle : particles) {
        if (!particle.active || particle.size <= 0.0f) continue;
        const molga::ResolvedSprite* resolved = nullptr;
        if (textured) {
            const std::size_t frame = emitter.FrameIndexForParticle(
                particle, config.sprites.size());
            if (frame >= resolvedSprites_.size() || !resolvedSprites_[frame].valid) continue;
            resolved = &resolvedSprites_[frame];
        }

        const float offsetX = config.simulationSpace == ParticleSimulationSpace::Local
            ? emitter.x : 0.0f;
        const float offsetY = config.simulationSpace == ParticleSimulationSpace::Local
            ? emitter.y : 0.0f;
        const Vector2 pivot = resolved ? resolved->pivot : Vector2{0.5f, 0.5f};
        Sprite sprite;
        sprite.SetPosition(particle.x + offsetX - pivot.x * particle.size,
                           particle.y + offsetY - pivot.y * particle.size);
        sprite.SetSize(particle.size, particle.size);
        sprite.SetColor(particle.r, particle.g, particle.b, particle.a);
        sprite.SetRotation(particle.rotation * 180.0f / Constants::PI);
        if (resolved) {
            sprite.SetTexture(resolved->texture);
            sprite.SetUV(resolved->uv.u0, resolved->uv.v0,
                         resolved->uv.u1, resolved->uv.v1);
        }
        renderer->DrawSprite(&sprite);
    }

    if (useAdditiveBlending) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ParticleSystem::SubmitEmitterGeometry(const ParticleEmitter& target,
                                           molga::RenderQueue& queue) {
    EnsureResolvedSprites();
    const auto batches = target.BuildGeometry(resolvedSprites_);
    float worldY = 0.0f;
    if (gameObject) {
        if (const Transform* transform = gameObject->GetComponent<Transform>()) {
            worldY = transform->GetWorldPosition().y;
        }
    }
    const molga::SortKey componentSortKey =
        molga::MakeWorldSortKey(GetWorldSortSettings(), worldY);
    for (const auto& batch : batches) {
        if (!batch.geometry || batch.geometry->empty()) continue;
        molga::RenderCommand command;
        command.sortKey = componentSortKey;
        command.batchKey.shader = ShaderManager::Get().Get("batch");
        command.batchKey.texture = batch.texture;
        command.batchKey.blendMode = GetBlendMode();
        command.batchKey.isBatchable = true;
        command.geometry = batch.geometry;
        command.worldBounds = batch.worldBounds;
        queue.Submit(command);
    }
}

void ParticleSystem::CollectRender(molga::RenderQueue& queue) {
    if (!gameObject || !enabled) return;
    SubmitEmitterGeometry(emitter, queue);
}

void ParticleSystem::Serialize(nlohmann::json& json) const {
    Component::Serialize(json);
    json["schemaVersion"] = 2;
    json["playOnAwake"] = playOnAwake;
    json["looping"] = looping;
    json["durationSeconds"] = EffectiveDuration();
    json["presetName"] = presetName;
    json["blendMode"] = useAdditiveBlending ? "Additive" : "Alpha";
    json["useAdditiveBlending"] = useAdditiveBlending;
    molga::SerializeWorldSortSettings(json, GetWorldSortSettings());
    json["simulationSpace"] = ToString(config.simulationSpace);
    json["seed"] = config.seed;
    json["frameMode"] = ToString(config.frameMode);
    json["sprites"] = nlohmann::json::array();
    for (const auto& sprite : config.sprites) {
        json["sprites"].push_back(molga::SerializeSpriteRef(sprite));
    }
    nlohmann::json configJson;
    SerializeConfig(configJson, config);
    json["config"] = std::move(configJson);
}

void ParticleSystem::Deserialize(const nlohmann::json& json) {
    Component::Deserialize(json);
    const molga::WorldSortSettings2D worldSort =
        molga::DeserializeWorldSortSettings(json);
    sortingLayer = worldSort.sortingLayer;
    sortingOrder = worldSort.sortingOrder;
    sortMode = worldSort.sortMode;
    ySortOffset = worldSort.ySortOffset;
    const int schemaVersion = json.value("schemaVersion", 1);
    if (json.contains("playOnAwake")) playOnAwake = json["playOnAwake"];
    if (json.contains("looping")) looping = json["looping"];
    if (json.contains("presetName")) presetName = json["presetName"];
    if (json.contains("useAdditiveBlending")) {
        useAdditiveBlending = json["useAdditiveBlending"];
    }
    if (json.contains("blendMode") && json["blendMode"].is_string()) {
        useAdditiveBlending = json["blendMode"].get<std::string>() == "Additive";
    }
    if (json.contains("config")) DeserializeConfig(json["config"], config);

    if (schemaVersion >= 2) {
        durationSeconds = json.value("durationSeconds", config.maxLife);
        config.simulationSpace = ParticleSimulationSpaceFromString(
            json.value("simulationSpace", std::string("World")));
        config.seed = json.value("seed", std::uint32_t{1});
        config.frameMode = ParticleFrameModeFromString(
            json.value("frameMode", std::string("Start")));
        config.sprites.clear();
        if (json.contains("sprites") && json["sprites"].is_array()) {
            for (const auto& value : json["sprites"]) {
                config.sprites.push_back(molga::DeserializeSpriteRef(value));
            }
        }
    } else {
        // Legacy emitters used maxLife for both concepts. Preserve their timing
        // while materializing independent v2 curves and duration in memory.
        durationSeconds = config.maxLife;
        config.simulationSpace = ParticleSimulationSpace::World;
        config.frameMode = ParticleFrameMode::Start;
        config.seed = 1;
        config.sprites.clear();
    }

    config.Normalize();
    emitter.SetConfig(config);
    emissionTime = 0.0f;
    resolvedSprites_.clear();
    resolvedSpriteRefs_.clear();
    editorPreviewInitialized_ = false;
}

ParticleEmitter ParticleSystem::CreatePreviewEmitter() const {
    ParticleEmitter preview;
    preview.SetConfig(config);
    SyncEmitterPosition(preview);
    return preview;
}

ParticleEmitter& ParticleSystem::GetEditorPreviewEmitter() {
    if (!editorPreviewInitialized_) {
        editorPreview_ = CreatePreviewEmitter();
        editorPreviewInitialized_ = true;
    }
    return editorPreview_;
}

const ParticleEmitter* ParticleSystem::TryGetEditorPreviewEmitter() const {
    return editorPreviewInitialized_ ? &editorPreview_ : nullptr;
}

void ParticleSystem::ResetEditorPreview() {
    editorPreview_ = CreatePreviewEmitter();
    editorPreviewInitialized_ = true;
}

void ParticleSystem::UpdateEditorPreview(float dt) {
    if (!editorPreviewInitialized_) return;
    SyncEmitterPosition(editorPreview_);
    editorPreview_.Update(dt);
}

void ParticleSystem::CollectEditorPreviewRender(molga::RenderQueue& queue) {
    if (!editorPreviewInitialized_ || !enabled) return;
    SubmitEmitterGeometry(editorPreview_, queue);
}

void ParticleSystem::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    const char* presets[] = {"Custom", "Fire", "Smoke", "Spark", "Snow", "Explosion"};
    int currentPreset = 0;
    for (int i = 0; i < 6; ++i) {
        if (presetName == presets[i]) currentPreset = i;
    }
    if (ImGui::Combo("Preset", &currentPreset, presets, 6)) {
        presetName = presets[currentPreset];
        if (presetName == "Fire") config = ParticlePresets::Fire();
        else if (presetName == "Smoke") config = ParticlePresets::Smoke();
        else if (presetName == "Spark") config = ParticlePresets::Spark();
        else if (presetName == "Snow") config = ParticlePresets::Snow();
        else if (presetName == "Explosion") config = ParticlePresets::Explosion();
        config.Normalize();
        emitter.SetConfig(config);
        ResetEditorPreview();
    }

    ImGui::Spacing();
    ImGui::Text("General Settings");
    ImGui::Separator();
    ImGui::Checkbox("Play on Awake", &playOnAwake);
    ImGui::Checkbox("Looping", &looping);
    ImGui::Checkbox("Use Additive Blending", &useAdditiveBlending);
    ImGui::DragInt("Sorting Order", &sortingOrder, 1);
    float duration = EffectiveDuration();
    if (ImGui::DragFloat("Emitter Duration", &duration, 0.05f, 0.0f, 10000.0f)) {
        durationSeconds = duration;
    }
    int simulationSpace = config.simulationSpace == ParticleSimulationSpace::Local ? 0 : 1;
    if (ImGui::Combo("Simulation Space", &simulationSpace, "Local\0World\0")) {
        config.simulationSpace = simulationSpace == 0
            ? ParticleSimulationSpace::Local : ParticleSimulationSpace::World;
        emitter.SetSimulationSpace(config.simulationSpace);
    }
    int frameMode = static_cast<int>(config.frameMode);
    if (ImGui::Combo("Frame Mode", &frameMode, "Start\0Random\0Over Life\0")) {
        config.frameMode = static_cast<ParticleFrameMode>(frameMode);
    }
    int seed = static_cast<int>(config.seed);
    if (ImGui::InputInt("Seed", &seed)) {
        config.seed = static_cast<std::uint32_t>(std::max(0, seed));
        emitter.SetSeed(config.seed);
    }

    bool changed = false;
    if (ImGui::CollapsingHeader("Spawn Settings")) {
        if (ImGui::DragFloat("Spawn Rate", &config.spawnRate, 0.5f, 0.0f, 1000.0f)) changed = true;
        int maxParticles = config.maxParticles;
        if (ImGui::DragInt("Max Particles", &maxParticles, 1, 1, 100000)) {
            config.maxParticles = maxParticles;
            changed = true;
        }
        if (ImGui::DragFloat("Spawn Radius", &config.spawnRadius, 0.5f, 0.0f, 1000.0f)) changed = true;
    }
    if (ImGui::CollapsingHeader("Velocity Settings")) {
        if (ImGui::DragFloat("Min Speed", &config.minSpeed, 0.5f)) changed = true;
        if (ImGui::DragFloat("Max Speed", &config.maxSpeed, 0.5f)) changed = true;
        if (ImGui::SliderAngle("Min Angle", &config.minAngle, -360.0f, 360.0f)) changed = true;
        if (ImGui::SliderAngle("Max Angle", &config.maxAngle, -360.0f, 360.0f)) changed = true;
    }
    if (ImGui::CollapsingHeader("Gravity Settings")) {
        if (ImGui::DragFloat("Gravity X", &config.gravityX, 0.5f)) changed = true;
        if (ImGui::DragFloat("Gravity Y", &config.gravityY, 0.5f)) changed = true;
    }
    if (ImGui::CollapsingHeader("Size Settings")) {
        if (ImGui::DragFloat("Start Size", &config.startSize, 0.5f, 0.0f)) changed = true;
        if (ImGui::DragFloat("End Size", &config.endSize, 0.5f, 0.0f)) changed = true;
        if (ImGui::DragFloat("Size Variance", &config.sizeVariance, 0.5f, 0.0f)) changed = true;
        if (changed) config.sizeOverLife.keys.clear();
    }
    if (ImGui::CollapsingHeader("Rotation Settings")) {
        if (ImGui::DragFloat("Min Rotation Speed", &config.minRotationSpeed, 0.1f)) changed = true;
        if (ImGui::DragFloat("Max Rotation Speed", &config.maxRotationSpeed, 0.1f)) changed = true;
    }
    if (ImGui::CollapsingHeader("Life Settings")) {
        if (ImGui::DragFloat("Min Life", &config.minLife, 0.1f, 0.0001f)) changed = true;
        if (ImGui::DragFloat("Max Life", &config.maxLife, 0.1f, 0.0001f)) changed = true;
    }
    if (ImGui::CollapsingHeader("Color Settings")) {
        float startColor[4] = {config.startR, config.startG, config.startB, config.startA};
        if (ImGui::ColorEdit4("Start Color", startColor)) {
            config.startR = startColor[0]; config.startG = startColor[1];
            config.startB = startColor[2]; config.startA = startColor[3];
            config.colorOverLife.keys.clear();
            changed = true;
        }
        float endColor[4] = {config.endR, config.endG, config.endB, config.endA};
        if (ImGui::ColorEdit4("End Color", endColor)) {
            config.endR = endColor[0]; config.endG = endColor[1];
            config.endB = endColor[2]; config.endA = endColor[3];
            config.colorOverLife.keys.clear();
            changed = true;
        }
    }
    if (changed) {
        presetName = "Custom";
        config.Normalize();
        emitter.SetConfig(config);
        ResetEditorPreview();
    }

    ImGui::Spacing();
    ImGui::Text("Editor Preview (isolated)");
    ImGui::Separator();
    ParticleEmitter& preview = GetEditorPreviewEmitter();
    if (ImGui::Button("Play")) preview.Start();
    ImGui::SameLine();
    if (ImGui::Button("Pause")) preview.Pause();
    ImGui::SameLine();
    if (ImGui::Button("Resume")) preview.Resume();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) preview.Stop();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) preview.Clear();
    ImGui::SameLine();
    if (ImGui::Button("Burst 10")) preview.Burst(10);
    UpdateEditorPreview(ImGui::GetIO().DeltaTime);
    ImGui::Text("Preview particles: %d / %d", preview.GetActiveCount(), config.maxParticles);
#endif
}
