#include "Core/AssetDatabase.h"
#include "Core/AssetDependencyValidator.h"
#include "Core/SpriteResolver.h"
#include "Core/TextureImportSettings.h"
#include "Core/PathService.h"
#include "doctest.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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
    CHECK(j["schemaVersion"] == 2);
    CHECK(j["assetRootMode"] == "packageRoot");
    CHECK(j["records"].is_array());
    CHECK(j["records"].size() == 2);
    for (const auto& record : j["records"]) {
        const auto hash = record.value("hash", std::string());
        CHECK_FALSE(hash.empty());
        CHECK(hash != "dummy_hash");
    }

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
    CHECK_FALSE(rec->hash.empty());
    CHECK(rec->hash != "dummy_hash");
    CHECK(rec->settings.is_object());
    CHECK(rec->settings["colorSpace"] == "SRGB");
    // The fixture is deliberately not a valid PNG. Catalog v2 retains this
    // diagnostic instead of turning a failed import into a usable asset.
    CHECK(rec->importFailed);
    CHECK_FALSE(rec->importError.empty());

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

TEST_CASE("Catalog v1 remains readable with v2 fields defaulted") {
    const fs::path root = fs::temp_directory_path() / "molga_catalog_v1_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    const std::string guid = "11111111111111111111111111111111";
    const nlohmann::json v1 = {
        {"schemaVersion", 1},
        {"records", nlohmann::json::array({{
            {"guid", guid}, {"sourcePath", "Assets/legacy.png"},
            {"importer", "TextureImporter"}, {"importerVersion", 1},
            {"artifactPath", ""}, {"hash", "legacy-hash"},
            {"width", 32}, {"height", 16}
        }})}
    };
    const fs::path catalog = root / "catalog.json";
    { std::ofstream(catalog) << v1.dump(2); }

    AssetDatabase db;
    REQUIRE(db.LoadCatalog(catalog, root));
    const AssetRecord* record = db.Find(guid);
    REQUIRE(record != nullptr);
    CHECK(record->settings.empty());
    CHECK(record->dependencies.empty());
    CHECK(record->metadata.empty());
    CHECK_FALSE(record->importFailed);
    CHECK_FALSE(record->generated);
    CHECK(db.AbsoluteSourcePath(guid) == root / "Assets" / "legacy.png");

    fs::remove_all(root);
}

TEST_CASE("Catalog v2 round-trips settings dependencies metadata and failures") {
    const fs::path root = fs::temp_directory_path() / "molga_catalog_v2_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    const std::string clipGuid = "22222222222222222222222222222222";
    const std::string textureGuid = "33333333333333333333333333333333";
    const nlohmann::json v2 = {
        {"schemaVersion", 2}, {"assetRootMode", "packageRoot"},
        {"records", nlohmann::json::array({
            {
                {"guid", clipGuid}, {"sourcePath", "Assets/hero.animclip"},
                {"importer", "AnimationClipImporter"}, {"importerVersion", 1},
                {"settings", {{"custom", 7}}},
                {"dependencies", {textureGuid}},
                {"metadata", {{"schemaVersion", 3}}},
                {"importFailed", true}, {"importError", "fixture failure"},
                {"generated", true}, {"width", 0}, {"height", 0}
            },
            {
                {"guid", textureGuid}, {"sourcePath", "Assets/hero.png"},
                {"importer", "TextureImporter"}, {"importerVersion", 2},
                {"settings", nlohmann::json::object()},
                {"dependencies", nlohmann::json::array()},
                {"metadata", nlohmann::json::object()},
                {"importFailed", false}, {"generated", false},
                {"width", 64}, {"height", 32}
            }
        })}
    };
    const fs::path input = root / "catalog_in.json";
    const fs::path output = root / "catalog_out.json";
    { std::ofstream(input) << v2.dump(2); }

    AssetDatabase db;
    REQUIRE(db.LoadCatalog(input, root));
    const AssetRecord* clip = db.Find(clipGuid);
    REQUIRE(clip != nullptr);
    CHECK(clip->settings["custom"] == 7);
    CHECK(clip->dependencies == std::vector<std::string>{textureGuid});
    CHECK(clip->metadata["schemaVersion"] == 3);
    CHECK(clip->importFailed);
    CHECK(clip->importError == "fixture failure");
    CHECK(clip->generated);
    REQUIRE(db.SaveCatalog(output));

    AssetDatabase loaded;
    REQUIRE(loaded.LoadCatalog(output, root));
    const AssetRecord* roundTrip = loaded.Find(clipGuid);
    REQUIRE(roundTrip != nullptr);
    CHECK(roundTrip->settings == clip->settings);
    CHECK(roundTrip->dependencies == clip->dependencies);
    CHECK(roundTrip->metadata == clip->metadata);
    CHECK(roundTrip->importFailed == clip->importFailed);
    CHECK(roundTrip->importError == clip->importError);
    CHECK(roundTrip->generated == clip->generated);

    fs::remove_all(root);
}

TEST_CASE("SpriteResolver computes top-left UV pivot and PPU without GL") {
    const fs::path root = fs::temp_directory_path() / "molga_sprite_metadata_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    const std::string textureGuid = "44444444444444444444444444444444";
    const std::string sliceGuid = "55555555555555555555555555555555";

    molga::TextureImportSettings settings;
    settings.spriteMode = molga::SpriteImportMode::Multiple;
    settings.pixelsPerUnit = 4.0f;
    settings.slices.push_back({sliceGuid, "run", {10, 5, 20, 10}, {0.25f, 0.75f}});
    const nlohmann::json catalogJson = {
        {"schemaVersion", 2},
        {"records", nlohmann::json::array({{
            {"guid", textureGuid}, {"sourcePath", "Assets/sheet.png"},
            {"importer", "TextureImporter"}, {"importerVersion", 2},
            {"settings", molga::SerializeTextureImportSettings(settings)},
            {"dependencies", nlohmann::json::array()},
            {"metadata", nlohmann::json::object()}, {"importFailed", false},
            {"width", 100}, {"height", 50}
        }})}
    };
    const fs::path catalog = root / "catalog.json";
    { std::ofstream(catalog) << catalogJson.dump(2); }
    AssetDatabase db;
    REQUIRE(db.LoadCatalog(catalog, root));

    const auto resolved = molga::SpriteResolver::ResolveMetadata(
        {textureGuid, sliceGuid}, db);
    CHECK(resolved.valid);
    CHECK(resolved.texture == nullptr);
    CHECK(resolved.uv.u0 == doctest::Approx(0.1f));
    CHECK(resolved.uv.u1 == doctest::Approx(0.3f));
    CHECK(resolved.uv.v0 == doctest::Approx(0.7f));
    CHECK(resolved.uv.v1 == doctest::Approx(0.9f));
    CHECK(resolved.pivot.x == doctest::Approx(0.25f));
    CHECK(resolved.pivot.y == doctest::Approx(0.75f));
    CHECK(resolved.nativeSize.x == doctest::Approx(5.0f));
    CHECK(resolved.nativeSize.y == doctest::Approx(2.5f));
    CHECK_FALSE(molga::SpriteResolver::ResolveMetadata({textureGuid, ""}, db).valid);
    CHECK_FALSE(molga::SpriteResolver::ResolveMetadata(
        {textureGuid, "66666666666666666666666666666666"}, db).valid);

    fs::remove_all(root);
}

TEST_CASE("Dependency validator reports missing and type-mismatched scene references") {
    const fs::path root = fs::temp_directory_path() / "molga_dependency_scene_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    const std::string textureGuid = "77777777777777777777777777777777";
    const std::string normalGuid = "76767676767676767676767676767676";
    const std::string controllerGuid = "78787878787878787878787878787878";
    const std::string missingClipGuid = "79797979797979797979797979797979";
    const nlohmann::json catalogJson = {
        {"schemaVersion", 2},
        {"records", nlohmann::json::array({
            {{"guid", textureGuid}, {"sourcePath", "Assets/hero.png"},
             {"importer", "TextureImporter"}, {"importerVersion", 2},
             {"settings", nlohmann::json::object()},
             {"dependencies", nlohmann::json::array()},
             {"metadata", nlohmann::json::object()}, {"importFailed", false},
             {"width", 1}, {"height", 1}},
            {{"guid", normalGuid}, {"sourcePath", "Assets/hero_normal.png"},
             {"importer", "TextureImporter"}, {"importerVersion", 2},
             {"settings", {{"usage", "NormalMap"},
                            {"colorSpace", "LegacyLinear"}}},
             {"dependencies", nlohmann::json::array()},
             {"metadata", nlohmann::json::object()}, {"importFailed", false},
             {"width", 1}, {"height", 1}},
            {{"guid", controllerGuid}, {"sourcePath", "Assets/hero.animator"},
             {"importer", "AnimatorControllerImporter"}, {"importerVersion", 1},
             {"settings", nlohmann::json::object()},
             {"dependencies", {missingClipGuid}},
             {"metadata", nlohmann::json::object()}, {"importFailed", false}}
        })}
    };
    const fs::path catalog = root / "catalog.json";
    { std::ofstream(catalog) << catalogJson.dump(2); }
    AssetDatabase db;
    REQUIRE(db.LoadCatalog(catalog, root));

    const fs::path missingScene = root / "missing.scene";
    { std::ofstream(missingScene) << nlohmann::json({
        {"type", "Animator2D"},
        {"controllerGuid", "88888888888888888888888888888888"}
    }).dump(); }
    const auto missing = molga::AssetDependencyValidator::ValidateScenes({missingScene}, db);
    REQUIRE(missing.issues.size() == 1);
    CHECK(missing.issues[0].code == molga::DependencyIssueCode::Missing);
    CHECK(missing.issues[0].expectedImporter == "AnimatorControllerImporter");

    const fs::path mismatchScene = root / "mismatch.scene";
    { std::ofstream(mismatchScene) << nlohmann::json({
        {"type", "Animator2D"}, {"controllerGuid", textureGuid}
    }).dump(); }
    const auto mismatch = molga::AssetDependencyValidator::ValidateScenes({mismatchScene}, db);
    REQUIRE(mismatch.issues.size() == 1);
    CHECK(mismatch.issues[0].code == molga::DependencyIssueCode::TypeMismatch);
    CHECK(mismatch.issues[0].expectedImporter == "AnimatorControllerImporter");
    CHECK(mismatch.issues[0].actualImporter == "TextureImporter");

    const fs::path usageScene = root / "normal_usage.scene";
    { std::ofstream(usageScene) << nlohmann::json({
        {"type", "SpriteRenderer"}, {"normalMapGuid", textureGuid}
    }).dump(); }
    const auto usageMismatch =
        molga::AssetDependencyValidator::ValidateScenes({usageScene}, db);
    REQUIRE(usageMismatch.issues.size() == 1);
    CHECK(usageMismatch.issues[0].code ==
          molga::DependencyIssueCode::UsageMismatch);
    CHECK(usageMismatch.issues[0].expectedImporter == "TextureImporter");
    CHECK(usageMismatch.issues[0].actualImporter == "TextureImporter");

    const fs::path validNormalScene = root / "normal_valid.scene";
    { std::ofstream(validNormalScene) << nlohmann::json({
        {"type", "SpriteRenderer"}, {"normalMapGuid", normalGuid}
    }).dump(); }
    CHECK(molga::AssetDependencyValidator::ValidateScenes(
        {validNormalScene}, db).Ok());

    const fs::path overrideUsageScene = root / "normal_override_usage.scene";
    { std::ofstream(overrideUsageScene) << nlohmann::json({
        {"modifications", nlohmann::json::array({{
            {"target", 1},
            {"component", "SpriteRenderer"},
            {"key", "normalMapGuid"},
            {"value", textureGuid}
        }})}
    }).dump(); }
    const auto overrideUsageMismatch =
        molga::AssetDependencyValidator::ValidateScenes(
            {overrideUsageScene}, db);
    REQUIRE(overrideUsageMismatch.issues.size() == 1);
    CHECK(overrideUsageMismatch.issues[0].code ==
          molga::DependencyIssueCode::UsageMismatch);
    CHECK(overrideUsageMismatch.issues[0].guid == textureGuid);

    const fs::path validOverrideScene = root / "normal_override_valid.scene";
    { std::ofstream(validOverrideScene) << nlohmann::json({
        {"modifications", nlohmann::json::array({{
            {"target", 1},
            {"component", "SpriteRenderer"},
            {"key", "normalMapGuid"},
            {"value", normalGuid}
        }})}
    }).dump(); }
    CHECK(molga::AssetDependencyValidator::ValidateScenes(
        {validOverrideScene}, db).Ok());

    const fs::path recursiveScene = root / "recursive.scene";
    { std::ofstream(recursiveScene) << nlohmann::json({
        {"type", "Animator2D"}, {"controllerGuid", controllerGuid}
    }).dump(); }
    const auto recursive = molga::AssetDependencyValidator::ValidateScenes({recursiveScene}, db);
    REQUIRE(recursive.issues.size() == 1);
    CHECK(recursive.issues[0].code == molga::DependencyIssueCode::Missing);
    CHECK(recursive.issues[0].guid == missingClipGuid);
    CHECK(recursive.issues[0].expectedImporter == "AnimationClipImporter");
    CHECK(recursive.issues[0].owner == "Assets/hero.animator");

    fs::remove_all(root);
}

TEST_CASE("Dependency validator detects recursive prefab cycles") {
    const fs::path root = fs::temp_directory_path() / "molga_dependency_cycle_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets");
    const std::string aGuid = "99999999999999999999999999999999";
    const std::string bGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    { std::ofstream(root / "Assets" / "a.prefab") << nlohmann::json({
        {"type", "PrefabInstance"}, {"guid", bGuid}
    }).dump(); }
    { std::ofstream(root / "Assets" / "b.prefab") << nlohmann::json({
        {"type", "PrefabInstance"}, {"guid", aGuid}
    }).dump(); }
    const nlohmann::json catalogJson = {
        {"schemaVersion", 2},
        {"records", nlohmann::json::array({
            {{"guid", aGuid}, {"sourcePath", "Assets/a.prefab"},
             {"importer", "PrefabImporter"}, {"importerVersion", 1},
             {"dependencies", nlohmann::json::array()}, {"importFailed", false}},
            {{"guid", bGuid}, {"sourcePath", "Assets/b.prefab"},
             {"importer", "PrefabImporter"}, {"importerVersion", 1},
             {"dependencies", nlohmann::json::array()}, {"importFailed", false}}
        })}
    };
    const fs::path catalog = root / "catalog.json";
    { std::ofstream(catalog) << catalogJson.dump(2); }
    AssetDatabase db;
    REQUIRE(db.LoadCatalog(catalog, root));

    const auto result = molga::AssetDependencyValidator::ValidateAssetRoots({aGuid}, db);
    CHECK_FALSE(result.Ok());
    CHECK(std::any_of(result.issues.begin(), result.issues.end(), [](const auto& issue) {
        return issue.code == molga::DependencyIssueCode::Cycle;
    }));
    CHECK(result.Summary().find("cyclic asset dependency") != std::string::npos);

    fs::remove_all(root);
}
