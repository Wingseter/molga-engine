#include "Editor/Commands/CreateSpriteFromAssetCommand.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include <algorithm>

namespace molga {

CreateSpriteFromAssetCommand::CreateSpriteFromAssetCommand(
    std::string textureGuid, std::string name, Vector2 worldPos,
    std::vector<std::shared_ptr<GameObject>>* objects)
    : textureGuid_(std::move(textureGuid)), name_(std::move(name)),
      worldPos_(worldPos), objects_(objects) {}

void CreateSpriteFromAssetCommand::Execute() {
    if (!objects_) return;
    if (!object_) {
        object_ = std::make_shared<GameObject>(name_);
        auto* tr = object_->AddComponent<Transform>();
        tr->SetPosition(worldPos_);
        auto* sr = object_->AddComponent<SpriteRenderer>();
        sr->SetTextureGuid(textureGuid_);
        sr->ResolveAssets();   // 즉시 텍스처 로드(누락이면 placeholder)
    }
    objects_->push_back(object_);
    created_ = object_.get();
}

void CreateSpriteFromAssetCommand::Undo() {
    if (!objects_ || !object_) return;
    objects_->erase(
        std::remove(objects_->begin(), objects_->end(), object_), objects_->end());
    created_ = nullptr;
}

} // namespace molga
