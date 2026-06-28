#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
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

#ifdef MOLGA_MARROW_SUPPORT
    // Marrow Runtime objects
    std::shared_ptr<const marrow::runtime::SkeletonData> skeletonData;
    std::shared_ptr<const marrow::runtime::AtlasData> atlasData;
    std::unique_ptr<marrow::runtime::Skeleton> skeleton;
    std::unique_ptr<marrow::runtime::AnimationState> animationState;

    // Mix caching: {(from_anim, to_anim) -> mix_duration_seconds}
    std::map<std::pair<std::string, std::string>, float> customMixDurations;

    // OpenGL texture
    Texture* texture = nullptr;

    // Local mesh buffers for rendering (if we want to draw custom meshes)
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;

    // Preallocated buffers to prevent per-frame heap allocations (reused across draws)
    std::vector<float> vboData;
    std::vector<unsigned int> indices;

    void SetupGLBuffers();
    void CleanGLBuffers();
#endif
};
