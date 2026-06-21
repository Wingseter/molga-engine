#include "Editor/Watcher/AssetWatcher.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using molga::AssetWatcher;

TEST_CASE("watcher reports added and removed files between polls") {
    fs::path root = fs::temp_directory_path() / "molga_watch_test";
    fs::remove_all(root);
    fs::create_directories(root);

    AssetWatcher w;
    w.Prime(root);                              // 최초 스냅샷

    { std::ofstream(root / "new.png") << "x"; }
    AssetWatcher::Changes c1 = w.Poll(root);
    CHECK(c1.added.size() == 1);
    CHECK(c1.removed.empty());

    fs::remove(root / "new.png");
    AssetWatcher::Changes c2 = w.Poll(root);
    CHECK(c2.removed.size() == 1);
    CHECK(c2.added.empty());

    fs::remove_all(root);
}
