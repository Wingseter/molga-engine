#include "Editor/Commands/CreateSpriteFromAssetCommand.h"
#include "ECS/GameObject.h"
#include "ECS/Components/SpriteRenderer.h"
#include "doctest.h"
#include <vector>
#include <memory>

using molga::CreateSpriteFromAssetCommand;

TEST_CASE("command adds a GameObject with a SpriteRenderer bound to the guid") {
    std::vector<std::shared_ptr<GameObject>> objects;
    CreateSpriteFromAssetCommand cmd(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Hero", {100.0f, 50.0f}, &objects);
    cmd.Execute();
    REQUIRE(objects.size() == 1);
    auto* sr = objects[0]->GetComponent<SpriteRenderer>();
    REQUIRE(sr != nullptr);
    CHECK(sr->GetTextureGuid() == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    cmd.Undo();
    CHECK(objects.empty());          // undo는 생성된 오브젝트를 제거
}
