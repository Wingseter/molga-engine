#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Core/TextureImportSettings.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using molga::AssetDatabase;
using molga::AssetRecord;
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
    std::string heroGuid = db.GuidForSource("Assets/hero.png");
    REQUIRE(heroGuid.size() == 32);

    const auto* rec = db.Find(heroGuid);
    REQUIRE(rec != nullptr);
    CHECK(rec->sourcePath == "Assets/hero.png");
    CHECK(rec->importer == "TextureImporter");

    // Legacy path lookup (without Assets/ prefix) should also resolve
    CHECK(db.GuidForSource("hero.png") == heroGuid);

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

TEST_CASE("A scanned root not named Assets resolves sources inside that root") {
    const fs::path root = fs::temp_directory_path() / "molga_adb_plain_root_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path clip = root / "idle.animclip";
    { std::ofstream(clip) << nlohmann::json({
        {"schemaVersion", 1}, {"textureGuid", ""}, {"frames", nlohmann::json::array()}
    }).dump(); }

    AssetDatabase db;
    db.ScanProject(root);
    const std::string guid = db.GuidForSource("idle.animclip");
    REQUIRE_FALSE(guid.empty());
    CHECK(db.AbsoluteSourcePath(guid) == clip);
    CHECK(db.GuidForAbsolutePath(clip) == guid);

    const fs::path catalog = root / "catalog.json";
    REQUIRE(db.SaveCatalog(catalog));
    nlohmann::json saved;
    { std::ifstream file(catalog); file >> saved; }
    REQUIRE(saved["records"].size() == 1);
    CHECK_FALSE(saved["records"][0]["hash"].get<std::string>().empty());

    fs::remove_all(root);
}

TEST_CASE("Texture sidecar migration keeps legacy color and unknown keys") {
    const fs::path root = fs::temp_directory_path() / "molga_adb_texture_migration_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    const fs::path texture = root / "Assets" / "legacy.png";
    { std::ofstream(texture) << "invalid fixture is enough for metadata"; }
    const std::string guid = "abababababababababababababababab";
    const nlohmann::json oldMeta = {
        {"guid", guid}, {"importer", "TextureImporter"}, {"importerVersion", 1},
        {"futureRootKey", {{"keep", 42}}},
        {"settings", {{"futureTextureKey", "keep-too"}}}
    };
    { std::ofstream(molga::AssetMeta::MetaPathFor(texture)) << oldMeta.dump(2); }

    AssetDatabase db;
    db.ScanProject(root / "Assets");
    const AssetRecord* record = db.Find(guid);
    REQUIRE(record != nullptr);
    CHECK(record->importerVersion == 2);
    const auto settings = molga::DeserializeTextureImportSettings(record->settings);
    CHECK(settings.colorSpace == molga::TextureColorSpace::LegacyLinear);
    CHECK(settings.pixelsPerUnit == doctest::Approx(1.0f));
    CHECK(record->settings["futureTextureKey"] == "keep-too");

    nlohmann::json migrated;
    { std::ifstream file(molga::AssetMeta::MetaPathFor(texture)); file >> migrated; }
    CHECK(migrated["futureRootKey"] == oldMeta["futureRootKey"]);
    CHECK(migrated["settings"]["futureTextureKey"] == "keep-too");
    CHECK(migrated["settings"]["colorSpace"] == "LegacyLinear");
    CHECK(migrated["settings"]["pixelsPerUnit"] == doctest::Approx(1.0));

    fs::remove_all(root);
}

TEST_CASE("A new texture sidecar receives SRGB P1 defaults") {
    const fs::path root = fs::temp_directory_path() / "molga_adb_texture_defaults_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    const fs::path texture = root / "Assets" / "new.png";
    { std::ofstream(texture) << "invalid fixture is enough for metadata"; }

    AssetDatabase db;
    db.ScanProject(root / "Assets");
    const std::string guid = db.GuidForSource("new.png");
    const AssetRecord* record = db.Find(guid);
    REQUIRE(record != nullptr);
    const auto settings = molga::DeserializeTextureImportSettings(record->settings, true);
    CHECK(settings.filter == molga::TextureFilterMode::Linear);
    CHECK(settings.wrapU == molga::TextureWrapMode::Clamp);
    CHECK(settings.wrapV == molga::TextureWrapMode::Clamp);
    CHECK_FALSE(settings.mipmaps);
    CHECK(settings.colorSpace == molga::TextureColorSpace::SRGB);
    CHECK(settings.pixelsPerUnit == doctest::Approx(1.0f));
    CHECK(settings.defaultPivot.x == doctest::Approx(0.5f));
    CHECK(settings.defaultPivot.y == doctest::Approx(0.5f));

    fs::remove_all(root);
}
