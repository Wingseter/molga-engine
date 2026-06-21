#include "Core/AssetDatabase.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

using molga::AssetDatabase;
namespace fs = std::filesystem;

static fs::path MakeProject() {
    fs::path root = fs::temp_directory_path() / "molga_adb_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    { std::ofstream(root / "Assets" / "hero.png") << "img"; }
    { std::ofstream(root / "Assets" / "shot.wav") << "snd"; }
    return root / "Assets";
}

TEST_CASE("Scan assigns one record per source asset and builds both maps") {
    fs::path assets = MakeProject();
    AssetDatabase db;
    db.ScanProject(assets);

    CHECK(db.RecordCount() == 2);                  // .meta는 카운트하지 않는다
    std::string heroGuid = db.GuidForSource("hero.png");
    REQUIRE(heroGuid.size() == 32);

    const auto* rec = db.Find(heroGuid);
    REQUIRE(rec != nullptr);
    CHECK(rec->sourcePath == "hero.png");
    CHECK(rec->importer == "TextureImporter");

    fs::remove_all(assets.parent_path());
}

TEST_CASE("Re-scan keeps guids stable (meta persists)") {
    fs::path assets = MakeProject();
    AssetDatabase db;
    db.ScanProject(assets);
    std::string first = db.GuidForSource("hero.png");
    db.ScanProject(assets);
    CHECK(db.GuidForSource("hero.png") == first);
    fs::remove_all(assets.parent_path());
}

TEST_CASE("Unknown guid resolves to nullptr") {
    AssetDatabase db;
    CHECK(db.Find("ffffffffffffffffffffffffffffffff") == nullptr);
}
