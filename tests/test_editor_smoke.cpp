#include "doctest.h"

#include "Core/World.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/GameObject.h"
#include "Editor/SceneDocument.h"
#include "SmokeTestSupport.h"

#include <filesystem>

TEST_CASE("editor save play stop keeps edit world authoritative") {
    test_support::TempDirectory temp{"editor-smoke"};
    const auto scenePath = temp.Path() / "Scenes/main.json";
    std::filesystem::create_directories(scenePath.parent_path());

    SceneDocument document;
    auto spriteObject = std::make_shared<GameObject>("SmokeSprite");
    auto* sprite = spriteObject->AddComponent<SpriteRenderer>();
    sprite->SetTexturePath("Assets/Textures/smoke.ppm");
    document.EditWorld().Add(spriteObject);
    document.SetPath(scenePath.string());

    REQUIRE(document.EditWorld().SaveToFile(scenePath.string()));
    REQUIRE(document.EditWorld().Objects().size() == 1);

    document.EnterPlay();
    REQUIRE(document.IsPlaying());
    REQUIRE(document.ActiveWorld().Objects().size() == 1);

    document.ActiveWorld().Objects().front()->SetName("PlayOnlyName");
    document.ActiveWorld().Add(std::make_shared<GameObject>("PlayOnlyObject"));
    CHECK(document.ActiveWorld().Objects().size() == 2);

    document.ExitPlay();
    CHECK_FALSE(document.IsPlaying());
    REQUIRE(document.EditWorld().Objects().size() == 1);
    CHECK(document.EditWorld().Objects().front()->GetName() == "SmokeSprite");

    World reloaded;
    REQUIRE(reloaded.LoadFromFile(scenePath.string()));
    REQUIRE(reloaded.Objects().size() == 1);
    CHECK(reloaded.Objects().front()->GetName() == "SmokeSprite");
}

TEST_CASE("editor opens project main scene into edit world") {
    test_support::TempDirectory temp{"editor-open-smoke"};
    const auto scenePath = temp.Path() / "Scenes/main.json";

    std::filesystem::create_directories(scenePath.parent_path());
    World source;
    source.Add(std::make_shared<GameObject>("SmokeSprite"));
    REQUIRE(source.SaveToFile(scenePath.string()));

    SceneDocument opened;
    REQUIRE(opened.Open(scenePath.string()));
    CHECK(opened.Path() == scenePath.string());
    CHECK(opened.EditWorld().Name() == "main");
    REQUIRE(opened.EditWorld().Objects().size() == 1);
    CHECK(opened.EditWorld().Objects().front()->GetName() == "SmokeSprite");
}
