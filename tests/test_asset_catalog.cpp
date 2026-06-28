#include "Core/AssetDatabase.h"
#include "Core/PathService.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using molga::AssetDatabase;
using molga::AssetRecord;
namespace fs = std::filesystem;

static fs::path MakeCatalogProject() {
    fs::path root = fs::temp_directory_path() / "molga_catalog_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets" / "Textures");
    { std::ofstream(root / "Assets" / "Textures" / "player.png") << "img"; }
    { std::ofstream(root / "Assets" / "sfx.wav") << "snd"; }
    return root;
}

TEST_CASE("SaveCatalog writes JSON with all records") {
    fs::path root = MakeCatalogProject();
    AssetDatabase db;
    db.ScanProject(root / "Assets");

    CHECK(db.RecordCount() == 2);

    fs::path catalogPath = root / "asset_catalog.json";
    REQUIRE(db.SaveCatalog(catalogPath));
    REQUIRE(fs::exists(catalogPath));

    // Read the file and verify JSON structure
    std::ifstream file(catalogPath);
    nlohmann::json j;
    file >> j;
    CHECK(j["schemaVersion"] == 1);
    CHECK(j["assetRootMode"] == "packageRoot");
    CHECK(j["records"].is_array());
    CHECK(j["records"].size() == 2);

    fs::remove_all(root);
}

TEST_CASE("LoadCatalog restores records and allows GUID lookup") {
    fs::path root = MakeCatalogProject();
    AssetDatabase scanDb;
    scanDb.ScanProject(root / "Assets");

    std::string playerGuid = scanDb.GuidForSource("Assets/Textures/player.png");
    REQUIRE(playerGuid.size() == 32);

    fs::path catalogPath = root / "asset_catalog.json";
    REQUIRE(scanDb.SaveCatalog(catalogPath));

    // Load into a fresh database
    AssetDatabase loadDb;
    REQUIRE(loadDb.LoadCatalog(catalogPath, root));

    CHECK(loadDb.RecordCount() == 2);

    const AssetRecord* rec = loadDb.Find(playerGuid);
    REQUIRE(rec != nullptr);
    CHECK(rec->sourcePath == "Assets/Textures/player.png");
    CHECK(rec->importer == "TextureImporter");

    // GuidForSource should work
    CHECK(loadDb.GuidForSource("Assets/Textures/player.png") == playerGuid);

    // AbsoluteSourcePath should resolve through package root
    fs::path absPath = loadDb.AbsoluteSourcePath(playerGuid);
    CHECK(absPath == root / "Assets" / "Textures" / "player.png");

    fs::remove_all(root);
}

TEST_CASE("Clear removes all records") {
    fs::path root = MakeCatalogProject();
    AssetDatabase db;
    db.ScanProject(root / "Assets");
    CHECK(db.RecordCount() == 2);

    db.Clear();
    CHECK(db.RecordCount() == 0);
    CHECK(db.GuidForSource("Assets/Textures/player.png").empty());

    fs::remove_all(root);
}

TEST_CASE("LoadCatalog returns false for missing file") {
    AssetDatabase db;
    CHECK_FALSE(db.LoadCatalog("/tmp/nonexistent_catalog.json", "/tmp"));
}

TEST_CASE("Path normalization: foo.png, Assets/foo.png, and backslash resolve consistently") {
    fs::path root = fs::temp_directory_path() / "molga_pathnorm_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    { std::ofstream(root / "Assets" / "foo.png") << "img"; }

    AssetDatabase db;
    db.ScanProject(root / "Assets");

    std::string guid1 = db.GuidForSource("foo.png");
    std::string guid2 = db.GuidForSource("Assets/foo.png");
    std::string guid3 = db.GuidForSource("Assets\\foo.png");

    REQUIRE(!guid1.empty());
    CHECK(guid1 == guid2);
    CHECK(guid1 == guid3);

    fs::remove_all(root);
}
