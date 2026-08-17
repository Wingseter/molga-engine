#include "PropertyCommands.h"
#include "SceneSnapshots.h"
#include "ECS/GameObject.h"
#include "Editor/Editor.h"

namespace molga {

GameObjectPropertyCommand::GameObjectPropertyCommand(unsigned int targetId,
                                                     const nlohmann::json& beforeProp,
                                                     const nlohmann::json& afterProp)
    : targetId_(targetId), beforeProp_(beforeProp), afterProp_(afterProp) {}

void GameObjectPropertyCommand::Execute() {
    GameObject* obj = FindGameObjectById(targetId_);
    if (obj) {
        RestoreGameObjectProperties(obj, afterProp_);
        Editor::Get().MarkSceneModified();
    }
}

void GameObjectPropertyCommand::Undo() {
    GameObject* obj = FindGameObjectById(targetId_);
    if (obj) {
        RestoreGameObjectProperties(obj, beforeProp_);
        Editor::Get().MarkSceneModified();
    }
}

} // namespace molga
