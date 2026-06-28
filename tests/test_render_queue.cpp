#include "Rendering/RenderQueue.h"
#include "doctest.h"

using namespace molga;

TEST_CASE("RenderQueue sort is deterministic and stable") {
    RenderQueue queue;

    RenderCommand c1;
    c1.sortKey.sortingOrder = 10;
    c1.isBatchableSprite = true;

    RenderCommand c2;
    c2.sortKey.sortingOrder = 5;
    c2.isBatchableSprite = true;

    RenderCommand c3;
    c3.sortKey.sortingOrder = 10;
    c3.isBatchableSprite = true;

    // Submit in order: c1 (order 10), c2 (order 5), c3 (order 10)
    queue.Submit(c1);
    queue.Submit(c2);
    queue.Submit(c3);

    queue.Sort();

    const auto& commands = queue.GetCommands();
    REQUIRE(commands.size() == 3);

    // After sort:
    // First should be c2 (order 5)
    // Second should be c1 (order 10, first submitted)
    // Third should be c3 (order 10, second submitted)
    CHECK(commands[0].sortKey.sortingOrder == 5);
    CHECK(commands[1].sortKey.sortingOrder == 10);
    CHECK(commands[2].sortKey.sortingOrder == 10);

    CHECK(commands[1].sortKey.submissionIndex < commands[2].sortKey.submissionIndex);
}

TEST_CASE("BatchKey equivalence and grouping compatibility") {
    // Check compatibility comparison
    BatchKey k1;
    k1.shader = reinterpret_cast<Shader*>(0x1);
    k1.texture = reinterpret_cast<Texture*>(0x2);
    k1.blendMode = BlendMode::Alpha;
    k1.isBatchable = true;

    BatchKey k2;
    k2.shader = reinterpret_cast<Shader*>(0x1);
    k2.texture = reinterpret_cast<Texture*>(0x2);
    k2.blendMode = BlendMode::Alpha;
    k2.isBatchable = true;

    CHECK(k1 == k2);

    BatchKey k3;
    k3.shader = reinterpret_cast<Shader*>(0x3); // different shader
    k3.texture = reinterpret_cast<Texture*>(0x2);
    k3.blendMode = BlendMode::Alpha;
    k3.isBatchable = true;

    CHECK(k1 != k3);
}
