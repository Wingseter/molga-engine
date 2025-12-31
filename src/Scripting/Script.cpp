#include "Script.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"

Transform* Script::GetTransform() {
    if (gameObject) {
        return gameObject->GetComponent<Transform>();
    }
    return nullptr;
}
