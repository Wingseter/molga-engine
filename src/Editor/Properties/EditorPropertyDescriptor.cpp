#include "Editor/Properties/EditorPropertyDescriptor.h"

#include "Core/AssetDatabase.h"
#include "Core/ProjectSettings.h"
#include "ECS/Component.h"
#include "ECS/GameObject.h"
#include "Scripting/Script.h"
#include "Scripting/ScriptField.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <nlohmann/json.hpp>

namespace molga {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string Humanize(std::string key) {
    if (key.empty()) return key;
    key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    for (std::size_t i = 1; i < key.size(); ++i) {
        if (std::isupper(static_cast<unsigned char>(key[i])) && key[i - 1] != ' ') {
            key.insert(i++, 1, ' ');
        }
    }
    return key;
}

std::string AssetTypeFor(const std::string& componentType, const std::string& key) {
    // Asset types are deliberately explicit. Treating every string containing
    // "guid" as an asset reference allowed an Audio GUID to be assigned to a
    // texture property and only fail later during ResolveAssets().
    if (key == "textureGuid") return "TextureImporter";
    if (key == "mainTextureGuid") return "TextureImporter";
    if (key == "fontGuid") return "FontImporter";
    if (key == "tileSetGuid") return "TileSetImporter";
    if (key == "controllerGuid") return "AnimatorControllerImporter";
    if (key == "prefabGuid") return "PrefabImporter";
    if (componentType == "AudioSource" && key == "clipGuid") return "AudioImporter";
    return {};
}

void SetEnumMetadata(EditorPropertyDescriptor& descriptor,
                     std::vector<std::string> labels,
                     std::vector<EditorPropertyValue> values) {
    if (labels.empty() || labels.size() != values.size()) return;
    descriptor.type = EditorPropertyType::Enum;
    descriptor.enumLabels = std::move(labels);
    descriptor.enumValues = std::move(values);
}

bool IsWorldRendererComponent(const std::string& componentType) {
    return componentType == "SpriteRenderer" ||
           componentType == "TextRenderer2D" ||
           componentType == "ParticleSystem" ||
           componentType == "MarrowRenderer" ||
           componentType == "TilemapRenderer";
}

void ApplyWorldSortMetadata(const std::string& componentType,
                            const std::string& group,
                            const std::string& key,
                            const nlohmann::json& sample,
                            EditorPropertyDescriptor& descriptor) {
    if (!group.empty() || !IsWorldRendererComponent(componentType)) return;
    if (key == "sortMode") {
        SetEnumMetadata(descriptor, {"Fixed", "YAxis"},
                        {std::string{"Fixed"}, std::string{"YAxis"}});
        return;
    }
    if (key != "sortingLayer" || !sample.is_string()) return;

    const std::string authored = sample.get<std::string>();
    const auto& layers = ProjectSettings::Get().sortingLayers;
    std::vector<std::string> labels;
    std::vector<EditorPropertyValue> values;
    labels.reserve(layers.size() + 1U);
    values.reserve(layers.size() + 1U);
    bool foundAuthored = false;
    for (const std::string& layer : layers) {
        labels.push_back(layer);
        values.emplace_back(layer);
        foundAuthored = foundAuthored || layer == authored;
    }
    if (!foundAuthored) {
        labels.push_back(authored.empty()
            ? "⚠ Missing: <empty>"
            : "⚠ Missing: " + authored);
        values.emplace_back(authored);
    }
    SetEnumMetadata(descriptor, std::move(labels), std::move(values));
}

void ApplyKnownMetadata(const std::string& componentType, const std::string& group,
                        const std::string& key, EditorPropertyDescriptor& descriptor) {
    descriptor.assetType = AssetTypeFor(componentType, key);
    if (!descriptor.assetType.empty()) {
        descriptor.type = EditorPropertyType::AssetGuid;
        return;
    }

    if (componentType == "Rigidbody2D" && key == "bodyType") {
        SetEnumMetadata(descriptor, {"Static", "Kinematic", "Dynamic"},
                        {std::int64_t{0}, std::int64_t{1}, std::int64_t{2}});
    } else if (componentType == "TextRenderer2D" && key == "alignment") {
        SetEnumMetadata(descriptor, {"Left", "Center", "Right"},
                        {std::int64_t{0}, std::int64_t{1}, std::int64_t{2}});
    } else if (componentType == "UILabel" && key == "horizontalAlignment") {
        SetEnumMetadata(descriptor, {"Left", "Center", "Right"},
                        {std::int64_t{0}, std::int64_t{1}, std::int64_t{2}});
    } else if (componentType == "UILabel" && key == "verticalAlignment") {
        SetEnumMetadata(descriptor, {"Top", "Middle", "Bottom"},
                        {std::int64_t{0}, std::int64_t{1}, std::int64_t{2}});
    } else if (componentType == "SpriteRenderer" && key == "sizeMode") {
        SetEnumMetadata(descriptor, {"Custom", "Native"},
                        {std::string{"Custom"}, std::string{"Native"}});
    } else if (componentType == "AudioSource" && key == "outputBus") {
        SetEnumMetadata(descriptor, {"Master", "Music", "SFX", "Voice", "UI"},
                        {std::string{"Master"}, std::string{"Music"},
                         std::string{"SFX"}, std::string{"Voice"}, std::string{"UI"}});
    } else if (componentType == "ParticleSystem" && key == "blendMode") {
        SetEnumMetadata(descriptor, {"Alpha", "Additive"},
                        {std::string{"Alpha"}, std::string{"Additive"}});
    } else if (componentType == "ParticleSystem" && key == "simulationSpace") {
        SetEnumMetadata(descriptor, {"Local", "World"},
                        {std::string{"Local"}, std::string{"World"}});
    } else if (componentType == "ParticleSystem" && key == "frameMode") {
        SetEnumMetadata(descriptor, {"Start", "Random", "OverLife"},
                        {std::string{"Start"}, std::string{"Random"},
                         std::string{"OverLife"}});
    } else if (componentType == "SpriteRenderer" && group == "material" &&
               key == "blendMode") {
        SetEnumMetadata(descriptor, {"Opaque", "Alpha", "Additive", "Multiply"},
                        {std::int64_t{0}, std::int64_t{1}, std::int64_t{2},
                         std::int64_t{3}});
    }
}

EditorPropertyValue JsonToValue(const nlohmann::json& value) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer() || value.is_number_unsigned())
        return static_cast<std::int64_t>(value.get<long long>());
    if (value.is_number()) return value.get<double>();
    if (value.is_string()) return value.get<std::string>();
    return std::string{};
}

nlohmann::json ValueToJson(const EditorPropertyValue& value) {
    return std::visit([](const auto& item) -> nlohmann::json { return item; }, value);
}

bool SetJsonPath(Component& component, const std::string& group,
                 const std::string& key, int channel,
                 const EditorPropertyValue& value) {
    nlohmann::json snapshot;
    component.Serialize(snapshot);
    nlohmann::json* owner = &snapshot;
    if (!group.empty()) {
        std::size_t begin = 0;
        while (begin < group.size()) {
            const std::size_t end = group.find('.', begin);
            const std::string segment = group.substr(
                begin, end == std::string::npos ? std::string::npos : end - begin);
            if (!owner->contains(segment) || !(*owner)[segment].is_object()) return false;
            owner = &(*owner)[segment];
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    if (!owner->contains(key)) return false;
    if (channel >= 0) {
        nlohmann::json& array = (*owner)[key];
        if (!array.is_array() || static_cast<std::size_t>(channel) >= array.size()) return false;
        array[static_cast<std::size_t>(channel)] = ValueToJson(value);
    } else {
        (*owner)[key] = ValueToJson(value);
    }
    component.Deserialize(snapshot);
    return true;
}

EditorPropertyValue GetJsonPath(Component& component, const std::string& group,
                                const std::string& key, int channel) {
    nlohmann::json snapshot;
    component.Serialize(snapshot);
    const nlohmann::json* owner = &snapshot;
    if (!group.empty()) {
        std::size_t begin = 0;
        while (begin < group.size()) {
            const std::size_t end = group.find('.', begin);
            const std::string segment = group.substr(
                begin, end == std::string::npos ? std::string::npos : end - begin);
            if (!owner->contains(segment) || !(*owner)[segment].is_object())
                return std::string{};
            owner = &(*owner)[segment];
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    if (!owner->contains(key)) return std::string{};
    const nlohmann::json& value = (*owner)[key];
    if (channel >= 0) {
        if (!value.is_array() || static_cast<std::size_t>(channel) >= value.size())
            return std::string{};
        return JsonToValue(value[static_cast<std::size_t>(channel)]);
    }
    return JsonToValue(value);
}

ScriptFieldRef* FindScriptField(Component& component, const std::string& name) {
    auto* script = dynamic_cast<Script*>(&component);
    if (!script) return nullptr;
    const auto& fields = script->Fields().Fields();
    auto it = std::find_if(fields.begin(), fields.end(),
                           [&name](const ScriptFieldRef& field) {
                               return field.name == name;
                           });
    return it == fields.end() ? nullptr : const_cast<ScriptFieldRef*>(&*it);
}

EditorPropertyDescriptor JsonDescriptor(const std::string& componentType,
                                        const std::string& group,
                                        const std::string& key,
                                        const nlohmann::json& sample,
                                        int channel = -1) {
    EditorPropertyDescriptor descriptor;
    descriptor.group = group;
    descriptor.channel = channel;
    descriptor.key = (group.empty() ? key : group + "." + key) +
        (channel >= 0 ? "." + std::to_string(channel) : "");
    descriptor.label = Humanize(key);
    if (channel >= 0) {
        static const char* axes[] = {"X", "Y", "Z", "W"};
        static const char* colors[] = {"R", "G", "B", "A"};
        const std::string lowerKey = Lower(key);
        const bool color = lowerKey.find("color") != std::string::npos ||
                           lowerKey.find("tint") != std::string::npos;
        descriptor.label += std::string(" ") +
            (color ? colors[std::min(channel, 3)] : axes[std::min(channel, 3)]);
    }
    const nlohmann::json& typed = channel >= 0 ? sample[channel] : sample;
    if (typed.is_boolean()) descriptor.type = EditorPropertyType::Bool;
    else if (typed.is_number_integer() || typed.is_number_unsigned())
        descriptor.type = EditorPropertyType::Integer;
    else if (typed.is_number()) descriptor.type = EditorPropertyType::Float;
    else descriptor.type = EditorPropertyType::String;
    ApplyKnownMetadata(componentType, group, key, descriptor);
    ApplyWorldSortMetadata(componentType, group, key, typed, descriptor);
    descriptor.getter = [group, key, channel](Component& component) {
        return GetJsonPath(component, group, key, channel);
    };
    descriptor.setter = [group, key, channel](Component& component,
                                               const EditorPropertyValue& value) {
        return SetJsonPath(component, group, key, channel, value);
    };
    if (descriptor.type == EditorPropertyType::AssetGuid) {
        descriptor.afterChange = [](Component& component) { component.ResolveAssets(); };
    }
    return descriptor;
}

void AppendJsonDescriptors(std::vector<EditorPropertyDescriptor>& out,
                           const std::string& componentType,
                           const std::string& group,
                           const nlohmann::json& object) {
    if (!object.is_object()) return;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const std::string& key = it.key();
        const auto& value = it.value();
        // Do not expose migration-only mirrors beside their authoritative
        // modern fields. They remain serialized for backwards compatibility.
        if (key == "schemaVersion" ||
            (componentType == "TilemapRenderer" && group.empty() &&
             key == "activeLayerId") ||
            (componentType == "SpriteRenderer" && group.empty() &&
             (key == "texturePath" || key == "textureGuid")) ||
            (componentType == "AudioSource" && group.empty() && key == "clipPath") ||
            (componentType == "SpriteRenderer" && group == "material" &&
             key == "mainTexturePath") ||
            (componentType == "SpriteRenderer" && group == "material" &&
             key == "properties") ||
            (componentType == "ParticleSystem" && group.empty() &&
             key == "useAdditiveBlending")) {
            continue;
        }
        if (value.is_boolean() || value.is_number() || value.is_string()) {
            out.push_back(JsonDescriptor(componentType, group, key, value));
        } else if (value.is_array() && value.size() >= 2 && value.size() <= 4 &&
                   std::all_of(value.begin(), value.end(),
                               [](const nlohmann::json& v) { return v.is_number(); })) {
            for (int channel = 0; channel < static_cast<int>(value.size()); ++channel)
                out.push_back(JsonDescriptor(componentType, group, key, value, channel));
        } else if (value.is_object()) {
            const std::string childGroup = group.empty() ? key : group + "." + key;
            AppendJsonDescriptors(out, componentType, childGroup, value);
        }
    }
}

EditorPropertyDescriptor ScriptDescriptor(const ScriptFieldRef& sample, int channel) {
    EditorPropertyDescriptor descriptor;
    descriptor.group = "fields";
    descriptor.channel = channel;
    descriptor.key = "fields." + sample.name +
        (channel >= 0 ? "." + std::to_string(channel) : "");
    descriptor.label = Humanize(sample.name);
    if (channel >= 0) {
        static const char* vectorChannels[] = {"X", "Y"};
        static const char* colorChannels[] = {"R", "G", "B", "A"};
        descriptor.label += std::string(" ") +
            (sample.type == ScriptFieldType::Color
                ? colorChannels[std::min(channel, 3)]
                : vectorChannels[std::min(channel, 1)]);
    }
    switch (sample.type) {
        case ScriptFieldType::Bool: descriptor.type = EditorPropertyType::Bool; break;
        case ScriptFieldType::Int:
        case ScriptFieldType::ObjectRef: descriptor.type = EditorPropertyType::Integer; break;
        case ScriptFieldType::Enum:
            descriptor.type = EditorPropertyType::Enum;
            descriptor.enumLabels = sample.enumLabels;
            descriptor.enumValues.reserve(sample.enumLabels.size());
            for (std::size_t index = 0; index < sample.enumLabels.size(); ++index) {
                descriptor.enumValues.emplace_back(static_cast<std::int64_t>(index));
            }
            break;
        case ScriptFieldType::Float:
        case ScriptFieldType::Vector2:
        case ScriptFieldType::Color: descriptor.type = EditorPropertyType::Float; break;
        case ScriptFieldType::PrefabRef:
            descriptor.type = EditorPropertyType::AssetGuid;
            descriptor.assetType = "PrefabImporter";
            break;
        case ScriptFieldType::String: descriptor.type = EditorPropertyType::String; break;
    }
    const std::string name = sample.name;
    descriptor.getter = [name, channel](Component& component) -> EditorPropertyValue {
        ScriptFieldRef* field = FindScriptField(component, name);
        if (!field || !field->ptr) return std::string{};
        switch (field->type) {
            case ScriptFieldType::Float: return static_cast<double>(*static_cast<float*>(field->ptr));
            case ScriptFieldType::Int:
            case ScriptFieldType::Enum:
                return static_cast<std::int64_t>(*static_cast<int*>(field->ptr));
            case ScriptFieldType::Bool: return *static_cast<bool*>(field->ptr);
            case ScriptFieldType::String: return *static_cast<std::string*>(field->ptr);
            case ScriptFieldType::Vector2: {
                const auto& value = *static_cast<Vector2*>(field->ptr);
                return static_cast<double>(channel == 0 ? value.x : value.y);
            }
            case ScriptFieldType::Color: {
                const auto& value = *static_cast<Color*>(field->ptr);
                const float values[] = {value.r, value.g, value.b, value.a};
                return static_cast<double>(values[std::max(0, channel)]);
            }
            case ScriptFieldType::ObjectRef:
                return static_cast<std::int64_t>(static_cast<ObjectRef*>(field->ptr)->targetId);
            case ScriptFieldType::PrefabRef:
                return static_cast<PrefabRef*>(field->ptr)->guid;
        }
        return std::string{};
    };
    descriptor.setter = [name, channel](Component& component,
                                         const EditorPropertyValue& value) {
        ScriptFieldRef* field = FindScriptField(component, name);
        if (!field || !field->ptr) return false;
        try {
            switch (field->type) {
                case ScriptFieldType::Float:
                    *static_cast<float*>(field->ptr) = static_cast<float>(std::get<double>(value)); break;
                case ScriptFieldType::Int:
                case ScriptFieldType::Enum:
                    *static_cast<int*>(field->ptr) = static_cast<int>(std::get<std::int64_t>(value)); break;
                case ScriptFieldType::Bool:
                    *static_cast<bool*>(field->ptr) = std::get<bool>(value); break;
                case ScriptFieldType::String:
                    *static_cast<std::string*>(field->ptr) = std::get<std::string>(value); break;
                case ScriptFieldType::Vector2: {
                    auto& item = *static_cast<Vector2*>(field->ptr);
                    (channel == 0 ? item.x : item.y) = static_cast<float>(std::get<double>(value));
                    break;
                }
                case ScriptFieldType::Color: {
                    auto& item = *static_cast<Color*>(field->ptr);
                    float* values[] = {&item.r, &item.g, &item.b, &item.a};
                    *values[std::max(0, channel)] = static_cast<float>(std::get<double>(value));
                    break;
                }
                case ScriptFieldType::ObjectRef:
                    static_cast<ObjectRef*>(field->ptr)->targetId =
                        static_cast<unsigned int>(std::get<std::int64_t>(value)); break;
                case ScriptFieldType::PrefabRef:
                    static_cast<PrefabRef*>(field->ptr)->guid = std::get<std::string>(value); break;
            }
        } catch (const std::bad_variant_access&) {
            return false;
        }
        return true;
    };
    if (descriptor.type == EditorPropertyType::AssetGuid) {
        descriptor.afterChange = [](Component& component) { component.ResolveAssets(); };
    }
    return descriptor;
}

bool IsDynamicSortingLayerDescriptor(const EditorPropertyDescriptor& descriptor) {
    return descriptor.key == "sortingLayer" &&
           descriptor.type == EditorPropertyType::Enum;
}

bool DescriptorShapeMatches(const EditorPropertyDescriptor& descriptor,
                            const EditorPropertyDescriptor& candidate) {
    if (candidate.key != descriptor.key ||
        candidate.type != descriptor.type ||
        candidate.channel != descriptor.channel ||
        candidate.assetType != descriptor.assetType) {
        return false;
    }
    return IsDynamicSortingLayerDescriptor(descriptor) ||
           (candidate.enumLabels == descriptor.enumLabels &&
            candidate.enumValues == descriptor.enumValues);
}

void MergeDynamicSortingLayerOptions(EditorPropertyDescriptor& descriptor,
                                     const EditorPropertyDescriptor& candidate) {
    if (!IsDynamicSortingLayerDescriptor(descriptor)) return;
    const std::size_t count = std::min(candidate.enumLabels.size(),
                                       candidate.enumValues.size());
    for (std::size_t index = 0; index < count; ++index) {
        const EditorPropertyValue& value = candidate.enumValues[index];
        if (std::find(descriptor.enumValues.begin(), descriptor.enumValues.end(), value) !=
            descriptor.enumValues.end()) {
            continue;
        }
        descriptor.enumLabels.push_back(candidate.enumLabels[index]);
        descriptor.enumValues.push_back(value);
    }
}

void IntersectEditorProperties(
    std::vector<EditorPropertyDescriptor>& common,
    const std::vector<EditorPropertyDescriptor>& candidates) {
    for (auto descriptor = common.begin(); descriptor != common.end();) {
        const auto candidate = std::find_if(
            candidates.begin(), candidates.end(),
            [&descriptor](const EditorPropertyDescriptor& item) {
                return DescriptorShapeMatches(*descriptor, item);
            });
        if (candidate == candidates.end()) {
            descriptor = common.erase(descriptor);
            continue;
        }
        MergeDynamicSortingLayerOptions(*descriptor, *candidate);
        ++descriptor;
    }
}

} // namespace

EditorComponentIdentity CaptureEditorComponentIdentity(const Component& component) {
    EditorComponentIdentity identity;
    const GameObject* owner = component.GetGameObject();
    if (!owner) return identity;
    identity.objectId = owner->GetID();
    identity.runtimeTypeId = component.GetRuntimeTypeID();
    identity.instanceId = component.GetInstanceID();
    identity.componentType = component.GetTypeName();
    return identity;
}

std::vector<EditorPropertyDescriptor> DescribeEditorProperties(Component& component) {
    std::vector<EditorPropertyDescriptor> result;

    EditorPropertyDescriptor enabled;
    enabled.key = "enabled";
    enabled.label = "Enabled";
    enabled.type = EditorPropertyType::Bool;
    enabled.getter = [](Component& item) -> EditorPropertyValue { return item.IsEnabled(); };
    enabled.setter = [](Component& item, const EditorPropertyValue& value) {
        if (!std::holds_alternative<bool>(value)) return false;
        item.SetEnabled(std::get<bool>(value));
        return true;
    };
    result.push_back(std::move(enabled));

    if (auto* script = dynamic_cast<Script*>(&component)) {
        for (const auto& field : script->Fields().Fields()) {
            const int channels = field.type == ScriptFieldType::Vector2 ? 2 :
                                 field.type == ScriptFieldType::Color ? 4 : 0;
            if (channels == 0) result.push_back(ScriptDescriptor(field, -1));
            else for (int channel = 0; channel < channels; ++channel)
                result.push_back(ScriptDescriptor(field, channel));
        }
        return result;
    }

    nlohmann::json snapshot;
    component.Serialize(snapshot);
    AppendJsonDescriptors(result, component.GetTypeName(), {}, snapshot);
    if (component.GetTypeName() == "Camera") {
        const bool pixelPerfect = snapshot.value("pixelPerfect", false);
        const char* inactiveProjection = pixelPerfect ? "orthoSize" : "pixelZoom";
        result.erase(std::remove_if(result.begin(), result.end(),
            [inactiveProjection](const EditorPropertyDescriptor& descriptor) {
                return descriptor.key == inactiveProjection;
            }), result.end());
    }
    if (IsWorldRendererComponent(component.GetTypeName()) &&
        snapshot.value("sortMode", std::string{"Fixed"}) != "YAxis") {
        result.erase(std::remove_if(result.begin(), result.end(),
            [](const EditorPropertyDescriptor& descriptor) {
                return descriptor.key == "ySortOffset";
            }), result.end());
    }
    return result;
}

std::vector<EditorPropertyDescriptor> CommonEditorProperties(
    const std::vector<Component*>& components) {
    if (components.empty() || !components.front()) return {};
    std::vector<EditorPropertyDescriptor> common =
        DescribeEditorProperties(*components.front());
    for (std::size_t index = 1; index < components.size(); ++index) {
        if (!components[index]) return {};
        const auto candidates = DescribeEditorProperties(*components[index]);
        IntersectEditorProperties(common, candidates);
    }
    return common;
}

std::vector<EditorPropertyDescriptor> CommonEditorProperties(
    const std::vector<EditorComponentIdentity>& components,
    const EditorComponentResolver& resolve) {
    if (components.empty() || !resolve) return {};
    Component* first = resolve(components.front());
    if (!first) return {};
    std::vector<EditorPropertyDescriptor> common =
        DescribeEditorProperties(*first);
    // Serialize()/Script field discovery are extension points. The first
    // component may have been replaced while its descriptors were produced.
    if (!resolve(components.front())) return {};

    for (std::size_t index = 1; index < components.size(); ++index) {
        Component* component = resolve(components[index]);
        if (!component) return {};
        const auto candidates = DescribeEditorProperties(*component);
        if (!resolve(components[index])) return {};
        IntersectEditorProperties(common, candidates);
    }
    return common;
}

bool EditorPropertyValuesEqual(const EditorPropertyDescriptor& descriptor,
                               const EditorPropertyValue& lhs,
                               const EditorPropertyValue& rhs) {
    if (lhs.index() != rhs.index()) return false;
    if (const auto* left = std::get_if<double>(&lhs)) {
        return std::fabs(*left - std::get<double>(rhs)) <= descriptor.epsilon;
    }
    return lhs == rhs;
}

bool HasMixedEditorPropertyValue(const EditorPropertyDescriptor& descriptor,
                                 const std::vector<Component*>& components) {
    if (components.size() < 2 || !components.front()) return false;
    const EditorPropertyValue first = descriptor.getter(*components.front());
    for (std::size_t index = 1; index < components.size(); ++index) {
        if (!components[index] || !EditorPropertyValuesEqual(
                descriptor, first, descriptor.getter(*components[index]))) return true;
    }
    return false;
}

bool HasMixedEditorPropertyValue(
    const EditorPropertyDescriptor& descriptor,
    const std::vector<EditorComponentIdentity>& components,
    const EditorComponentResolver& resolve) {
    if (components.size() < 2 || !resolve || !descriptor.getter) return false;
    Component* firstComponent = resolve(components.front());
    if (!firstComponent) return true;
    const EditorPropertyValue first = descriptor.getter(*firstComponent);
    if (!resolve(components.front())) return true;
    for (std::size_t index = 1; index < components.size(); ++index) {
        Component* component = resolve(components[index]);
        if (!component) return true;
        const EditorPropertyValue current = descriptor.getter(*component);
        if (!resolve(components[index]) ||
            !EditorPropertyValuesEqual(descriptor, first, current)) {
            return true;
        }
    }
    return false;
}

bool IsEditorPropertyValueValid(const EditorPropertyDescriptor& descriptor,
                                const EditorPropertyValue& value) {
    if (descriptor.type == EditorPropertyType::Enum) {
        return !descriptor.enumValues.empty() &&
               std::find(descriptor.enumValues.begin(), descriptor.enumValues.end(), value) !=
                   descriptor.enumValues.end();
    }
    if (descriptor.type != EditorPropertyType::AssetGuid) return true;
    const auto* guid = std::get_if<std::string>(&value);
    if (!guid) return false;
    if (guid->empty()) return true;
    if (descriptor.assetType.empty()) return false;
    const AssetRecord* record = AssetDatabase::Get().Find(*guid);
    return record && !record->importFailed && record->importer == descriptor.assetType;
}

std::size_t ApplyEditorPropertyValue(const EditorPropertyDescriptor& descriptor,
                                     const std::vector<Component*>& components,
                                     const EditorPropertyValue& value) {
    if (!IsEditorPropertyValueValid(descriptor, value)) return 0;
    std::size_t changed = 0;
    for (Component* component : components) {
        if (!component) continue;
        const EditorPropertyValue before = descriptor.getter(*component);
        if (EditorPropertyValuesEqual(descriptor, before, value)) continue;
        if (descriptor.setter(*component, value)) {
            if (descriptor.afterChange) descriptor.afterChange(*component);
            ++changed;
        }
    }
    return changed;
}

std::size_t ApplyEditorPropertyValue(
    const EditorPropertyDescriptor& descriptor,
    const std::vector<EditorComponentIdentity>& components,
    const EditorComponentResolver& resolve,
    const EditorPropertyValue& value) {
    if (!resolve || !descriptor.getter || !descriptor.setter ||
        !IsEditorPropertyValueValid(descriptor, value)) {
        return 0;
    }

    std::size_t changed = 0;
    for (const EditorComponentIdentity& identity : components) {
        Component* component = resolve(identity);
        if (!component) continue;
        const EditorPropertyValue before = descriptor.getter(*component);

        // A getter may synchronously delete or replace this component (or a
        // later target). Never carry its pointer into the setter.
        component = resolve(identity);
        if (!component || EditorPropertyValuesEqual(descriptor, before, value)) continue;
        if (!descriptor.setter(*component, value)) continue;

        // Deserialize()/SetEnabled() may run user callbacks. Resolve the exact
        // instance again before the optional asset hook and before counting it
        // as a live changed target.
        component = resolve(identity);
        if (!component) continue;
        if (descriptor.afterChange) {
            descriptor.afterChange(*component);
            component = resolve(identity);
            if (!component) continue;
        }
        ++changed;
    }
    return changed;
}

bool ShouldCommitEditorPropertyImmediately(EditorPropertyType type,
                                           bool changed,
                                           bool itemActivated,
                                           bool itemActive,
                                           bool ownsActiveGesture) {
    if (!changed) return false;
    if (type == EditorPropertyType::Bool) return true;
    if (ownsActiveGesture) return false;
    // Asset drops and popup choices can mutate an input while ImGui reports
    // neither activation nor an active item. They have no later deactivation
    // event on which an Inspector transaction could be finalized.
    return !itemActivated && !itemActive;
}

} // namespace molga
