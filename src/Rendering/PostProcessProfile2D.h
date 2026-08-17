#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace molga {

enum class PostProcessEffectType2D {
    Bloom,
    ColorAdjust,
    Vignette,
};

const char* PostProcessEffectTypeName(PostProcessEffectType2D type);

struct BloomSettings2D {
    bool enabled = true;
    float threshold = 0.8f;
    float softKnee = 0.5f;
    float intensity = 0.6f;
    float scatter = 0.7f;
    nlohmann::json preserved = nlohmann::json::object();

    bool IsActive() const { return enabled && intensity != 0.0f; }
};

struct ColorAdjustSettings2D {
    bool enabled = true;
    float exposureEV = 0.0f;
    float contrast = 0.0f;
    float saturation = 1.0f;
    float tint[3] = {1.0f, 1.0f, 1.0f};
    nlohmann::json preserved = nlohmann::json::object();

    bool IsActive() const {
        return enabled &&
               (exposureEV != 0.0f || contrast != 0.0f || saturation != 1.0f ||
                tint[0] != 1.0f || tint[1] != 1.0f || tint[2] != 1.0f);
    }
};

struct VignetteSettings2D {
    bool enabled = true;
    float intensity = 0.2f;
    float smoothness = 0.5f;
    float color[3] = {0.0f, 0.0f, 0.0f};
    nlohmann::json preserved = nlohmann::json::object();

    bool IsActive() const { return enabled && intensity != 0.0f; }
};

using PostProcessEffectSettings2D =
    std::variant<BloomSettings2D, ColorAdjustSettings2D, VignetteSettings2D>;

struct PostProcessEffect2D {
    PostProcessEffectType2D type = PostProcessEffectType2D::Bloom;
    PostProcessEffectSettings2D settings = BloomSettings2D{};

    bool IsEnabled() const;
    bool IsActive() const;
};

// Strict, ordered .postfx schema v1. Unknown object keys are retained in the
// preserved documents and written back, but never participate in execution.
struct PostProcessProfile2D {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    std::vector<PostProcessEffect2D> effects;
    nlohmann::json preserved = nlohmann::json::object();

    static bool Deserialize(const nlohmann::json& document,
                            PostProcessProfile2D& out,
                            std::string* errorOut = nullptr);
    static bool LoadFromFile(const std::filesystem::path& path,
                             PostProcessProfile2D& out,
                             std::string* errorOut = nullptr);

    nlohmann::json Serialize() const;
    bool SaveToFile(const std::filesystem::path& path,
                    std::string* errorOut = nullptr) const;

    std::size_t ActiveEffectCount() const;
    bool HasActiveEffects() const { return ActiveEffectCount() != 0; }
};

} // namespace molga
