#include "Editor/Commands/PrefabCommands.h"
#include "Editor/Editor.h"
#include "ECS/GameObject.h"
#include "Core/SceneSerializer.h"
#include "Core/PrefabRegistry.h"
#include "Core/PrefabUtil.h"
#include "ECS/Components/PrefabInstance.h"
#include "Common/Log.h"
#include <algorithm>

namespace molga {

// ── CreatePrefabFromObjectCommand ─────────────────────────────────────────────
CreatePrefabFromObjectCommand::CreatePrefabFromObjectCommand(
    unsigned int rootId, const std::filesystem::path& relativePrefabPath)
    : rootId_(rootId), relativePrefabPath_(relativePrefabPath) {
    guid_ = PrefabRegistry::GenerateGUID();
}

void CreatePrefabFromObjectCommand::Execute() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    // Save scene state for Undo
    savedSceneStateBefore_ = SceneSerializer::SerializeScene(*gameObjectsPtr, "SceneBeforeCreatePrefab");

    GameObject* obj = Editor::Get().FindObjectById(rootId_);
    if (!obj) return;

    // 1. Serialize subtree to JSON
    nlohmann::json subtreeJson = SceneSerializer::SerializeSubtree(obj);

    // 2. Save prefab file via registry
    bool saved = PrefabRegistry::Get().SavePrefab(guid_, relativePrefabPath_, subtreeJson);
    if (!saved) {
        Log::Error("CreatePrefabCommand", "Failed to save prefab: " + relativePrefabPath_.string());
        return;
    }

    // 3. Convert scene object to prefab instance
    std::vector<GameObject*> subtree;
    obj->CollectSubtree(subtree);
    std::unordered_map<unsigned int, unsigned int> idRemap;
    for (auto* o : subtree) {
        if (o) {
            idRemap[o->GetID()] = o->GetID(); // Since it has the same ID right now
        }
    }

    auto* pi = obj->AddComponent<PrefabInstance>();
    pi->SetPrefabGuid(guid_);
    pi->SetIdRemap(idRemap);
    pi->SetModifications(nlohmann::json::array());

    Editor::Get().MarkSceneModified();
}

void CreatePrefabFromObjectCommand::Undo() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    // Restore scene state
    SceneSerializer::DeserializeScene(savedSceneStateBefore_, *gameObjectsPtr);
    Editor::Get().MarkSceneModified();
}

// ── InstantiatePrefabCommand ──────────────────────────────────────────────────
InstantiatePrefabCommand::InstantiatePrefabCommand(const std::string& prefabGuid)
    : guid_(prefabGuid) {}

void InstantiatePrefabCommand::Execute() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    // Save scene state
    savedSceneStateBefore_ = SceneSerializer::SerializeScene(*gameObjectsPtr, "SceneBeforeInstantiatePrefab");

    std::unordered_map<unsigned int, unsigned int> idRemap;
    std::vector<std::shared_ptr<GameObject>> instantiatedObjects;
    GameObject* root = PrefabRegistry::Get().Instantiate(guid_, instantiatedObjects, idRemap);

    if (root) {
        auto* pi = root->AddComponent<PrefabInstance>();
        pi->SetPrefabGuid(guid_);
        pi->SetIdRemap(idRemap);
        pi->SetModifications(nlohmann::json::array());

        createdRootId_ = root->GetID();

        // Add instantiated objects to scene
        for (auto& io : instantiatedObjects) {
            if (io) {
                Editor::Get().AddExistingObject(io);
            }
        }

        Editor::Get().SetSelectedObject(root);
        Editor::Get().MarkSceneModified();
    }
}

void InstantiatePrefabCommand::Undo() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    if (Editor::Get().GetSelectedObject() && Editor::Get().GetSelectedObject()->GetID() == createdRootId_) {
        Editor::Get().SetSelectedObject(nullptr);
    }

    // Restore scene state
    SceneSerializer::DeserializeScene(savedSceneStateBefore_, *gameObjectsPtr);
    Editor::Get().MarkSceneModified();
}

// ── ApplyPrefabCommand ────────────────────────────────────────────────────────
ApplyPrefabCommand::ApplyPrefabCommand(unsigned int rootId) : rootId_(rootId) {}

void ApplyPrefabCommand::Execute() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    GameObject* obj = Editor::Get().FindObjectById(rootId_);
    if (!obj) return;

    auto* pi = obj->GetComponent<PrefabInstance>();
    if (!pi) return;

    guid_ = pi->GetPrefabGuid();
    path_ = PrefabRegistry::Get().GetPrefabPath(guid_);
    oldPrefabJson_ = PrefabRegistry::Get().GetPrefabJson(guid_);
    oldModifications_ = pi->GetModifications();

    // Save scene state in case we want to undo the propagation
    savedSceneStateBefore_ = SceneSerializer::SerializeScene(*gameObjectsPtr, "SceneBeforeApplyPrefab");

    // Apply modifications to the prefab template
    bool applied = PrefabUtil::ApplyPrefab(obj);
    if (applied) {
        // Refresh other instances of the same prefab in the scene
        for (auto& io : *gameObjectsPtr) {
            if (io && io->GetID() != rootId_) {
                auto* otherPi = io->GetComponent<PrefabInstance>();
                if (otherPi && otherPi->GetPrefabGuid() == guid_) {
                    PrefabUtil::RevertPrefab(io.get(), *gameObjectsPtr);
                }
            }
        }
        Editor::Get().MarkSceneModified();
    }
}

void ApplyPrefabCommand::Undo() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    // 1. Restore the prefab file template in the registry
    PrefabRegistry::Get().SavePrefab(guid_, path_, oldPrefabJson_);

    // 2. Restore scene state (which restores modifications on this instance and all other instances)
    SceneSerializer::DeserializeScene(savedSceneStateBefore_, *gameObjectsPtr);
    Editor::Get().MarkSceneModified();
}

// ── RevertPrefabCommand ───────────────────────────────────────────────────────
RevertPrefabCommand::RevertPrefabCommand(unsigned int rootId) : rootId_(rootId) {}

void RevertPrefabCommand::Execute() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    // Save scene state
    savedSceneStateBefore_ = SceneSerializer::SerializeScene(*gameObjectsPtr, "SceneBeforeRevertPrefab");

    GameObject* obj = Editor::Get().FindObjectById(rootId_);
    if (!obj) return;

    bool reverted = PrefabUtil::RevertPrefab(obj, *gameObjectsPtr);
    if (reverted) {
        Editor::Get().MarkSceneModified();
    }
}

void RevertPrefabCommand::Undo() {
    auto* gameObjectsPtr = Editor::Get().GetGameObjects();
    if (!gameObjectsPtr) return;

    // Restore scene state
    SceneSerializer::DeserializeScene(savedSceneStateBefore_, *gameObjectsPtr);
    Editor::Get().MarkSceneModified();
}

// ── UnpackPrefabCommand ───────────────────────────────────────────────────────
UnpackPrefabCommand::UnpackPrefabCommand(unsigned int rootId) : rootId_(rootId) {}

void UnpackPrefabCommand::Execute() {
    GameObject* obj = Editor::Get().FindObjectById(rootId_);
    if (!obj) return;

    auto* pi = obj->GetComponent<PrefabInstance>();
    if (!pi) return;

    guid_ = pi->GetPrefabGuid();
    modifications_ = pi->GetModifications();
    idRemap_ = pi->GetIdRemap();

    obj->RemoveComponent<PrefabInstance>();
    Editor::Get().MarkSceneModified();
}

void UnpackPrefabCommand::Undo() {
    GameObject* obj = Editor::Get().FindObjectById(rootId_);
    if (!obj) return;

    auto* pi = obj->AddComponent<PrefabInstance>();
    pi->SetPrefabGuid(guid_);
    pi->SetModifications(modifications_);
    pi->SetIdRemap(idRemap_);
    Editor::Get().MarkSceneModified();
}

} // namespace molga
