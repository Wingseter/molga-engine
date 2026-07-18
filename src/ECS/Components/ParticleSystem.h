#pragma once

#include "../Component.h"
#include "../../Systems/Particle.h"
#include "../../Rendering/WorldSort2D.h"

#include <string>
#include <vector>

class ParticleSystem : public Component {
public:
    COMPONENT_TYPE(ParticleSystem)

    ParticleSystem();

    void Play();
    void Pause();
    void Resume();
    void Stop();
    void Clear();
    void Emit(int count);

    bool IsPlaying() const { return emitter.IsEmitting(); }
    bool IsPaused() const { return emitter.IsPaused(); }

    int GetSortingOrder() const { return sortingOrder; }
    void SetSortingOrder(int order) { sortingOrder = order; }
    void SetSortingLayer(const std::string& layer) { sortingLayer = layer; }
    const std::string& GetSortingLayer() const { return sortingLayer; }
    void SetSortMode(molga::SortMode2D mode) { sortMode = mode; }
    molga::SortMode2D GetSortMode() const { return sortMode; }
    void SetYSortOffset(float offset) { ySortOffset = offset; }
    float GetYSortOffset() const { return ySortOffset; }
    molga::WorldSortSettings2D GetWorldSortSettings() const {
        return {sortingLayer, sortingOrder, sortMode, ySortOffset};
    }
    BlendMode GetBlendMode() const {
        return useAdditiveBlending ? BlendMode::Additive : BlendMode::Alpha;
    }
    void SetBlendMode(BlendMode mode) {
        useAdditiveBlending = mode == BlendMode::Additive;
    }

    // Lifecycle
    void Start() override;
    void Update(float dt) override;
    void RenderSprite(Renderer* renderer) override;
    void CollectRender(molga::RenderQueue& queue) override;
    void ResolveAssets() override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

    // Runtime-only editor preview. The copy and its clock/pool are never
    // serialized and never mutate the authored/live emitter.
    ParticleEmitter CreatePreviewEmitter() const;
    ParticleEmitter& GetEditorPreviewEmitter();
    const ParticleEmitter* TryGetEditorPreviewEmitter() const;
    void ResetEditorPreview();
    void UpdateEditorPreview(float dt);
    void CollectEditorPreviewRender(molga::RenderQueue& queue);

    // Exposed legacy configuration fields/API remain source compatible.
    ParticleConfig config;
    bool playOnAwake = true;
    bool looping = true;
    std::string presetName = "Custom";
    bool useAdditiveBlending = false;
    int sortingOrder = 0;

    // A negative value preserves legacy behavior by using config.maxLife as
    // the one-shot emitter duration. Schema v2 always serializes a positive,
    // explicit duration independent of particle lifetime.
    float durationSeconds = -1.0f;

    const ParticleEmitter& GetEmitter() const { return emitter; }
    ParticleEmitter& GetEmitter() { return emitter; }
    const std::vector<molga::ResolvedSprite>& GetResolvedSprites() const {
        return resolvedSprites_;
    }

private:
    float EffectiveDuration() const;
    void SyncEmitterPosition(ParticleEmitter& target) const;
    void EnsureResolvedSprites();
    void SubmitEmitterGeometry(const ParticleEmitter& target,
                               molga::RenderQueue& queue);

    ParticleEmitter emitter;
    float emissionTime = 0.0f;
    std::vector<molga::ResolvedSprite> resolvedSprites_;
    std::vector<molga::SpriteRef> resolvedSpriteRefs_;

    ParticleEmitter editorPreview_;
    bool editorPreviewInitialized_ = false;
    std::string sortingLayer = "Default";
    molga::SortMode2D sortMode = molga::SortMode2D::Fixed;
    float ySortOffset = 0.0f;
};
