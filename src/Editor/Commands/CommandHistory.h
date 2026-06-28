#pragma once

#include "Editor/Commands/EditorCommand.h"
#include <memory>
#include <vector>

namespace molga {

// Undo/Redo 스택. Execute는 즉시 실행 후 undo 스택에 쌓고 redo 스택을 비운다.
class CommandHistory {
public:
    void Execute(std::unique_ptr<ICommand> cmd) {
        if (!cmd) return;

        // Redo branch truncation invalidates clean marker if it discards the saved state.
        if (hasCleanMarker_ && cleanIndex_ > static_cast<int>(undo_.size())) {
            hasCleanMarker_ = false;
        }

        cmd->Execute();
        undo_.push_back(std::move(cmd));
        redo_.clear();
    }

    void Undo() {
        if (undo_.empty()) return;
        auto cmd = std::move(undo_.back());
        undo_.pop_back();
        cmd->Undo();
        redo_.push_back(std::move(cmd));
    }

    void Redo() {
        if (redo_.empty()) return;
        auto cmd = std::move(redo_.back());
        redo_.pop_back();
        cmd->Execute();
        undo_.push_back(std::move(cmd));
    }

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

    void Clear() {
        undo_.clear();
        redo_.clear();
        hasCleanMarker_ = true;
        cleanIndex_ = 0;
    }

    void MarkClean() {
        cleanIndex_ = static_cast<int>(undo_.size());
        hasCleanMarker_ = true;
    }

    bool IsDirty() const {
        if (!hasCleanMarker_) return true;
        return static_cast<int>(undo_.size()) != cleanIndex_;
    }

private:
    std::vector<std::unique_ptr<ICommand>> undo_;
    std::vector<std::unique_ptr<ICommand>> redo_;
    int cleanIndex_ = 0;
    bool hasCleanMarker_ = true;
};

} // namespace molga
