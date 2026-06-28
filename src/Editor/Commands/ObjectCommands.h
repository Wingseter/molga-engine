#pragma once

#include "Editor/Commands/EditorCommand.h"
#include "ECS/GameObject.h"
#include <memory>
#include <string>
#include <vector>

namespace molga {

// 새 GameObject(+Transform) 생성.
class CreateObjectCommand : public ICommand {
public:
    explicit CreateObjectCommand(std::string name);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Object"; }
    GameObject* created() const { return object_.get(); }
private:
    std::string name_;
    std::shared_ptr<GameObject> object_;  // 생성물을 보관해 redo 시 재사용
    unsigned int id_ = 0;
};

// 선택 subtree 삭제(자손 포함). undo 시 shared_ptr와 부모 링크를 복원.
class DeleteObjectCommand : public ICommand {
public:
    explicit DeleteObjectCommand(GameObject* root);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Delete Object"; }
private:
    struct Saved {
        std::shared_ptr<GameObject> obj;
        unsigned int parentId;  // 0 = 부모 없음
    };
    unsigned int rootId_;
    std::vector<Saved> saved_;  // 제거된 subtree(부모 먼저)
};

// 이름 변경.
class RenameObjectCommand : public ICommand {
public:
    RenameObjectCommand(unsigned int id, std::string newName);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Rename Object"; }
private:
    unsigned int id_;
    std::string newName_;
    std::string oldName_;
};

// 재부모화
class ReparentObjectCommand : public ICommand {
public:
    ReparentObjectCommand(unsigned int childId, unsigned int newParentId);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Reparent Object"; }
private:
    unsigned int childId_;
    unsigned int newParentId_;
    unsigned int oldParentId_ = 0;
};

// Duplicate: 선택된 오브젝트를 복제(Transform + SpriteRenderer 복사). undo 지원.
class DuplicateObjectCommand : public ICommand {
public:
    explicit DuplicateObjectCommand(GameObject* src);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Duplicate Object"; }
private:
    unsigned int srcId_;
    std::shared_ptr<GameObject> copy_;
    unsigned int copyId_ = 0;
    std::vector<std::shared_ptr<GameObject>> duplicatedObjects_;
};

} // namespace molga
