#include "Editor/Commands/ProjectFileCommands.h"
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Core/PersistentStorage.h"
#include "Common/Log.h"
#include <fstream>

namespace molga {

namespace fs = std::filesystem;

static void MoveWithMeta(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    fs::path fromMeta = AssetMeta::MetaPathFor(from);
    if (fs::exists(fromMeta)) {
        fs::rename(fromMeta, AssetMeta::MetaPathFor(to), ec);
    }
}

// ── Rename ─────────────────────────────────────────────────────────────────
ProjectFileRenameCommand::ProjectFileRenameCommand(fs::path src, std::string newName)
    : src_(std::move(src)) {
    dst_ = src_.parent_path() / newName;
}
void ProjectFileRenameCommand::Execute() {
    MoveWithMeta(src_, dst_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(src_, AssetDatabase::Get().Root()),
        fs::relative(dst_, AssetDatabase::Get().Root()));
}
void ProjectFileRenameCommand::Undo() {
    MoveWithMeta(dst_, src_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(dst_, AssetDatabase::Get().Root()),
        fs::relative(src_, AssetDatabase::Get().Root()));
}

// ── Move ───────────────────────────────────────────────────────────────────
ProjectFileMoveCommand::ProjectFileMoveCommand(fs::path src, fs::path dstDir)
    : src_(std::move(src)) {
    dst_ = dstDir / src_.filename();
}
void ProjectFileMoveCommand::Execute() {
    MoveWithMeta(src_, dst_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(src_, AssetDatabase::Get().Root()),
        fs::relative(dst_, AssetDatabase::Get().Root()));
}
void ProjectFileMoveCommand::Undo() {
    MoveWithMeta(dst_, src_);
    AssetDatabase::Get().OnSourceRenamed(
        fs::relative(dst_, AssetDatabase::Get().Root()),
        fs::relative(src_, AssetDatabase::Get().Root()));
}

// ── Delete (휴지통) ─────────────────────────────────────────────────────────
ProjectFileDeleteCommand::ProjectFileDeleteCommand(fs::path src, fs::path trashDir)
    : src_(std::move(src)), trashDir_(std::move(trashDir)) {}
void ProjectFileDeleteCommand::Execute() {
    std::error_code ec;
    fs::create_directories(trashDir_, ec);
    trashed_ = trashDir_ / src_.filename();
    MoveWithMeta(src_, trashed_);
    AssetDatabase::Get().OnSourceRemoved(
        fs::relative(src_, AssetDatabase::Get().Root()));
}
void ProjectFileDeleteCommand::Undo() {
    MoveWithMeta(trashed_, src_);
    AssetDatabase::Get().OnSourceAdded(
        fs::relative(src_, AssetDatabase::Get().Root()));
}

// ── Create ─────────────────────────────────────────────────────────────────
ProjectFileCreateCommand::ProjectFileCreateCommand(fs::path target, std::string contents, bool isDirectory)
    : target_(std::move(target)), contents_(std::move(contents)), isDirectory_(isDirectory) {}
void ProjectFileCreateCommand::Execute() {
    if (isDirectory_) {
        fs::create_directories(target_);
    } else {
        std::string error;
        if (!PersistentStorage::AtomicWriteText(target_, contents_, &error)) {
            Log::Error("ProjectFile", "Could not create '" + target_.string() + "': " + error);
            return;
        }
        AssetDatabase::Get().OnSourceAdded(
            fs::relative(target_, AssetDatabase::Get().Root()));
    }
}
void ProjectFileCreateCommand::Undo() {
    std::error_code ec;
    fs::remove_all(target_, ec);
    fs::remove(AssetMeta::MetaPathFor(target_), ec);
    if (!isDirectory_) {
        AssetDatabase::Get().OnSourceRemoved(
            fs::relative(target_, AssetDatabase::Get().Root()));
    }
}

AssetContentCommand::AssetContentCommand(fs::path target, std::string before,
                                         std::string after, std::string assetGuid)
    : target_(std::move(target)), before_(std::move(before)),
      after_(std::move(after)), assetGuid_(std::move(assetGuid)) {}

void AssetContentCommand::Apply(const std::string& contents) {
    std::string error;
    succeeded_ = PersistentStorage::AtomicWriteText(target_, contents, &error);
    if (!succeeded_) {
        Log::Error("AssetContent", "Could not write '" + target_.string() + "': " + error);
        return;
    }
    if (!assetGuid_.empty()) {
        // A failed import intentionally keeps the edited source/meta and the
        // last-good runtime resource. The catalog exposes the failure badge;
        // Undo remains able to restore the prior contents.
        AssetDatabase::Get().TryReimport(assetGuid_, &error);
        if (!error.empty()) Log::Warn("AssetContent", error);
    }
}

void AssetContentCommand::Execute() { Apply(after_); }
void AssetContentCommand::Undo() { Apply(before_); }

} // namespace molga
