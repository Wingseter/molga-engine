#pragma once

#include "Editor/Commands/EditorCommand.h"
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <nlohmann/json.hpp>

class GameObject;

namespace molga {

class CreatePrefabFromObjectCommand : public ICommand {
public:
    CreatePrefabFromObjectCommand(unsigned int rootId, const std::filesystem::path& relativePrefabPath);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Prefab from Object"; }
private:
    unsigned int rootId_;
    std::filesystem::path relativePrefabPath_;
    std::string guid_;
    nlohmann::json savedSceneStateBefore_;
};

class InstantiatePrefabCommand : public ICommand {
public:
    InstantiatePrefabCommand(const std::string& prefabGuid);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Instantiate Prefab"; }
private:
    std::string guid_;
    nlohmann::json savedSceneStateBefore_;
    unsigned int createdRootId_ = 0;
};

class ApplyPrefabCommand : public ICommand {
public:
    ApplyPrefabCommand(unsigned int rootId);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Apply Prefab"; }
private:
    unsigned int rootId_;
    nlohmann::json oldPrefabJson_;
    nlohmann::json oldModifications_;
    std::string guid_;
    std::filesystem::path path_;
    nlohmann::json savedSceneStateBefore_;
};

class RevertPrefabCommand : public ICommand {
public:
    RevertPrefabCommand(unsigned int rootId);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Revert Prefab"; }
private:
    unsigned int rootId_;
    nlohmann::json savedSceneStateBefore_;
};

class UnpackPrefabCommand : public ICommand {
public:
    UnpackPrefabCommand(unsigned int rootId);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Unpack Prefab"; }
private:
    unsigned int rootId_;
    std::string guid_;
    nlohmann::json modifications_;
    std::unordered_map<unsigned int, unsigned int> idRemap_;
};

} // namespace molga
