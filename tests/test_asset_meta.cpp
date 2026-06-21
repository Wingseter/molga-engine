#include "Core/AssetMeta.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

using molga::AssetMeta;
namespace fs = std::filesystem;

TEST_CASE("CreateOrLoad writes a .meta with a fresh guid and reloads it") {
    fs::path dir = fs::temp_directory_path() / "molga_meta_test";
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
