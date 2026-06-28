#pragma once

#include "Editor/Commands/EditorCommand.h"
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
