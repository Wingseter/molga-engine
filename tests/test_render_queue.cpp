#include "Rendering/RenderQueue.h"
#include "Rendering/WorldSort2D.h"
#include "Core/ProjectSettings.h"
#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "doctest.h"

#include <limits>
#include <memory>

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

TEST_CASE("SortKey priority is camera pass then layer order Y and submission") {
    SortKey base;
    base.cameraPass = 2;
    base.sortingLayer = 3;
    base.sortingOrder = -5;
    base.depthOrYSort = 10.0f;
    base.submissionIndex = 9;

    SortKey camera = base;
    camera.cameraPass = 1;
    CHECK(camera < base);
    SortKey layer = base;
    layer.sortingLayer = 2;
    CHECK(layer < base);
    SortKey order = base;
    order.sortingOrder = -6;
    CHECK(order < base);
    SortKey y = base;
    y.depthOrYSort = 9.0f;
    CHECK(y < base);
    SortKey submission = base;
    submission.submissionIndex = 8;
    CHECK(submission < base);
}

TEST_CASE("RenderQueue normalizes non-finite direct depth keys") {
    RenderQueue queue;
    RenderCommand nan;
    nan.sortKey.depthOrYSort = std::numeric_limits<float>::quiet_NaN();
    RenderCommand negative;
    negative.sortKey.depthOrYSort = -1.0f;
    RenderCommand zero;
    zero.sortKey.depthOrYSort = 0.0f;
    queue.Submit(nan);
    queue.Submit(negative);
    queue.Submit(zero);

    CHECK(nan.sortKey == zero.sortKey); // equality also uses normalized depth
    queue.Sort();
    REQUIRE(queue.GetCommands().size() == 3U);
    CHECK(queue.GetCommands()[0].sortKey.depthOrYSort == -1.0f);
    CHECK(std::isnan(queue.GetCommands()[1].sortKey.depthOrYSort));
    CHECK(queue.GetCommands()[1].sortKey.submissionIndex <
          queue.GetCommands()[2].sortKey.submissionIndex);
}

TEST_CASE("WorldSort2D resolves current layer order Y and missing fallback once") {
    ProjectSettings& settings = ProjectSettings::Get();
    settings.SetDefaults();
    settings.sortingLayers = {"Background", "Default", "Foreground"};

    WorldSortSettings2D authored;
    authored.sortingLayer = "Foreground";
    authored.sortingOrder = -7;
    authored.sortMode = SortMode2D::YAxis;
    authored.ySortOffset = 2.5f;
    SortKey key = MakeWorldSortKey(authored, 12.0f);
    CHECK(key.sortingLayer == 2);
    CHECK(key.sortingOrder == -7);
    CHECK(key.depthOrYSort == doctest::Approx(14.5f));

    settings.sortingLayers = {"Foreground", "Background", "Default"};
    key = MakeWorldSortKey(authored, 12.0f);
    CHECK(key.sortingLayer == 0); // component stores a name, not a stale index

    Log::ClearSinks();
    auto sink = std::make_shared<Log::RingBufferSink>(8);
    Log::AddSink(sink);
    authored.sortingLayer = "Deleted";
    CHECK(MakeWorldSortKey(authored, 0.0f).sortingLayer == 2);
    CHECK(MakeWorldSortKey(authored, 5.0f).sortingLayer == 2);
    CHECK(sink->Snapshot().size() == 1U);
    Log::ClearSinks();
    settings.SetDefaults();
}

TEST_CASE("WorldSort2D normalizes malformed authored Y without changing fixed mode") {
    ProjectSettings::Get().SetDefaults();
    WorldSortSettings2D settings;
    settings.sortMode = SortMode2D::YAxis;
    settings.ySortOffset = std::numeric_limits<float>::infinity();
    CHECK(MakeWorldSortKey(settings,
                           std::numeric_limits<float>::quiet_NaN()).depthOrYSort == 0.0f);
    settings.sortMode = SortMode2D::Fixed;
    settings.ySortOffset = 100.0f;
    CHECK(MakeWorldSortKey(settings, 100.0f).depthOrYSort == 0.0f);
}
