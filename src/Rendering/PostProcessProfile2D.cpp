#include "Rendering/PostProcessProfile2D.h"

#include "Core/PersistentStorage.h"

#include <cmath>
#include <fstream>
#include <unordered_set>
#include <type_traits>

namespace molga {
namespace {

void SetError(std::string* output, const std::string& message) {
    if (output) *output = message;
}

bool ReadBool(const nlohmann::json& object, const char* key, bool fallback,
              bool& value, std::string* error) {
    const auto found = object.find(key);
    if (found == object.end()) {
        value = fallback;
        return true;
    }
    if (!found->is_boolean()) {
        SetError(error, std::string("'") + key + "' must be a boolean");
        return false;
    }
    value = found->get<bool>();
    return true;
}

bool ReadFloat(const nlohmann::json& object, const char* key, float fallback,
               float minimum, float maximum, float& value, std::string* error) {
    const auto found = object.find(key);
    if (found == object.end()) {
        value = fallback;
        return true;
    }
    if (!found->is_number()) {
        SetError(error, std::string("'") + key + "' must be a number");
        return false;
    }
    double parsed = 0.0;
    try {
        parsed = found->get<double>();
    } catch (const nlohmann::json::exception&) {
        SetError(error, std::string("'") + key + "' is not a finite number");
        return false;
    }
    if (!std::isfinite(parsed)) {
        SetError(error, std::string("'") + key + "' must be finite");
        return false;
    }
    if (parsed < static_cast<double>(minimum) ||
        parsed > static_cast<double>(maximum)) {
        SetError(error, std::string("'") + key + "' is outside [" +
                            std::to_string(minimum) + ", " +
                            std::to_string(maximum) + "]");
        return false;
    }
    value = static_cast<float>(parsed);
    return true;
}

bool ReadVec3(const nlohmann::json& object, const char* key,
              const float fallback[3], float minimum, float maximum,
              float value[3], std::string* error) {
    const auto found = object.find(key);
    if (found == object.end()) {
        for (int i = 0; i < 3; ++i) value[i] = fallback[i];
        return true;
    }
    if (!found->is_array() || found->size() != 3) {
        SetError(error, std::string("'") + key + "' must be a three-number array");
        return false;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& item = (*found)[index];
        if (!item.is_number()) {
            SetError(error, std::string("'") + key + "' must contain only numbers");
            return false;
        }
        double parsed = 0.0;
        try {
            parsed = item.get<double>();
        } catch (const nlohmann::json::exception&) {
            SetError(error, std::string("'") + key + "' contains an invalid number");
            return false;
        }
        if (!std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
            SetError(error, std::string("'") + key + "' values must be finite and inside [" +
                                std::to_string(minimum) + ", " +
                                std::to_string(maximum) + "]");
            return false;
        }
        value[index] = static_cast<float>(parsed);
    }
    return true;
}

nlohmann::json SerializeEffect(const PostProcessEffect2D& effect) {
    return std::visit([&effect](const auto& settings) {
        nlohmann::json object = settings.preserved.is_object()
            ? settings.preserved : nlohmann::json::object();
        object["type"] = PostProcessEffectTypeName(effect.type);
        object["enabled"] = settings.enabled;
        using Settings = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<Settings, BloomSettings2D>) {
            object["threshold"] = settings.threshold;
            object["softKnee"] = settings.softKnee;
            object["intensity"] = settings.intensity;
            object["scatter"] = settings.scatter;
        } else if constexpr (std::is_same_v<Settings, ColorAdjustSettings2D>) {
            object["exposureEV"] = settings.exposureEV;
            object["contrast"] = settings.contrast;
            object["saturation"] = settings.saturation;
            object["tint"] = {settings.tint[0], settings.tint[1], settings.tint[2]};
        } else {
            object["intensity"] = settings.intensity;
            object["smoothness"] = settings.smoothness;
            object["color"] = {settings.color[0], settings.color[1], settings.color[2]};
        }
        return object;
    }, effect.settings);
}

} // namespace

const char* PostProcessEffectTypeName(PostProcessEffectType2D type) {
    switch (type) {
        case PostProcessEffectType2D::Bloom: return "Bloom";
        case PostProcessEffectType2D::ColorAdjust: return "ColorAdjust";
        case PostProcessEffectType2D::Vignette: return "Vignette";
    }
    return "Unknown";
}

bool PostProcessEffect2D::IsEnabled() const {
    return std::visit([](const auto& value) { return value.enabled; }, settings);
}

bool PostProcessEffect2D::IsActive() const {
    return std::visit([](const auto& value) { return value.IsActive(); }, settings);
}

bool PostProcessProfile2D::Deserialize(const nlohmann::json& document,
                                       PostProcessProfile2D& out,
                                       std::string* errorOut) {
    SetError(errorOut, {});
    if (!document.is_object()) {
        SetError(errorOut, "post-process profile root must be an object");
        return false;
    }
    const auto schema = document.find("schemaVersion");
    bool hasSupportedSchema = false;
    if (schema != document.end()) {
        if (schema->is_number_unsigned()) {
            hasSupportedSchema =
                schema->get<std::uint64_t>() ==
                static_cast<std::uint64_t>(kSchemaVersion);
        } else if (schema->is_number_integer()) {
            hasSupportedSchema =
                schema->get<std::int64_t>() ==
                static_cast<std::int64_t>(kSchemaVersion);
        }
    }
    if (!hasSupportedSchema) {
        SetError(errorOut, "post-process profile schemaVersion must be integer 1");
        return false;
    }
    const auto effects = document.find("effects");
    if (effects == document.end() || !effects->is_array()) {
        SetError(errorOut, "post-process profile effects must be an array");
        return false;
    }

    PostProcessProfile2D parsed;
    parsed.preserved = document;
    std::unordered_set<PostProcessEffectType2D> unique;
    parsed.effects.reserve(effects->size());
    for (std::size_t index = 0; index < effects->size(); ++index) {
        const nlohmann::json& object = (*effects)[index];
        const std::string prefix = "effect[" + std::to_string(index) + "]: ";
        if (!object.is_object()) {
            SetError(errorOut, prefix + "entry must be an object");
            return false;
        }
        const auto typeValue = object.find("type");
        if (typeValue == object.end() || !typeValue->is_string()) {
            SetError(errorOut, prefix + "type must be a string");
            return false;
        }
        const std::string typeName = typeValue->get<std::string>();
        PostProcessEffect2D effect;
        std::string localError;
        if (typeName == "Bloom") {
            effect.type = PostProcessEffectType2D::Bloom;
            BloomSettings2D value;
            value.preserved = object;
            if (!ReadBool(object, "enabled", value.enabled, value.enabled, &localError) ||
                !ReadFloat(object, "threshold", value.threshold, 0.0f, 16.0f,
                           value.threshold, &localError) ||
                !ReadFloat(object, "softKnee", value.softKnee, 0.0f, 1.0f,
                           value.softKnee, &localError) ||
                !ReadFloat(object, "intensity", value.intensity, 0.0f, 10.0f,
                           value.intensity, &localError) ||
                !ReadFloat(object, "scatter", value.scatter, 0.0f, 1.0f,
                           value.scatter, &localError)) {
                SetError(errorOut, prefix + localError);
                return false;
            }
            effect.settings = std::move(value);
        } else if (typeName == "ColorAdjust") {
            effect.type = PostProcessEffectType2D::ColorAdjust;
            ColorAdjustSettings2D value;
            value.preserved = object;
            const float tintDefault[3] = {1.0f, 1.0f, 1.0f};
            if (!ReadBool(object, "enabled", value.enabled, value.enabled, &localError) ||
                !ReadFloat(object, "exposureEV", value.exposureEV, -8.0f, 8.0f,
                           value.exposureEV, &localError) ||
                !ReadFloat(object, "contrast", value.contrast, -1.0f, 1.0f,
                           value.contrast, &localError) ||
                !ReadFloat(object, "saturation", value.saturation, 0.0f, 2.0f,
                           value.saturation, &localError) ||
                !ReadVec3(object, "tint", tintDefault, 0.0f, 4.0f,
                          value.tint, &localError)) {
                SetError(errorOut, prefix + localError);
                return false;
            }
            effect.settings = std::move(value);
        } else if (typeName == "Vignette") {
            effect.type = PostProcessEffectType2D::Vignette;
            VignetteSettings2D value;
            value.preserved = object;
            const float colorDefault[3] = {0.0f, 0.0f, 0.0f};
            if (!ReadBool(object, "enabled", value.enabled, value.enabled, &localError) ||
                !ReadFloat(object, "intensity", value.intensity, 0.0f, 1.0f,
                           value.intensity, &localError) ||
                !ReadFloat(object, "smoothness", value.smoothness, 0.01f, 1.0f,
                           value.smoothness, &localError) ||
                !ReadVec3(object, "color", colorDefault, 0.0f, 1.0f,
                          value.color, &localError)) {
                SetError(errorOut, prefix + localError);
                return false;
            }
            effect.settings = std::move(value);
        } else {
            SetError(errorOut, prefix + "unsupported effect type '" + typeName + "'");
            return false;
        }
        if (!unique.insert(effect.type).second) {
            SetError(errorOut, prefix + "duplicate effect type '" + typeName + "'");
            return false;
        }
        parsed.effects.push_back(std::move(effect));
    }
    out = std::move(parsed);
    return true;
}

bool PostProcessProfile2D::LoadFromFile(const std::filesystem::path& path,
                                        PostProcessProfile2D& out,
                                        std::string* errorOut) {
    try {
        std::ifstream input(path);
        if (!input) {
            SetError(errorOut, "could not open post-process profile: " + path.string());
            return false;
        }
        nlohmann::json document;
        input >> document;
        return Deserialize(document, out, errorOut);
    } catch (const std::exception& error) {
        SetError(errorOut, std::string("invalid post-process profile: ") + error.what());
        return false;
    }
}

nlohmann::json PostProcessProfile2D::Serialize() const {
    nlohmann::json document = preserved.is_object()
        ? preserved : nlohmann::json::object();
    document["schemaVersion"] = kSchemaVersion;
    document["effects"] = nlohmann::json::array();
    for (const PostProcessEffect2D& effect : effects) {
        document["effects"].push_back(SerializeEffect(effect));
    }
    return document;
}

bool PostProcessProfile2D::SaveToFile(const std::filesystem::path& path,
                                      std::string* errorOut) const {
    PostProcessProfile2D validated;
    std::string validationError;
    const nlohmann::json document = Serialize();
    if (!Deserialize(document, validated, &validationError)) {
        SetError(errorOut, validationError);
        return false;
    }
    if (!PersistentStorage::AtomicWriteText(path, document.dump(2) + '\n')) {
        SetError(errorOut, "could not atomically save post-process profile");
        return false;
    }
    SetError(errorOut, {});
    return true;
}

std::size_t PostProcessProfile2D::ActiveEffectCount() const {
    std::size_t count = 0;
    for (const PostProcessEffect2D& effect : effects) {
        if (effect.IsActive()) ++count;
    }
    return count;
}

} // namespace molga
