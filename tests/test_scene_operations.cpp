#include "Editor/SceneOperations.h"
#include "Core/SceneSerializer.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "SmokeTestSupport.h"
#include "doctest.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("SceneOperations SaveSceneAsPath saves exact path and updates state") {
    test_support::TempDirectory temp{"scene-operations-save-path"};
    const fs::path scenePath = temp.Path() / "Scenes" / "level_01.json";

    std::vector<std::shared_ptr<GameObject>> objects;
    auto player = std::make_shared<GameObject>("Player");
    player->AddComponent<Transform>(32.0f, 48.0f);
    objects.push_back(player);

    SceneOperations operations;
    operations.MarkModified();

    REQUIRE(operations.SaveSceneAsPath(objects, scenePath.string()));

    CHECK(operations.GetCurrentPath() == scenePath.string());
    CHECK_FALSE(operations.IsModified());
    CHECK(fs::exists(scenePath));

    std::vector<std::shared_ptr<GameObject>> loaded;
    REQUIRE(SceneSerializer::LoadScene(scenePath.string(), loaded));
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0]->GetName() == "Player");
}

TEST_CASE("SceneOperations OpenScenePath loads exact path and updates state") {
    test_support::TempDirectory temp{"scene-operations-open-path"};
    const fs::path scenePath = temp.Path() / "Scenes" / "boss.json";

    std::vector<std::shared_ptr<GameObject>> source;
    source.push_back(std::make_shared<GameObject>("Boss"));
    fs::create_directories(scenePath.parent_path());
    REQUIRE(SceneSerializer::SaveScene(scenePath.string(), source));

    std::vector<std::shared_ptr<GameObject>> loaded;
    SceneOperations operations;
    operations.MarkModified();

    REQUIRE(operations.OpenScenePath(loaded, scenePath.string()));

    CHECK(operations.GetCurrentPath() == scenePath.string());
    CHECK_FALSE(operations.IsModified());
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0]->GetName() == "Boss");
}
