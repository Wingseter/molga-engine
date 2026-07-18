#include "Core/AssetMeta.h"
#include "Core/PersistentStorage.h"
#include "Core/TextureImportSettings.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

using molga::AssetMeta;
namespace fs = std::filesystem;

TEST_CASE("CreateOrLoad writes a .meta with a fresh guid and reloads it") {
    fs::path dir = fs::temp_directory_path() / "molga_meta_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::path asset = dir / "hero.png";
    { std::ofstream(asset) << "fake"; }
    fs::path metaPath = AssetMeta::MetaPathFor(asset);
    fs::remove(metaPath);

    AssetMeta m = AssetMeta::CreateOrLoad(asset, "TextureImporter", 1);
    CHECK(m.guid.size() == 32);
    CHECK(m.importer == "TextureImporter");
    CHECK(m.importerVersion == 1);
    CHECK(fs::exists(metaPath));

    AssetMeta again = AssetMeta::CreateOrLoad(asset, "TextureImporter", 1);
    CHECK(again.guid == m.guid); // guid는 안정적이어야 한다

    fs::remove_all(dir);
}

TEST_CASE("MetaPathFor appends .meta") {
    CHECK(AssetMeta::MetaPathFor("a/b/hero.png").string()
          == fs::path("a/b/hero.png.meta").string());
}

TEST_CASE("AssetMeta preserves unknown root and texture setting keys") {
    const fs::path dir = fs::temp_directory_path() / "molga_meta_preserve_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path asset = dir / "hero.png";
    { std::ofstream(asset) << "fake"; }

    const std::string guid = "11111111111111111111111111111111";
    const nlohmann::json original = {
        {"guid", guid},
        {"importer", "TextureImporter"},
        {"importerVersion", 2},
        {"pluginData", {{"keep", true}, {"revision", 7}}},
        {"settings", {
            {"filter", "Linear"},
            {"pixelsPerUnit", 2.0},
            {"vendorCompressionHint", "future-value"}
        }}
    };
    { std::ofstream(AssetMeta::MetaPathFor(asset)) << original.dump(2); }

    AssetMeta meta = AssetMeta::CreateOrLoad(asset, "TextureImporter", 2);
    auto settings = molga::DeserializeTextureImportSettings(meta.settings, true);
    settings.filter = molga::TextureFilterMode::Nearest;
    meta.settings = molga::SerializeTextureImportSettings(settings);
    REQUIRE(AssetMeta::Write(asset, meta));

    nlohmann::json written;
    { std::ifstream file(AssetMeta::MetaPathFor(asset)); file >> written; }
    CHECK(written["guid"] == guid);
    CHECK(written["pluginData"] == original["pluginData"]);
    CHECK(written["settings"]["vendorCompressionHint"] == "future-value");
    CHECK(written["settings"]["filter"] == "Nearest");
    CHECK(written["settings"]["colorSpace"] == "LegacyLinear");

    fs::remove_all(dir);
}

TEST_CASE("AssetMeta recovers malformed sidecars without throwing") {
    const fs::path dir = fs::temp_directory_path() / "molga_meta_malformed_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path asset = dir / "hero.png";
    { std::ofstream(asset) << "fake"; }
    { std::ofstream(AssetMeta::MetaPathFor(asset)) << "{ not valid json"; }

    AssetMeta recovered;
    CHECK_NOTHROW(recovered = AssetMeta::CreateOrLoad(asset, "TextureImporter", 2));
    CHECK(molga::Guid::IsValid(recovered.guid));
    CHECK(recovered.importer == "TextureImporter");
    CHECK(recovered.importerVersion == 2);

    nlohmann::json written;
    { std::ifstream file(AssetMeta::MetaPathFor(asset)); file >> written; }
    CHECK(written.is_object());
    CHECK(written["guid"] == recovered.guid);

    fs::remove_all(dir);
}

TEST_CASE("AssetMeta atomic failure leaves the last sidecar intact") {
    const fs::path dir = fs::temp_directory_path() / "molga_meta_atomic_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path asset = dir / "hero.png";
    { std::ofstream(asset) << "fake"; }
    AssetMeta meta = AssetMeta::CreateOrLoad(asset, "TextureImporter", 2);

    std::ifstream beforeFile(AssetMeta::MetaPathFor(asset));
    const std::string before((std::istreambuf_iterator<char>(beforeFile)),
                             std::istreambuf_iterator<char>());
    meta.settings["filter"] = "Nearest";
    PersistentStorage::FailNextAtomicReplaceForTesting();
    CHECK_FALSE(AssetMeta::Write(asset, meta));

    std::ifstream afterFile(AssetMeta::MetaPathFor(asset));
    const std::string after((std::istreambuf_iterator<char>(afterFile)),
                            std::istreambuf_iterator<char>());
    CHECK(after == before);

    fs::remove_all(dir);
}

TEST_CASE("Texture settings tolerate malformed values and preserve unknown fields") {
    const nlohmann::json input = {
        {"filter", 42},
        {"mipmaps", "not-a-bool"},
        {"pixelsPerUnit", -5.0},
        {"defaultPivot", {"bad", 2.0}},
        {"futureSamplerOption", {{"quality", "high"}}},
        {"slices", nlohmann::json::array({{
            {"id", 17}, {"name", false}, {"rect", {0, 0, 16, 16}},
            {"pivot", {std::numeric_limits<double>::infinity(), -1.0}},
            {"futureSliceFlag", "keep"}
        }})}
    };

    molga::TextureImportSettings settings;
    CHECK_NOTHROW(settings = molga::DeserializeTextureImportSettings(input, true));
    CHECK(settings.filter == molga::TextureFilterMode::Linear);
    CHECK(settings.mipmaps == false);
    CHECK(settings.pixelsPerUnit == doctest::Approx(1.0f));
    CHECK(settings.defaultPivot.x == doctest::Approx(0.5f));
    CHECK(settings.defaultPivot.y == doctest::Approx(1.0f));
    REQUIRE(settings.slices.size() == 1);
    CHECK(molga::Guid::IsValid(settings.slices[0].id));
    CHECK(settings.slices[0].pivot.x == doctest::Approx(0.5f));
    CHECK(settings.slices[0].pivot.y == doctest::Approx(0.0f));

    const nlohmann::json output = molga::SerializeTextureImportSettings(settings);
    CHECK(output["futureSamplerOption"] == input["futureSamplerOption"]);
    CHECK(output["slices"][0]["futureSliceFlag"] == "keep");
}

TEST_CASE("Grid re-slicing keeps IDs names and pivots for unchanged pixel rectangles") {
    auto first = molga::BuildGridSlices(64, 32, 16, 16, {});
    REQUIRE(first.size() == 8);
    for (const auto& slice : first) CHECK(molga::Guid::IsValid(slice.id));
    first[3].name = "run_3";
    first[3].pivot = {0.25f, 0.75f};

    const auto resized = molga::BuildGridSlices(80, 32, 16, 16, first);
    REQUIRE(resized.size() == 10);
    const auto preserved = std::find_if(resized.begin(), resized.end(), [](const auto& slice) {
        return slice.pixelRect.x == 48 && slice.pixelRect.y == 0;
    });
    REQUIRE(preserved != resized.end());
    CHECK(preserved->id == first[3].id);
    CHECK(preserved->name == "run_3");
    CHECK(preserved->pivot.x == doctest::Approx(0.25f));
    CHECK(preserved->pivot.y == doctest::Approx(0.75f));

    const auto differentGrid = molga::BuildGridSlices(64, 32, 32, 16, first);
    REQUIRE(differentGrid.size() == 4);
    for (const auto& slice : differentGrid) {
        CHECK(std::none_of(first.begin(), first.end(), [&](const auto& old) {
            return old.id == slice.id;
        }));
    }
    CHECK(molga::BuildGridSlices(64, 32, 0, 16, first).empty());
}
