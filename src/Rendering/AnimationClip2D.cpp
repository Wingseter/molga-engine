#include "Rendering/AnimationClip2D.h"

#include "Core/Guid.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

namespace molga {
namespace {

void SetError(std::string* out, std::string value) {
    if (out) *out = std::move(value);
}

bool WriteJsonAtomically(const std::filesystem::path& path,
                         const nlohmann::json& value,
                         std::string* errorOut) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    try {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) {
                SetError(errorOut, "could not open temporary asset: " + temporary.string());
                return false;
            }
            stream << value.dump(2) << '\n';
            if (!stream) {
                SetError(errorOut, "could not write temporary asset: " + temporary.string());
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            SetError(errorOut, "could not replace asset: " + ec.message());
            return false;
        }
        SetError(errorOut, {});
        return true;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        SetError(errorOut, error.what());
        return false;
    }
}

} // namespace

bool AnimationClip2D::Validate(std::string* errorOut) const {
    if (!Guid::IsValid(textureGuid_)) {
        SetError(errorOut, "textureGuid must be a 32-character GUID");
        return false;
    }
    if (frames_.empty()) {
        SetError(errorOut, "frames must contain at least one frame");
        return false;
    }
    for (std::size_t index = 0; index < frames_.size(); ++index) {
        const AnimationFrame2D& frame = frames_[index];
        if (!Guid::IsValid(frame.sliceId)) {
            SetError(errorOut, "frame " + std::to_string(index) +
                                   " sliceId must be a 32-character GUID");
            return false;
        }
        if (!std::isfinite(frame.durationSeconds) || frame.durationSeconds <= 0.0f) {
            SetError(errorOut, "frame " + std::to_string(index) +
                                   " durationSeconds must be finite and positive");
            return false;
        }
    }
    SetError(errorOut, {});
    return true;
}

float AnimationClip2D::GetFrameDuration(std::size_t index) const {
    if (index >= frames_.size()) return FallbackFrameDuration;
    const float duration = frames_[index].durationSeconds;
    return std::isfinite(duration) && duration > 0.0f
        ? duration : FallbackFrameDuration;
}

float AnimationClip2D::GetDurationSeconds() const {
    double total = 0.0;
    for (std::size_t index = 0; index < frames_.size(); ++index) {
        total += static_cast<double>(GetFrameDuration(index));
    }
    if (!std::isfinite(total) || total <= 0.0) return 0.0f;
    return static_cast<float>(std::min(
        total, static_cast<double>(std::numeric_limits<float>::max())));
}

std::size_t AnimationClip2D::GetFrameIndexAt(float elapsedSeconds) const {
    if (frames_.empty()) return 0;
    const float duration = GetDurationSeconds();
    if (!std::isfinite(duration) || duration <= 0.0f) return 0;

    double local = std::isfinite(elapsedSeconds)
        ? std::max(0.0, static_cast<double>(elapsedSeconds)) : 0.0;
    if (loop_) {
        local = std::fmod(local, static_cast<double>(duration));
        if (local < 0.0) local += duration;
    } else if (local >= duration) {
        return frames_.size() - 1;
    }

    double cursor = 0.0;
    for (std::size_t index = 0; index < frames_.size(); ++index) {
        cursor += GetFrameDuration(index);
        if (local < cursor) return index;
    }
    return frames_.size() - 1;
}

SpriteRef AnimationClip2D::GetSpriteRef(std::size_t frameIndex) const {
    if (frameIndex >= frames_.size()) return {};
    return {textureGuid_, frames_[frameIndex].sliceId};
}

nlohmann::json AnimationClip2D::ToJson() const {
    nlohmann::json frames = nlohmann::json::array();
    for (const AnimationFrame2D& frame : frames_) {
        frames.push_back({
            {"sliceId", frame.sliceId},
            {"durationSeconds", frame.durationSeconds}
        });
    }
    return {
        {"schemaVersion", SchemaVersion},
        {"textureGuid", textureGuid_},
        {"loop", loop_},
        {"frames", std::move(frames)}
    };
}

bool AnimationClip2D::FromJson(const nlohmann::json& document,
                               std::string* errorOut) {
    try {
        if (!document.is_object()) {
            SetError(errorOut, "animation clip root must be an object");
            return false;
        }
        if (document.value("schemaVersion", SchemaVersion) != SchemaVersion) {
            SetError(errorOut, "unsupported animation clip schemaVersion");
            return false;
        }
        if (!document.contains("textureGuid") ||
            !document["textureGuid"].is_string()) {
            SetError(errorOut, "animation clip textureGuid is required");
            return false;
        }
        if (!document.contains("frames") || !document["frames"].is_array()) {
            SetError(errorOut, "animation clip frames array is required");
            return false;
        }

        AnimationClip2D candidate;
        candidate.textureGuid_ = document["textureGuid"].get<std::string>();
        candidate.loop_ = document.value("loop", true);
        for (const nlohmann::json& value : document["frames"]) {
            if (!value.is_object() || !value.contains("sliceId") ||
                !value["sliceId"].is_string() ||
                !value.contains("durationSeconds") ||
                !value["durationSeconds"].is_number()) {
                SetError(errorOut, "each animation frame requires sliceId and durationSeconds");
                return false;
            }
            candidate.frames_.push_back({
                value["sliceId"].get<std::string>(),
                value["durationSeconds"].get<float>()
            });
        }
        if (!candidate.Validate(errorOut)) return false;
        *this = std::move(candidate);
        return true;
    } catch (const std::exception& error) {
        SetError(errorOut, std::string("invalid animation clip: ") + error.what());
        return false;
    }
}

bool AnimationClip2D::LoadFromFile(const std::filesystem::path& path,
                                   std::string* errorOut) {
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            SetError(errorOut, "could not open animation clip: " + path.string());
            return false;
        }
        nlohmann::json document;
        stream >> document;
        return FromJson(document, errorOut);
    } catch (const std::exception& error) {
        SetError(errorOut, std::string("could not parse animation clip: ") + error.what());
        return false;
    }
}

bool AnimationClip2D::SaveToFile(const std::filesystem::path& path,
                                 std::string* errorOut) const {
    if (!Validate(errorOut)) return false;
    return WriteJsonAtomically(path, ToJson(), errorOut);
}

} // namespace molga
