#pragma once

#include "Rendering/SpriteRef.h"

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace molga {

struct AnimationFrame2D {
    std::string sliceId;
    float durationSeconds = 1.0f / 12.0f;
};

// A variable-duration sprite animation. The serialized contract intentionally
// references one texture and stable slice IDs instead of transient grid indices.
class AnimationClip2D {
public:
    static constexpr int SchemaVersion = 1;
    static constexpr float FallbackFrameDuration = 1.0f / 60.0f;

    const std::string& GetTextureGuid() const { return textureGuid_; }
    void SetTextureGuid(std::string value) { textureGuid_ = std::move(value); }

    bool IsLooping() const { return loop_; }
    void SetLooping(bool value) { loop_ = value; }

    const std::vector<AnimationFrame2D>& GetFrames() const { return frames_; }
    std::vector<AnimationFrame2D>& GetFrames() { return frames_; }
    void SetFrames(std::vector<AnimationFrame2D> value) { frames_ = std::move(value); }

    bool Validate(std::string* errorOut = nullptr) const;
    float GetFrameDuration(std::size_t index) const;
    float GetDurationSeconds() const;
    std::size_t GetFrameIndexAt(float elapsedSeconds) const;
    SpriteRef GetSpriteRef(std::size_t frameIndex) const;

    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& document, std::string* errorOut = nullptr);
    bool LoadFromFile(const std::filesystem::path& path, std::string* errorOut = nullptr);
    bool SaveToFile(const std::filesystem::path& path, std::string* errorOut = nullptr) const;

private:
    std::string textureGuid_;
    bool loop_ = true;
    std::vector<AnimationFrame2D> frames_;
};

} // namespace molga

using AnimationClip2D = molga::AnimationClip2D;
using AnimationFrame2D = molga::AnimationFrame2D;
