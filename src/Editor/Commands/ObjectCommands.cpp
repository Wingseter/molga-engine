#include "Editor/Commands/ObjectCommands.h"
#include "Editor/Editor.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Components/UIButton.h"
#include "Core/SceneSerializer.h"
#include <algorithm>

namespace molga {

static unsigned int ParentIdOf(GameObject* o) {
    GameObject* p = o ? o->GetParent() : nullptr;
    return p ? p->GetID() : 0u;
}

// ── CreateObjectCommand ───────────────────────────────────────────────────────
CreateObjectCommand::CreateObjectCommand(std::string name) : name_(std::move(name)) {}

void CreateObjectCommand::Execute() {
    if (!object_) {
        object_ = std::make_shared<GameObject>(name_);
        object_->AddComponent<Transform>();
        id_ = object_->GetID();
    }
    Editor::Get().AddExistingObject(object_);
    Editor::Get().SetSelectedObject(object_.get());
}

void CreateObjectCommand::Undo() {
    if (Editor::Get().GetSelectedObject() == object_.get()) {
        Editor::Get().SetSelectedObject(nullptr);
    }
    Editor::Get().RemoveObjectsByIds({ id_ });
}

// ── DeleteObjectCommand ───────────────────────────────────────────────────────
DeleteObjectCommand::DeleteObjectCommand(GameObject* root)
    : rootId_(root ? root->GetID() : 0u) {}

void DeleteObjectCommand::Execute() {
    saved_.clear();
    GameObject* root = Editor::Get().FindObjectById(rootId_);
    if (!root) return;

    std::vector<GameObject*> subtree;
    root->CollectSubtree(subtree);  // 부모 먼저

    std::vector<unsigned int> ids;
    for (GameObject* o : subtree) {
        ids.push_back(o->GetID());
        saved_.push_back({ Editor::Get().ShareObjectById(o->GetID()), ParentIdOf(o) });
    }

    // 선택이 지워질 대상이면 해제
    if (Editor::Get().GetSelectedObject() &&
        std::find(ids.begin(), ids.end(), Editor::Get().GetSelectedObject()->GetID()) != ids.end()) {
        Editor::Get().SetSelectedObject(nullptr);
    }
    Editor::Get().RemoveObjectsByIds(ids);
}

void DeleteObjectCommand::Undo() {
    // 부모 먼저 순서이므로 그대로 다시 추가하면 부모가 자식보다 먼저 들어간다.
    for (auto& s : saved_) {
        if (s.obj) Editor::Get().AddExistingObject(s.obj);
    }
    // 부모 링크 복원
    for (auto& s : saved_) {
        if (!s.obj) continue;
        if (s.parentId != 0) {
            if (GameObject* p = Editor::Get().FindObjectById(s.parentId)) {
                s.obj->SetParent(p);
            }
        }
    }
    saved_.clear();
}

// ── RenameObjectCommand ───────────────────────────────────────────────────────
RenameObjectCommand::RenameObjectCommand(unsigned int id, std::string newName)
    : id_(id), newName_(std::move(newName)) {}

void RenameObjectCommand::Execute() {
    if (GameObject* o = Editor::Get().FindObjectById(id_)) {
        oldName_ = o->GetName();
        o->SetName(newName_);
        Editor::Get().MarkSceneModified();
    }
}

void RenameObjectCommand::Undo() {
    if (GameObject* o = Editor::Get().FindObjectById(id_)) {
        o->SetName(oldName_);
        Editor::Get().MarkSceneModified();
    }
}

// ── ReparentObjectCommand ─────────────────────────────────────────────────────
ReparentObjectCommand::ReparentObjectCommand(unsigned int childId, unsigned int newParentId)
    : childId_(childId), newParentId_(newParentId) {}

void ReparentObjectCommand::Execute() {
    GameObject* child = Editor::Get().FindObjectById(childId_);
    if (!child) return;
    oldParentId_ = ParentIdOf(child);
    GameObject* np = newParentId_ ? Editor::Get().FindObjectById(newParentId_) : nullptr;
    child->SetParent(np);
    Editor::Get().MarkSceneModified();
}

void ReparentObjectCommand::Undo() {
    GameObject* child = Editor::Get().FindObjectById(childId_);
    if (!child) return;
    GameObject* op = oldParentId_ ? Editor::Get().FindObjectById(oldParentId_) : nullptr;
    child->SetParent(op);
    Editor::Get().MarkSceneModified();
}

// ── DuplicateObjectCommand ─────────────────────────────────────────────
DuplicateObjectCommand::DuplicateObjectCommand(GameObject* src)
    : srcId_(src ? src->GetID() : 0u) {}

void DuplicateObjectCommand::Execute() {
    if (!copy_) {
        GameObject* src = Editor::Get().FindObjectById(srcId_);
        if (!src) return;

        nlohmann::json subtreeJson = SceneSerializer::SerializeSubtree(src);

        std::vector<std::shared_ptr<GameObject>> instantiatedObjects;
        std::unordered_map<unsigned int, unsigned int> idRemap;
        
        GameObject* rootCopy = SceneSerializer::DeserializeSubtreeRemapped(subtreeJson, instantiatedObjects, idRemap);
        if (rootCopy) {
            rootCopy->SetName(src->GetName() + " (Copy)");
            if (src->GetParent()) {
                rootCopy->SetParent(src->GetParent());
            }

            duplicatedObjects_ = instantiatedObjects;
            for (auto& shObj : instantiatedObjects) {
                if (shObj.get() == rootCopy) {
                    copy_ = shObj;
                    break;
                }
            }
            copyId_ = rootCopy->GetID();
        }
    }

    if (copy_) {
        for (auto& obj : duplicatedObjects_) {
            Editor::Get().AddExistingObject(obj);
        }
        Editor::Get().SetSelectedObject(copy_.get());
        Editor::Get().MarkSceneModified();
    }
}

void DuplicateObjectCommand::Undo() {
    if (copy_) {
        if (Editor::Get().GetSelectedObject() == copy_.get()) {
            Editor::Get().SetSelectedObject(nullptr);
        }
        std::vector<unsigned int> idsToRemove;
        for (const auto& obj : duplicatedObjects_) {
            if (obj) idsToRemove.push_back(obj->GetID());
        }
        Editor::Get().RemoveObjectsByIds(idsToRemove);
        Editor::Get().MarkSceneModified();
    }
}

// ── CreateUIPresetCommand ───────────────────────────────────────────────────
CreateUIPresetCommand::CreateUIPresetCommand(UIPresetType type) : type_(type) {}

void CreateUIPresetCommand::Build() {
    if (built_) return;
    built_ = true;

    std::shared_ptr<GameObject> canvas;
    if (auto* active = Editor::Get().GetGameObjects()) {
        for (const auto& object : *active) {
            if (object && object->GetComponent<UICanvas>()) {
                canvas = object;
                break;
            }
        }
    }

    const bool needsNewCanvas = !canvas || type_ == UIPresetType::Canvas;
    if (needsNewCanvas) {
        canvas = std::make_shared<GameObject>("Canvas");
        canvas->AddComponent<UICanvas>();
        auto* rect = canvas->AddComponent<RectTransform>();
        rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        rect->SetPivot({0.5f, 0.5f});
        rect->SetSizeDelta({0.0f, 0.0f});
        objects_.push_back(canvas);
        parentIds_.push_back(0u);
    }

    if (type_ == UIPresetType::Canvas) {
        primary_ = canvas;
        return;
    }

    auto element = std::make_shared<GameObject>(
        type_ == UIPresetType::Image ? "Image" :
        type_ == UIPresetType::Label ? "Label" : "Button");
    auto* rect = element->AddComponent<RectTransform>();
    rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    rect->SetPivot({0.5f, 0.5f});
    if (type_ == UIPresetType::Image) {
        rect->SetSizeDelta({160.0f, 160.0f});
        element->AddComponent<UIImage>();
    } else if (type_ == UIPresetType::Label) {
        rect->SetSizeDelta({300.0f, 60.0f});
        element->AddComponent<UILabel>();
    } else {
        rect->SetSizeDelta({220.0f, 64.0f});
        element->AddComponent<UIButton>();
    }
    element->SetParent(canvas.get());
    objects_.push_back(element);
    parentIds_.push_back(canvas->GetID());
    primary_ = element;

    if (type_ == UIPresetType::Button) {
        auto label = std::make_shared<GameObject>("Label");
        auto* labelRect = label->AddComponent<RectTransform>();
        labelRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        labelRect->SetPivot({0.5f, 0.5f});
        labelRect->SetSizeDelta({-16.0f, -8.0f});
        label->AddComponent<UILabel>()->SetText("Button");
        label->SetParent(element.get());
        objects_.push_back(label);
        parentIds_.push_back(element->GetID());
    }
}

void CreateUIPresetCommand::Execute() {
    Build();
    for (const auto& object : objects_) Editor::Get().AddExistingObject(object);
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        if (!objects_[i] || parentIds_[i] == 0u) continue;
        if (GameObject* parent = Editor::Get().FindObjectById(parentIds_[i])) {
            objects_[i]->SetParent(parent);
        }
    }
    Editor::Get().SetSelectedObject(primary_.get());
    Editor::Get().MarkSceneModified();
}

void CreateUIPresetCommand::Undo() {
    std::vector<unsigned int> ids;
    ids.reserve(objects_.size());
    for (auto it = objects_.rbegin(); it != objects_.rend(); ++it) {
        if (*it) {
            ids.push_back((*it)->GetID());
            (*it)->SetParent(nullptr);
        }
    }
    if (Editor::Get().GetSelectedObject() == primary_.get()) {
        Editor::Get().SetSelectedObject(nullptr);
    }
    Editor::Get().RemoveObjectsByIds(ids);
}

} // namespace molga
