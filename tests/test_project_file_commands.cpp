#include "Editor/Commands/ProjectFileCommands.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using molga::ProjectFileDeleteCommand;
using molga::ProjectFileRenameCommand;

static fs::path Seed() {
    fs::path root = fs::temp_directory_path() / "molga_pfc_test";
    fs::remove_all(root);
    fs::create_directories(root);
    { std::ofstream(root / "hero.png") << "img"; }
    return root;
}

TEST_CASE("delete moves the file to trash and undo restores it") {
    fs::path root = Seed();
    fs::path file = root / "hero.png";
    fs::path trash = root / ".trash";

    ProjectFileDeleteCommand cmd(file, trash);
    cmd.Execute();
    CHECK_FALSE(fs::exists(file));   // 원본은 사라지고
    CHECK(fs::exists(trash));        // 휴지통으로 이동
    cmd.Undo();
    CHECK(fs::exists(file));         // 복원됨
    fs::remove_all(root);
}

TEST_CASE("rename moves the source and its .meta together; undo reverts both") {
    fs::path root = Seed();
    { std::ofstream(root / "hero.png.meta") << "{}"; }
    ProjectFileRenameCommand cmd(root / "hero.png", "villain.png");
    cmd.Execute();
    CHECK(fs::exists(root / "villain.png"));
    CHECK(fs::exists(root / "villain.png.meta"));   // .meta가 따라온다
    CHECK_FALSE(fs::exists(root / "hero.png"));
    cmd.Undo();
    CHECK(fs::exists(root / "hero.png"));
    CHECK(fs::exists(root / "hero.png.meta"));
    fs::remove_all(root);
}
