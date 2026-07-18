#pragma once

#include "Common/Types.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

class Component;

namespace molga {

enum class EditorPropertyType {
    Bool,
    Integer,
    Float,
    String,
    Enum,
    AssetGuid,
};

using EditorPropertyValue =
    std::variant<bool, std::int64_t, double, std::string>;

struct EditorPropertyDescriptor {
    std::string key;
    std::string label;
    std::string group;
    EditorPropertyType type = EditorPropertyType::Float;
    int channel = -1; // -1 scalar, otherwise vector/color axis/channel
    float epsilon = 1.0e-5f;
    std::vector<std::string> enumLabels;
    std::vector<EditorPropertyValue> enumValues;
    std::string assetType;

    std::function<EditorPropertyValue(Component&)> getter;
    std::function<bool(Component&, const EditorPropertyValue&)> setter;
    std::function<void(Component&)> afterChange;
};

struct EditorComponentIdentity {
    unsigned int objectId = 0;
    std::size_t runtimeTypeId = 0;
    std::uint64_t instanceId = 0;
    std::string componentType;

    bool IsValid() const { return objectId != 0 && instanceId != 0; }
};

using EditorComponentResolver =
    std::function<Component*(const EditorComponentIdentity&)>;

EditorComponentIdentity CaptureEditorComponentIdentity(const Component& component);

// Builds the single descriptor hierarchy used by both single and multi
// Inspector paths. Component snapshots provide built-in fields; registered
// Script fields retain their declared types and UI hints.
std::vector<EditorPropertyDescriptor> DescribeEditorProperties(Component& component);

// Intersection by stable descriptor key/type/channel, preserving the first
// component's authored order.
std::vector<EditorPropertyDescriptor> CommonEditorProperties(
    const std::vector<Component*>& components);
std::vector<EditorPropertyDescriptor> CommonEditorProperties(
    const std::vector<EditorComponentIdentity>& components,
    const EditorComponentResolver& resolve);

bool EditorPropertyValuesEqual(const EditorPropertyDescriptor& descriptor,
                               const EditorPropertyValue& lhs,
                               const EditorPropertyValue& rhs);

bool HasMixedEditorPropertyValue(const EditorPropertyDescriptor& descriptor,
                                 const std::vector<Component*>& components);
bool HasMixedEditorPropertyValue(
    const EditorPropertyDescriptor& descriptor,
    const std::vector<EditorComponentIdentity>& components,
    const EditorComponentResolver& resolve);

// Empty asset references are valid. Non-empty references must resolve to a
// healthy record owned by the descriptor's explicitly declared importer.
bool IsEditorPropertyValueValid(const EditorPropertyDescriptor& descriptor,
                                const EditorPropertyValue& value);

// Applies one scalar/axis/channel absolute value while preserving every other
// field. Returns the number of live components changed.
std::size_t ApplyEditorPropertyValue(const EditorPropertyDescriptor& descriptor,
                                     const std::vector<Component*>& components,
                                     const EditorPropertyValue& value);
std::size_t ApplyEditorPropertyValue(
    const EditorPropertyDescriptor& descriptor,
    const std::vector<EditorComponentIdentity>& components,
    const EditorComponentResolver& resolve,
    const EditorPropertyValue& value);

// Drag/drop and popup selection may change a value without activating the
// underlying ImGui input. Such changes must become an immediate transaction
// unless a live gesture already owns the row.
bool ShouldCommitEditorPropertyImmediately(EditorPropertyType type,
                                           bool changed,
                                           bool itemActivated,
                                           bool itemActive,
                                           bool ownsActiveGesture);

} // namespace molga
