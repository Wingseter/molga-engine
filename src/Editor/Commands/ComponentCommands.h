#pragma once

#include "Editor/Commands/EditorCommand.h"
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>

class GameObject;

namespace molga {

// Snapshot-based command for a component (Transform, SpriteRenderer, custom scripts, etc.)
class ComponentSnapshotCommand : public ICommand {
public:
    ComponentSnapshotCommand(unsigned int targetId,
                             const std::string& componentType,
                             const nlohmann::json& beforeSnap,
                             const nlohmann::json& afterSnap);

    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Modify Component (" + componentType_ + ")"; }

private:
    unsigned int targetId_;
    std::string componentType_;
    nlohmann::json beforeSnap_;
    nlohmann::json afterSnap_;
};

struct ComponentSnapshotChange {
    unsigned int targetId = 0;
    std::string componentType;
    nlohmann::json before;
    nlohmann::json after;
};

// Stable identity plus the snapshot captured at the start of a live Inspector
// gesture. The matching component is re-resolved before taking the final
// snapshot, so a same-type replacement is never mistaken for the edited
// instance.
struct ComponentSnapshotBaseline {
    unsigned int targetId = 0;
    std::size_t runtimeTypeId = 0;
    std::uint64_t instanceId = 0;
    std::string componentType;
    nlohmann::json before;
};

std::vector<ComponentSnapshotChange> CaptureAppliedComponentChanges(
    const std::vector<ComponentSnapshotBaseline>& baselines);

// A multi-Inspector gesture is represented by exactly one command. Each target
// is resolved by ID on every Execute/Undo; deleted targets are safe no-ops.
class BatchComponentSnapshotCommand : public ICommand {
public:
    explicit BatchComponentSnapshotCommand(
        std::vector<ComponentSnapshotChange> changes,
        bool valuesAlreadyApplied = false);

    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Modify Components"; }

    const std::vector<ComponentSnapshotChange>& Changes() const { return changes_; }

private:
    void Apply(bool after);
    void FinalizeAppliedTargets();
    std::vector<ComponentSnapshotChange> changes_;
    bool valuesAlreadyApplied_ = false;
};

// Component add command
class ComponentAddCommand : public ICommand {
public:
    ComponentAddCommand(unsigned int targetId, const std::string& componentType);

    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Add Component (" + componentType_ + ")"; }

private:
    unsigned int targetId_;
    std::string componentType_;
    nlohmann::json addedSnap_; // Cache serialized state after creation
};

// Component remove command
class ComponentRemoveCommand : public ICommand {
public:
    ComponentRemoveCommand(unsigned int targetId, const std::string& componentType);

    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Remove Component (" + componentType_ + ")"; }

private:
    unsigned int targetId_;
    std::string componentType_;
    nlohmann::json removedSnap_; // Cache serialized state of removed component
};

// Create GameObject with predefined components (e.g. Sprite, Tilemap)
class CreateObjectWithComponentsCommand : public ICommand {
public:
    CreateObjectWithComponentsCommand(const std::string& name, const std::vector<std::string>& componentTypes);

    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Object with Components"; }

    GameObject* created() const { return object_.get(); }

private:
    std::string name_;
    std::vector<std::string> componentTypes_;
    std::shared_ptr<GameObject> object_;
    unsigned int id_ = 0;
};

} // namespace molga
