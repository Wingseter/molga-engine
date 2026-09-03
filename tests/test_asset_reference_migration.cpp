#include "ECS/Components/SpriteRenderer.h"
#include "AssetDatabaseTestAuthority.h"
#include "Core/AssetDatabase.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;

// Every case scans its own subtree of the one process-lifetime project root,
// so the singleton keeps a single artifact authority and no case sees another
// case's records.
static fs::path SeedAssets(const char* caseName) {
    auto& authority = test_support::AssetDatabaseTestAuthority::Get();
    std::string bindError;
    REQUIRE_MESSAGE(authority.Bind(molga::AssetDatabase::Get(), &bindError),
                    bindError);
    const fs::path root = authority.AssetsCaseRoot(caseName);
    { std::ofstream(root / "hero.png") << "img"; }
    molga::AssetDatabase::Get().ScanProject(root);
    return root;
}

TEST_CASE("loading a legacy scene with texturePath migrates to textureGuid in memory") {
    fs::path assets = SeedAssets("loading a legacy scene with texturePath ");
    std::string guid = molga::AssetDatabase::Get().GuidForSource("hero.png");
    REQUIRE(guid.size() == 32);

    nlohmann::json legacy;            // guid 없이 path만 있는 구버전 데이터
    legacy["texturePath"] = "hero.png";

    SpriteRenderer sr;
    sr.Deserialize(legacy);
    CHECK(sr.GetTextureGuid() == guid);   // path가 guid로 승격되었다

    nlohmann::json out;
    sr.Serialize(out);
    CHECK(out.value("textureGuid", std::string()) == guid);  // 저장은 guid로

    fs::remove_all(assets);
}

TEST_CASE("loading a scene that already has textureGuid keeps it") {
    fs::path assets = SeedAssets("loading a scene that already has texture");
    std::string guid = molga::AssetDatabase::Get().GuidForSource("hero.png");
    nlohmann::json modern;
    modern["textureGuid"] = guid;

    SpriteRenderer sr;
    sr.Deserialize(modern);
    CHECK(sr.GetTextureGuid() == guid);
    fs::remove_all(assets);
}
