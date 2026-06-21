#pragma once

#include "Editor/Commands/EditorCommand.h"
#include "Common/Types.h"
#include <memory>
#include <string>
#include <vector>

class GameObject;

namespace molga {

// 텍스처 guid로 Sprite GameObject를 만든다. 테스트는 objects 벡터를 직접 주입하고,
// 에디터는 Editor의 활성 오브젝트 벡터를 넘긴다.
class CreateSpriteFromAssetCommand : public ICommand {
public:
    CreateSpriteFromAssetCommand(std::string textureGuid, std::string name,
                                 Vector2 worldPos,
                                 std::vector<std::shared_ptr<GameObject>>* objects);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Sprite"; }
    GameObject* created() const { return created_; }
private:
    std::string textureGuid_;
    std::string name_;
    Vector2 worldPos_;
    std::vector<std::shared_ptr<GameObject>>* objects_;
    std::shared_ptr<GameObject> object_;   // redo 재사용
    GameObject* created_ = nullptr;
};

} // namespace molga
