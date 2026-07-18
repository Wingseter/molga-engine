#pragma once

#include "Editor/Commands/EditorCommand.h"
#include <filesystem>
#include <string>

namespace molga {

// 소스 + 그 .meta를 함께 이동/이름 변경한다(.meta는 guid를 보존하므로 항상 동행).
class ProjectFileRenameCommand : public ICommand {
public:
    ProjectFileRenameCommand(std::filesystem::path src, std::string newName);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Rename Asset"; }
private:
    std::filesystem::path src_;
    std::filesystem::path dst_;
};

// 디렉터리 간 이동(.meta 동행).
class ProjectFileMoveCommand : public ICommand {
public:
    ProjectFileMoveCommand(std::filesystem::path src, std::filesystem::path dstDir);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Move Asset"; }
private:
    std::filesystem::path src_;
    std::filesystem::path dst_;
};

// 휴지통으로 이동(가역). trashDir는 보통 "<assets>/.trash".
class ProjectFileDeleteCommand : public ICommand {
public:
    ProjectFileDeleteCommand(std::filesystem::path src, std::filesystem::path trashDir);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Delete Asset"; }
private:
    std::filesystem::path src_;
    std::filesystem::path trashDir_;
    std::filesystem::path trashed_;   // 휴지통 안 실제 경로(undo 복원용)
};

// 빈 파일/폴더/스크립트 등 새 애셋 생성(undo는 삭제).
class ProjectFileCreateCommand : public ICommand {
public:
    ProjectFileCreateCommand(std::filesystem::path target, std::string contents, bool isDirectory);
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Create Asset"; }
private:
    std::filesystem::path target_;
    std::string contents_;
    bool isDirectory_;
};

// Atomic text replacement for JSON assets and .meta sidecars. Asset authoring
// has its own CommandHistory so these edits do not mark the active scene dirty.
class AssetContentCommand : public ICommand {
public:
    AssetContentCommand(std::filesystem::path target, std::string before,
                        std::string after, std::string assetGuid = {});
    void Execute() override;
    void Undo() override;
    std::string Name() const override { return "Edit Asset"; }
    bool Succeeded() const { return succeeded_; }

private:
    void Apply(const std::string& contents);
    std::filesystem::path target_;
    std::string before_;
    std::string after_;
    std::string assetGuid_;
    bool succeeded_ = false;
};

} // namespace molga
