#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/WorldSort2D.h"
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include <map>
#include <utility>

#ifdef MOLGA_MARROW_SUPPORT
#include <marrow/runtime/skeleton.hpp>
#include <marrow/runtime/animation_state.hpp>
#include <marrow/runtime/atlas.hpp>
#endif

class Texture;

class MarrowRenderer : public Component {
public:
    COMPONENT_TYPE(MarrowRenderer)

    MarrowRenderer() = default;
    virtual ~MarrowRenderer();

    // Set paths (used for deserialization & asset resolving)
    void SetSkeletonPath(const std::string& path) { skeletonPath = path; }
    const std::string& GetSkeletonPath() const { return skeletonPath; }

    void SetAtlasPath(const std::string& path) { atlasPath = path; }
    const std::string& GetAtlasPath() const { return atlasPath; }

    // Playback control
    void PlayAnimation(const std::string& animName, bool loop, int track = 0);
    void SetMix(const std::string& from, const std::string& to, float duration);

    // Sorting order
    void SetSortingOrder(int order) { sortingOrder = order; }
    int GetSortingOrder() const { return sortingOrder; }
    void SetSortingLayer(const std::string& layer) { sortingLayer = layer; }
    const std::string& GetSortingLayer() const { return sortingLayer; }
    void SetSortMode(molga::SortMode2D mode) { sortMode = mode; }
    molga::SortMode2D GetSortMode() const { return sortMode; }
    void SetYSortOffset(float offset) { ySortOffset = offset; }
    float GetYSortOffset() const { return ySortOffset; }
    molga::WorldSortSettings2D GetWorldSortSettings() const {
        return {sortingLayer, sortingOrder, sortMode, ySortOffset};
    }

    // Color/Tint
    void SetColor(const Color& c) { color = c; }
    const Color& GetColor() const { return color; }

    // Component lifecycle
    void Update(float dt) override;
    void Render() override {}
    void RenderSprite(Renderer* renderer) override;
    void CollectRender(molga::RenderQueue& queue) override;
    void ResolveAssets() override { ResolveAssets(false); }
    void ResolveAssets(bool forceReload);
    void OnDestroy() override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Inspector GUI
    void OnInspectorGUI() override;

private:
    std::string skeletonPath;
    std::string atlasPath;
    Color color = Color::White();
    int sortingOrder = 0;
    std::string sortingLayer = "Default";
    molga::SortMode2D sortMode = molga::SortMode2D::Fixed;
    float ySortOffset = 0.0f;

#ifdef MOLGA_MARROW_SUPPORT
    // Marrow Runtime objects
    std::shared_ptr<const marrow::runtime::SkeletonData> skeletonData;
    std::shared_ptr<const marrow::runtime::AtlasData> atlasData;
    std::unique_ptr<marrow::runtime::Skeleton> skeleton;
    std::unique_ptr<marrow::runtime::AnimationState> animationState;

    // Mix caching: {(from_anim, to_anim) -> mix_duration_seconds}
    std::map<std::pair<std::string, std::string>, float> customMixDurations;

    Texture* texture = nullptr;
#endif
};
