#pragma once

#include <string>

namespace molga {

// 되돌릴 수 있는 단일 편집 동작.
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0;          // 적용 / 다시 적용(redo)
    virtual void Undo() = 0;             // 되돌리기
    virtual std::string Name() const = 0;
};

} // namespace molga
