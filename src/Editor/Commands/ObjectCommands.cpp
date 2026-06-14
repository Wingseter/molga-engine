#include "Editor/Commands/ObjectCommands.h"
#include "Editor/Editor.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
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
        // 최초 실행: src를 찾아 복제돈 생성
        GameObject* src = Editor::Get().FindObjectById(srcId_);
        if (!src) return;

        copy_ = std::make_shared<GameObject>(src->GetName() + " (Copy)");

        // Transform 복사
        copy_->AddComponent<Transform>();
        if (auto* st = src->GetComponent<Transform>()) {
            if (auto* dt = copy_->GetComponent<Transform>()) {
                dt->SetPosition(st->GetPosition());
                dt->SetRotation(st->GetRotation());
                dt->SetScale(st->GetScale());
            }
        }

        // SpriteRenderer 복사
        if (auto* sr = src->GetComponent<SpriteRenderer>()) {
            copy_->AddComponent<SpriteRenderer>();
            if (auto* dr = copy_->GetComponent<SpriteRenderer>()) {
                dr->SetTexturePath(sr->GetTexturePath());
                dr->SetColor(sr->GetColor());
                dr->SetSortingOrder(sr->GetSortingOrder());
                dr->SetFlipX(sr->GetFlipX());
                dr->SetFlipY(sr->GetFlipY());
            }
        }

        copyId_ = copy_->GetID();
    }
    Editor::Get().AddExistingObject(copy_);
    Editor::Get().SetSelectedObject(copy_.get());
    Editor::Get().MarkSceneModified();
}

void DuplicateObjectCommand::Undo() {
    if (Editor::Get().GetSelectedObject() == copy_.get()) {
        Editor::Get().SetSelectedObject(nullptr);
    }
    Editor::Get().RemoveObjectsByIds({ copyId_ });
    Editor::Get().MarkSceneModified();
}

} // namespace molga
