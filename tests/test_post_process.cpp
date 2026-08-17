#include "Core/AssetDatabase.h"
#include "Core/AssetDependencyValidator.h"
#include "Core/Importers/ImporterRegistry.h"
#include "ECS/Components/Camera.h"
#include "Editor/Properties/EditorPropertyDescriptor.h"
#include "Rendering/PostProcessProfile2D.h"
#include "Rendering/PostProcessProfileResolver.h"
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;
using namespace molga;

namespace {

nlohmann::json CompleteProfile() {
    return {
        {"schemaVersion", 1},
        {"rootExtension", "retained"},
        {"effects", nlohmann::json::array({
            {{"type", "Bloom"}, {"enabled", true}, {"threshold", 0.8},
             {"softKnee", 0.5}, {"intensity", 0.6}, {"scatter", 0.7},
             {"pluginBloomKey", 42}},
            {{"type", "ColorAdjust"}, {"enabled", true}, {"exposureEV", 0.0},
             {"contrast", 0.0}, {"saturation", 1.0}, {"tint", {1.0, 1.0, 1.0}}},
            {{"type", "Vignette"}, {"enabled", true}, {"intensity", 0.2},
             {"smoothness", 0.5}, {"color", {0.0, 0.0, 0.0}}}
        })}
    };
}

void WriteJson(const fs::path& path, const nlohmann::json& value) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << value.dump(2) << '\n';
}

} // namespace

TEST_CASE("Post-process profile preserves order, extensions, and defaults") {
    PostProcessProfile2D profile;
    std::string error;
    REQUIRE(PostProcessProfile2D::Deserialize(CompleteProfile(), profile, &error));
    REQUIRE(profile.effects.size() == 3);
    CHECK(profile.effects[0].type == PostProcessEffectType2D::Bloom);
    CHECK(profile.effects[1].type == PostProcessEffectType2D::ColorAdjust);
    CHECK(profile.effects[2].type == PostProcessEffectType2D::Vignette);
    CHECK(profile.ActiveEffectCount() == 2); // neutral ColorAdjust bypasses

    const nlohmann::json roundTrip = profile.Serialize();
    CHECK(roundTrip["rootExtension"] == "retained");
    CHECK(roundTrip["effects"][0]["pluginBloomKey"] == 42);
    CHECK(roundTrip["effects"][1]["type"] == "ColorAdjust");

    PostProcessProfile2D defaults;
    REQUIRE(PostProcessProfile2D::Deserialize(
        {{"schemaVersion", 1}, {"effects", {{{"type", "Bloom"}}}}},
        defaults, &error));
    const auto& bloom = std::get<BloomSettings2D>(defaults.effects[0].settings);
    CHECK(bloom.threshold == doctest::Approx(0.8f));
    CHECK(bloom.softKnee == doctest::Approx(0.5f));
    CHECK(bloom.intensity == doctest::Approx(0.6f));
    CHECK(bloom.scatter == doctest::Approx(0.7f));
}

TEST_CASE("Post-process profile rejects schema, duplicate, unknown, range, and NaN") {
    const auto rejects = [](nlohmann::json document) {
        PostProcessProfile2D profile;
        std::string error;
        CHECK_FALSE(PostProcessProfile2D::Deserialize(document, profile, &error));
        CHECK_FALSE(error.empty());
    };
    rejects({{"schemaVersion", 2}, {"effects", nlohmann::json::array()}});
    rejects({{"schemaVersion", 1}, {"effects", {
        {{"type", "Bloom"}}, {{"type", "Bloom"}}}}});
    rejects({{"schemaVersion", 1}, {"effects", {{{"type", "FilmGrain"}}}}});
    rejects({{"schemaVersion", 1}, {"effects", {
        {{"type", "Bloom"}, {"threshold", 16.01}}}}});
    rejects({{"schemaVersion", 1}, {"effects", {
        {{"type", "Vignette"}, {"smoothness", 0.0}}}}});
    rejects({{"schemaVersion", 1}, {"effects", {
        {{"type", "ColorAdjust"},
         {"exposureEV", std::numeric_limits<double>::quiet_NaN()}}}}});
}

TEST_CASE("Disabled and mathematically neutral post effects completely bypass") {
    PostProcessProfile2D profile;
    std::string error;
    REQUIRE(PostProcessProfile2D::Deserialize(
        {{"schemaVersion", 1}, {"effects", {
            {{"type", "Bloom"}, {"enabled", false}},
            {{"type", "ColorAdjust"}, {"enabled", true}},
            {{"type", "Vignette"}, {"enabled", true}, {"intensity", 0.0}}
        }}}, profile, &error));
    CHECK_FALSE(profile.HasActiveEffects());
    CHECK(profile.ActiveEffectCount() == 0);
}

TEST_CASE("Post-process importer and resolver refresh hashes, preview, and last-good") {
    const fs::path root = fs::temp_directory_path() / "molga_postfx_resolver_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets");
    const fs::path source = root / "Assets" / "camera.postfx";
    const std::string guid = "1234567890abcdef1234567890abcdef";
    WriteJson(source, CompleteProfile());
    WriteJson(source.string() + ".meta", {
        {"guid", guid}, {"importer", "PostProcessProfileImporter"},
        {"importerVersion", 1}});

    AssetDatabase database;
    database.ScanProject(root / "Assets");
    const AssetRecord* record = database.Find(guid);
    REQUIRE(record != nullptr);
    CHECK(record->importer == "PostProcessProfileImporter");
    CHECK_FALSE(record->hash.empty());
    CHECK(ImporterRegistry::Get().FindForExtension(".POSTFX")->Name() ==
          "PostProcessProfileImporter");

    auto& resolver = PostProcessProfileResolver::Get();
    resolver.ClearForTesting();
    auto first = resolver.Resolve(guid, database);
    REQUIRE(first);
    CHECK(first.profile->effects.size() == 3);

    nlohmann::json changed = CompleteProfile();
    changed["effects"][0]["intensity"] = 1.5;
    WriteJson(source, changed);
    REQUIRE(database.TryReimport(guid));
    auto refreshed = resolver.Resolve(guid, database);
    REQUIRE(refreshed);
    CHECK(std::get<BloomSettings2D>(refreshed.profile->effects[0].settings).intensity ==
          doctest::Approx(1.5f));

    PostProcessProfile2D preview = *refreshed.profile;
    std::get<BloomSettings2D>(preview.effects[0].settings).intensity = 2.5f;
    REQUIRE(resolver.SetTransientOverride(guid, preview));
    auto transient = resolver.Resolve(guid, database);
    REQUIRE(transient);
    CHECK(transient.status == PostProcessProfileResolveStatus::TransientPreview);
    CHECK(std::get<BloomSettings2D>(transient.profile->effects[0].settings).intensity ==
          doctest::Approx(2.5f));
    resolver.ClearTransientOverride(guid);

    WriteJson(source, {{"schemaVersion", 1}, {"effects", {
        {{"type", "Bloom"}, {"intensity", 99.0}}}}});
    CHECK_FALSE(database.TryReimport(guid));
    auto lastGood = resolver.Resolve(guid, database);
    REQUIRE(lastGood);
    CHECK(lastGood.status == PostProcessProfileResolveStatus::LastGood);
    CHECK(std::get<BloomSettings2D>(lastGood.profile->effects[0].settings).intensity ==
          doctest::Approx(1.5f));

    database.OnSourceRemoved("camera.postfx");
    CHECK_FALSE(resolver.Resolve(guid, database));
    fs::remove_all(root, ec);
    resolver.ClearForTesting();
}

TEST_CASE("Camera post-process fields serialize and use a typed AssetGuid descriptor") {
    Camera camera;
    CHECK_FALSE(camera.IsPostProcessEnabled());
    CHECK(camera.GetPostProcessProfileGuid().empty());
    camera.SetPostProcessEnabled(true);
    camera.SetPostProcessProfileGuid("abcdef");
    nlohmann::json snapshot;
    camera.Serialize(snapshot);
    CHECK(snapshot["postProcessEnabled"] == true);
    CHECK(snapshot["postProcessProfileGuid"] == "abcdef");

    Camera copy;
    copy.Deserialize(snapshot);
    CHECK(copy.IsPostProcessEnabled());
    CHECK(copy.GetPostProcessProfileGuid() == "abcdef");

    Camera legacy;
    legacy.Deserialize({{"orthoSize", 100.0}});
    CHECK_FALSE(legacy.IsPostProcessEnabled());
    CHECK(legacy.GetPostProcessProfileGuid().empty());

    const auto descriptors = DescribeEditorProperties(camera);
    const auto found = std::find_if(descriptors.begin(), descriptors.end(),
        [](const auto& descriptor) {
            return descriptor.key == "postProcessProfileGuid";
        });
    REQUIRE(found != descriptors.end());
    CHECK(found->type == EditorPropertyType::AssetGuid);
    CHECK(found->assetType == "PostProcessProfileImporter");
}

TEST_CASE("Dependency validation enforces post-process profile references") {
    const fs::path root =
        fs::temp_directory_path() / "molga_postfx_dependency_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets");
    const std::string profileGuid = "11111111111111111111111111111111";
    const std::string textureGuid = "22222222222222222222222222222222";
    const std::string failedGuid = "33333333333333333333333333333333";
    const nlohmann::json catalogDocument = {
        {"schemaVersion", 2},
        {"records", nlohmann::json::array({
            {{"guid", profileGuid}, {"sourcePath", "Assets/valid.postfx"},
             {"importer", "PostProcessProfileImporter"},
             {"importerVersion", 1}, {"importFailed", false}},
            {{"guid", textureGuid}, {"sourcePath", "Assets/wrong.png"},
             {"importer", "TextureImporter"}, {"importerVersion", 2},
             {"importFailed", false}},
            {{"guid", failedGuid}, {"sourcePath", "Assets/broken.postfx"},
             {"importer", "PostProcessProfileImporter"},
             {"importerVersion", 1}, {"importFailed", true},
             {"importError", "invalid profile"}}
        })}
    };
    const fs::path catalog = root / "catalog.json";
    WriteJson(catalog, catalogDocument);
    AssetDatabase database;
    REQUIRE(database.LoadCatalog(catalog, root));

    const auto validate = [&](const std::string& guid) {
        const fs::path scene = root / (guid + ".scene");
        WriteJson(scene, {
            {"type", "Camera"}, {"postProcessEnabled", true},
            {"postProcessProfileGuid", guid}});
        return AssetDependencyValidator::ValidateScenes({scene}, database);
    };
    CHECK(validate(profileGuid).Ok());

    const auto missing = validate("44444444444444444444444444444444");
    REQUIRE(missing.issues.size() == 1);
    CHECK(missing.issues[0].code == DependencyIssueCode::Missing);
    CHECK(missing.issues[0].expectedImporter ==
          "PostProcessProfileImporter");

    const auto mismatch = validate(textureGuid);
    REQUIRE(mismatch.issues.size() == 1);
    CHECK(mismatch.issues[0].code == DependencyIssueCode::TypeMismatch);
    CHECK(mismatch.issues[0].expectedImporter ==
          "PostProcessProfileImporter");

    const auto failed = validate(failedGuid);
    REQUIRE(failed.issues.size() == 1);
    CHECK(failed.issues[0].code == DependencyIssueCode::ImportFailed);
    fs::remove_all(root, ec);
}
