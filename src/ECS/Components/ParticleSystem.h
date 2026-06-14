#pragma once

#include "../Component.h"
#include "../../Systems/Particle.h"
#include <string>

class ParticleSystem : public Component {
public:
    COMPONENT_TYPE(ParticleSystem)

    ParticleSystem();

    void Play();
    void Stop();
    void Emit(int count);

    int GetSortingOrder() const { return sortingOrder; }
    void SetSortingOrder(int order) { sortingOrder = order; }

    // Lifecycle
    void Start() override;
    void Update(float dt) override;
    void RenderSprite(Renderer* renderer) override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

    // Exposed configuration fields
    ParticleConfig config;
    bool playOnAwake = true;
    bool looping = true;
    std::string presetName = "Custom";
    bool useAdditiveBlending = false;
    int sortingOrder = 0;

    const ParticleEmitter& GetEmitter() const { return emitter; }
    ParticleEmitter& GetEmitter() { return emitter; }

private:
    ParticleEmitter emitter;
    float emissionTime = 0.0f;
};
