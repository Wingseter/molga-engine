#include "Editor/AssetReferenceScan.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

TEST_CASE("scan finds scenes that reference a given texture guid") {
    fs::path root = fs::temp_directory_path() / "molga_ref_test";
    fs::remove_all(root);
    fs::create_directories(root / "Scenes");

    nlohmann::json scene;
    scene["gameObjects"] = nlohmann::json::array();
    nlohmann::json go; go["components"] = nlohmann::json::array();
    nlohmann::json comp; comp["type"] = "SpriteRenderer";
    comp["textureGuid"] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    go["components"].push_back(comp);
    scene["gameObjects"].push_back(go);
    { std::ofstream(root / "Scenes" / "main.json") << scene.dump(); }

    auto refs = molga::AssetReferenceScan::FindReferencers(
        root, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(refs.size() == 1);

    auto none = molga::AssetReferenceScan::FindReferencers(
        root, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    CHECK(none.empty());

    fs::remove_all(root);
}
