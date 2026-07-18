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
#include "Editor/Selection/SelectionUtils.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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

// ── Multi-object delete/duplicate ───────────────────────────────────────────
namespace {

std::vector<HierarchyObjectPlacement> CapturePlacements(
    const std::vector<GameObject*>& objects) {
    std::vector<HierarchyObjectPlacement> placements;
    std::unordered_set<unsigned int> captured;
    placements.reserve(objects.size());
    for (GameObject* object : objects) {
        if (!object || !captured.insert(object->GetID()).second) continue;
        std::size_t worldIndex = 0;
        if (!Editor::Get().TryGetObjectIndex(object->GetID(), worldIndex)) continue;
        GameObject* parent = object->GetParent();
        placements.push_back({Editor::Get().ShareObjectById(object->GetID()),
                              worldIndex,
                              parent ? parent->GetID() : 0u,
                              object->GetSiblingIndex()});
    }
    std::stable_sort(placements.begin(), placements.end(),
                     [](const auto& left, const auto& right) {
                         return left.worldIndex < right.worldIndex;
                     });
    return placements;
}

void RemovePlacements(const std::vector<HierarchyObjectPlacement>& placements) {
    std::vector<unsigned int> ids;
    ids.reserve(placements.size());
    for (const auto& placement : placements) {
        if (!placement.object ||
            !Editor::Get().FindObjectById(placement.object->GetID())) {
            continue;
        }
        placement.object->SetParent(nullptr);
        ids.push_back(placement.object->GetID());
    }
    if (!ids.empty()) Editor::Get().RemoveObjectsByIds(ids);
}

void RestorePlacements(const std::vector<HierarchyObjectPlacement>& placements) {
    for (const auto& placement : placements) {
        if (!placement.object ||
            Editor::Get().FindObjectById(placement.object->GetID())) {
            continue;
        }
        Editor::Get().InsertExistingObjectAt(placement.object,
                                             placement.worldIndex);
    }

    // Resolve parents only after every saved object is visible in the world.
    for (const auto& placement : placements) {
        if (!placement.object) continue;
        GameObject* parent = placement.parentId == 0
            ? nullptr
            : Editor::Get().FindObjectById(placement.parentId);
        placement.object->SetParent(parent);
    }

    // SetParent appends, so replay sibling slots from low to high per parent.
    std::vector<const HierarchyObjectPlacement*> children;
    children.reserve(placements.size());
    for (const auto& placement : placements) {
        if (placement.object && placement.parentId != 0) {
            children.push_back(&placement);
        }
    }
    std::stable_sort(children.begin(), children.end(),
                     [](const auto* left, const auto* right) {
                         if (left->parentId != right->parentId) {
                             return left->parentId < right->parentId;
                         }
                         return left->siblingIndex < right->siblingIndex;
                     });
    for (const auto* placement : children) {
        placement->object->SetSiblingIndex(placement->siblingIndex);
    }
}

} // namespace

DeleteObjectsCommand::DeleteObjectsCommand(std::vector<unsigned int> selectedIds)
    : requestedIds_(std::move(selectedIds)) {}

void DeleteObjectsCommand::Execute() {
    auto& selection = Editor::Get().GetSelection();
    if (!built_) {
        built_ = true;
        previousSelection_ = selection.State();
        const auto roots = RootMostSelection(requestedIds_, [](unsigned int id) {
            return Editor::Get().FindObjectById(id);
        });
        std::vector<GameObject*> removed;
        for (unsigned int id : roots) {
            if (GameObject* object = Editor::Get().FindObjectById(id)) {
                object->CollectSubtree(removed);
            }
        }
        removedObjects_ = CapturePlacements(removed);
    }
    if (removedObjects_.empty()) return;
    RemovePlacements(removedObjects_);
    selection.Clear(SelectionSource::Hierarchy);
    selection.Rebind([](unsigned int id) {
        return Editor::Get().FindObjectById(id) != nullptr;
    });
    if (!resultCaptured_) {
        resultSelection_ = selection.State();
        resultCaptured_ = true;
    } else {
        selection.RestoreState(resultSelection_, SelectionSource::Hierarchy);
    }
    Editor::Get().MarkSceneModified();
}

void DeleteObjectsCommand::Undo() {
    RestorePlacements(removedObjects_);
    Editor::Get().GetSelection().RestoreState(
        previousSelection_, SelectionSource::Hierarchy);
    if (!removedObjects_.empty()) Editor::Get().MarkSceneModified();
}

DuplicateObjectsCommand::DuplicateObjectsCommand(
    std::vector<unsigned int> selectedIds)
    : requestedIds_(std::move(selectedIds)) {}

void DuplicateObjectsCommand::Execute() {
    auto& selection = Editor::Get().GetSelection();
    if (!built_) {
        built_ = true;
        previousSelection_ = selection.State();
        const auto roots = RootMostSelection(requestedIds_, [](unsigned int id) {
            return Editor::Get().FindObjectById(id);
        });

        struct DuplicateGroup {
            unsigned int sourceId = 0;
            unsigned int sourceParentId = 0;
            std::size_t sourceSiblingIndex = 0;
            std::size_t insertAfter = 0;
            std::shared_ptr<GameObject> root;
            std::vector<std::shared_ptr<GameObject>> objects;
        };
        std::vector<DuplicateGroup> groups;

        for (unsigned int id : roots) {
            GameObject* source = Editor::Get().FindObjectById(id);
            if (!source) continue;

            std::vector<GameObject*> sourceSubtree;
            source->CollectSubtree(sourceSubtree);
            std::size_t insertAfter = 0;
            bool foundInWorld = false;
            for (GameObject* object : sourceSubtree) {
                std::size_t index = 0;
                if (object && Editor::Get().TryGetObjectIndex(object->GetID(), index)) {
                    insertAfter = foundInWorld ? std::max(insertAfter, index) : index;
                    foundInWorld = true;
                }
            }
            if (!foundInWorld) continue;

            DuplicateGroup group;
            group.sourceId = id;
            group.sourceParentId = source->GetParent()
                ? source->GetParent()->GetID() : 0u;
            group.sourceSiblingIndex = source->GetSiblingIndex();
            group.insertAfter = insertAfter;

            std::unordered_map<unsigned int, unsigned int> idRemap;
            GameObject* rootCopy = SceneSerializer::DeserializeSubtreeRemapped(
                SceneSerializer::SerializeSubtree(source), group.objects, idRemap);
            if (!rootCopy) continue;
            rootCopy->SetName(source->GetName() + " (Copy)");
            for (const auto& object : group.objects) {
                if (object && object.get() == rootCopy) {
                    group.root = object;
                    break;
                }
            }
            if (!group.root) continue;
            duplicateRootIds_.push_back(group.root->GetID());
            groups.push_back(std::move(group));
        }

        // Insert each cloned DFS block directly after its source subtree. Work
        // from the back so earlier insertion points stay stable.
        std::vector<DuplicateGroup*> groupOrder;
        groupOrder.reserve(groups.size());
        for (auto& group : groups) groupOrder.push_back(&group);
        std::stable_sort(groupOrder.begin(), groupOrder.end(),
                         [](const auto* left, const auto* right) {
                             return left->insertAfter > right->insertAfter;
                         });
        for (DuplicateGroup* group : groupOrder) {
            std::size_t index = group->insertAfter + 1;
            for (const auto& object : group->objects) {
                Editor::Get().InsertExistingObjectAt(object, index++);
            }
        }

        // Descending source sibling order makes "copy immediately after source"
        // stable when several selected roots share one parent.
        std::stable_sort(groupOrder.begin(), groupOrder.end(),
                         [](const auto* left, const auto* right) {
                             if (left->sourceParentId != right->sourceParentId) {
                                 return left->sourceParentId < right->sourceParentId;
                             }
                             return left->sourceSiblingIndex > right->sourceSiblingIndex;
                         });
        for (DuplicateGroup* group : groupOrder) {
            GameObject* source = Editor::Get().FindObjectById(group->sourceId);
            GameObject* parent = group->sourceParentId == 0
                ? nullptr
                : Editor::Get().FindObjectById(group->sourceParentId);
            group->root->SetParent(parent);
            if (parent && source) {
                group->root->SetSiblingIndex(source->GetSiblingIndex() + 1);
            }
        }

        std::vector<GameObject*> duplicated;
        for (const auto& group : groups) {
            for (const auto& object : group.objects) {
                if (object) duplicated.push_back(object.get());
            }
        }
        duplicateObjects_ = CapturePlacements(duplicated);
        if (duplicateRootIds_.empty()) return;
        selection.SelectMany(duplicateRootIds_, duplicateRootIds_.back(),
                             SelectionSource::Hierarchy);
        resultSelection_ = selection.State();
        Editor::Get().MarkSceneModified();
        return;
    }

    if (duplicateObjects_.empty()) return;
    RestorePlacements(duplicateObjects_);
    selection.RestoreState(resultSelection_, SelectionSource::Hierarchy);
    Editor::Get().MarkSceneModified();
}

void DuplicateObjectsCommand::Undo() {
    RemovePlacements(duplicateObjects_);
    Editor::Get().GetSelection().RestoreState(
        previousSelection_, SelectionSource::Hierarchy);
    if (!duplicateObjects_.empty()) Editor::Get().MarkSceneModified();
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
