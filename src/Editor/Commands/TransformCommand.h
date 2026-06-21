#pragma once

#include "Editor/Commands/EditorCommand.h"
#include "Common/Types.h"
#include <string>

class World;
class Transform;

namespace molga {

// 한 Transform의 스냅샷(position/rotation(deg)/scale).
struct TransformState {
    Vector2 position;
    float   rotation;
    Vector2 scale;
};

// 단일 GameObject의 transform 변경을 before/after 스냅샷으로 기록하는 command.
// World*를 주입받아(테스트) 또는 nullptr이면 Editor의 활성 World를 사용(에디터).
class TransformCommand : public ICommand {
public:
    TransformCommand(World* world, unsigned int targetId,
                     const TransformState& before, const TransformState& after);

    void Execute() override;   // after 적용
    void Undo() override;      // before 복원
    std::string Name() const override { return "Transform"; }

    static TransformState Capture(const Transform* tr);

private:
    void ApplyTo(const TransformState& s);
    Transform* Resolve() const;

    World* world_;             // nullptr = Editor::Get().ActiveWorld 사용
    unsigned int targetId_;
    TransformState before_;
    TransformState after_;
};

} // namespace molga
