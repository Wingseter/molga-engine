#include "doctest.h"

#include "Core/PathService.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "SmokeTestSupport.h"

#include "Common/Log.h"
#include "Common/SmokeReportSink.h"
#include <fstream>
#include <iterator>

TEST_CASE("runtime world loads a packaged scene with project relative assets") {
    // Force linker to include component translation units
    {
        Transform t;
        t.SetPosition(0.0f, 0.0f);
        SpriteRenderer s;
        s.RenderSprite(nullptr);
    }

    test_support::TempDirectory temp{"runtime-smoke"};
    const auto root = temp.Path();
    const auto reportPath = root / "smoke_report.txt";
    auto reportSink = std::make_shared<Log::SmokeReportSink>(reportPath.string());
    Log::AddSink(reportSink);

    const auto scenePath = root / "Scenes/main.json";

    test_support::WriteText(root / "Assets/Textures/smoke.ppm",
                            "P6\n1 1\n255\n@ `");
    test_support::WriteText(
        scenePath,
        R"({
  "version": "1.0",
  "name": "Smoke Scene",
  "gameObjects": [{
    "name": "SmokeSprite",
    "id": 1001,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "Transform",
      "enabled": true,
      "position": [32.0, 48.0],
      "rotation": 0.0,
      "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer",
      "enabled": true,
      "texturePath": "Assets/Textures/smoke.ppm",
      "color": [1.0, 1.0, 1.0, 1.0],
      "size": [1.0, 1.0],
      "flipX": false,
      "flipY": false,
      "sortingOrder": 0
    }]
  }]
})");

    PathService::Get().SetAssetRoot(root);

    World world;
    REQUIRE(world.LoadFromFile(scenePath.string()));
    REQUIRE(world.Objects().size() == 1);

    auto* sprite = world.Objects().front()->GetComponent<SpriteRenderer>();
    REQUIRE(sprite != nullptr);
    CHECK(sprite->GetTexturePath() == "Assets/Textures/smoke.ppm");
    CHECK(PathService::Get().ResolveAsset(sprite->GetTexturePath()) ==
          (root / "Assets/Textures/smoke.ppm").string());

    world.StartPending();
    world.FixedStep(1.0F / 60.0F);
    world.Update(1.0F / 60.0F);
    world.LateUpdate(1.0F / 60.0F);

    Log::Error("SmokeTest", "simulated runtime asset missing");

    Log::RemoveSink(reportSink);
    reportSink->Flush();

    std::ifstream in(reportPath);
    std::string contents((std::istreambuf_iterator<char>(in)), {});
    CHECK(contents.find("simulated runtime asset missing") != std::string::npos);
    CHECK(contents.find("SmokeTest") != std::string::npos);
}
