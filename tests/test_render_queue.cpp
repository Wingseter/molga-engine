#include "Rendering/RenderQueue.h"
#include "Rendering/WorldSort2D.h"
#include "Core/ProjectSettings.h"
#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "doctest.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace molga;

namespace {

// ── Task 6.3: 제출 직전의 붙듦을 GPU 없이 관찰한다 ───────────────────────────
// 명령 하나가 가리키는 atlas page의 지분은 "그 명령이 실제로 그려질 때"만
// 프레임으로 넘어가야 한다. 그 "직전"은 순서 문제이지 값 문제라서, 붙듦과
// 그리기가 같은 로그에 순서대로 남는 것 말고는 관찰할 방법이 없다.
//
// 그래서 RenderSystem2D의 제출 루프는 RenderQueue.h의 SubmitVisibleCommands
// 하나뿐이고, 여기서는 진짜 Renderer/SpriteBatcher 대신 호출을 받아 적는
// 대역으로 그 같은 코드를 인스턴스화한다. 루프를 이 파일에 베껴 쓰면 재는
// 대상이 프로덕션이 아니라 이 픽스처가 된다.

struct RecordingStats {
    std::uint64_t submittedCommands = 0;
};

struct RecordingRenderer {
    explicit RecordingRenderer(std::vector<std::string>& log) : log_(&log) {}

    RecordingStats& Stats() noexcept { return stats; }

    // Task 8.2: 진짜 Renderer와 같은 질문을 받는다. RetainUntilFrameComplete가
    // 활성 프레임 없이 불리면 std::logic_error를 던지므로, 그 조건을 걸러 내는
    // 것은 제출 루프의 몫이고 그 판단은 이 함수 하나로만 관찰된다.
    bool HasFrame() const noexcept { return hasFrame; }

    void RetainUntilFrameComplete(std::uint64_t pageIdentity,
                                  std::shared_ptr<const void> pageLifetime) {
        log_->push_back("retain");
        retained.emplace_back(pageIdentity, std::move(pageLifetime));
    }

    const std::vector<std::string>& CallOrder() const noexcept { return *log_; }

    RecordingStats stats;
    bool hasFrame = true;
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const void>>> retained;

private:
    std::vector<std::string>* log_ = nullptr;
};

struct RecordingBatcher {
    explicit RecordingBatcher(std::vector<std::string>& log) : log_(&log) {}

    void DrawSprite(const std::array<Vertex2D, 4>&, const BatchKey& key) {
        Record(key);
    }
    void DrawGeometry(const std::vector<Vertex2D>&, const BatchKey& key) {
        Record(key);
    }
    void DrawIndexedGeometry(const std::vector<Vertex2D>&,
                             const std::vector<std::uint32_t>&,
                             const BatchKey& key) {
        Record(key);
    }
    void Flush() { log_->push_back("flush"); }

    std::vector<BatchKey> drawnKeys;

private:
    void Record(const BatchKey& key) {
        log_->push_back("draw");
        drawnKeys.push_back(key);
    }

    std::vector<std::string>* log_ = nullptr;
};

struct RenderQueueFixture {
    RenderQueueFixture() : renderer(log), batcher(log) {}

    // 텍스트 명령 하나. batchKey는 한 atlas 텍스처를 가리키는 보통의 배치
    // 가능한 sprite이고, page 지분만 따로 실려 있다.
    void EnqueueTextCommand(std::uint64_t pageIdentity,
                            std::shared_ptr<const void> pageLifetime) {
        RenderCommand command = TextCommand(pageIdentity,
                                            std::move(pageLifetime));
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    // 카메라 밖에 놓인 같은 모양의 명령. 컬링되는 쪽과 그려지는 쪽이 같은
    // 픽스처에서 나와야 "컬링된 명령은 토큰을 넘기지 않는다"가 두 방향으로
    // 관찰된다.
    void EnqueueCulledTextCommand(std::uint64_t pageIdentity,
                                  std::shared_ptr<const void> pageLifetime) {
        RenderCommand command = TextCommand(pageIdentity,
                                            std::move(pageLifetime));
        command.worldBounds = AABB(1000.0f, 1000.0f, 1.0f, 1.0f);
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    // 공유 geometry를 타는 같은 명령. 제출 루프에는 draw가 세 갈래로 나 있고,
    // sprite 갈래 하나만 관찰하면 나머지 두 갈래에서 붙듦이 draw 뒤로 밀려도
    // 아무 단언도 움직이지 않는다 — "제출 직전"이라는 이름이 함수의 3분의 1
    // 에서만 참인 상태가 된다. 월드 공간 텍스트가 DrawGeometry로 나가는 것은
    // Task 8.2의 그럴듯한 모양이므로, 세 갈래를 지금 같은 저울에 올린다.
    void EnqueueTextGeometryCommand(std::uint64_t pageIdentity,
                                    std::shared_ptr<const void> pageLifetime,
                                    bool batchable) {
        RenderCommand command = TextCommand(pageIdentity,
                                            std::move(pageLifetime));
        command.isBatchableSprite = false;
        command.batchKey.isBatchable = batchable;
        command.geometry = SharedGeometry();
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    void EnqueueTextIndexedGeometryCommand(
        std::uint64_t pageIdentity, std::shared_ptr<const void> pageLifetime) {
        RenderCommand command = TextCommand(pageIdentity,
                                            std::move(pageLifetime));
        command.isBatchableSprite = false;
        command.geometry = SharedGeometry();
        command.geometryIndices = SharedIndices();
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    // 배치되지 않는 sprite. 이 갈래에서만 앞뒤 Flush가 로그에 남으므로,
    // 붙듦이 앞 Flush보다 위로 올라가 *직전* batch의 제출에 매달리는 회귀는
    // 여기서만 보인다.
    void EnqueueNonBatchableTextCommand(
        std::uint64_t pageIdentity, std::shared_ptr<const void> pageLifetime) {
        RenderCommand command = TextCommand(pageIdentity,
                                            std::move(pageLifetime));
        command.batchKey.isBatchable = false;
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    void SetCameraBounds(const AABB& bounds) { cameraBounds = bounds; }

    void Render() {
        queue.Sort();
        SubmitVisibleCommands(queue.GetCommands(), cameraBounds, renderer,
                              batcher);
    }

    // 두 접근자 모두 먼저 존재를 못 박는다. 비어 있는 벡터의 front()를 비교에
    // 넣으면 회귀가 실패가 아니라 정의되지 않은 동작이 된다.
    BatchKey BatchKeyBefore() const {
        REQUIRE_FALSE(enqueuedKeys.empty());
        return enqueuedKeys.front();
    }
    BatchKey BatchKeyAfter() const {
        REQUIRE_FALSE(batcher.drawnKeys.empty());
        return batcher.drawnKeys.front();
    }

    std::vector<std::string> log;
    RenderQueue queue;
    RecordingRenderer renderer;
    RecordingBatcher batcher;
    std::vector<BatchKey> enqueuedKeys;
    std::optional<AABB> cameraBounds;

private:
    static RenderCommand TextCommand(std::uint64_t pageIdentity,
                                     std::shared_ptr<const void> pageLifetime) {
        RenderCommand command;
        command.isBatchableSprite = true;
        command.batchKey.shaderName = "batch";
        command.batchKey.textureStableId = 77U;
        command.batchKey.isBatchable = true;
        command.resourceLifetimeIdentity = pageIdentity;
        command.resourceLifetime = std::move(pageLifetime);
        return command;
    }

    static std::shared_ptr<const std::vector<Vertex2D>> SharedGeometry() {
        return std::make_shared<const std::vector<Vertex2D>>(
            std::vector<Vertex2D>(4));
    }

    static std::shared_ptr<const std::vector<std::uint32_t>> SharedIndices() {
        return std::make_shared<const std::vector<std::uint32_t>>(
            std::vector<std::uint32_t>{0U, 1U, 2U});
    }
};

} // namespace

TEST_CASE("resource lifetime is retained immediately before actual draw") {
    RenderQueueFixture f;
    auto token = std::make_shared<int>(1);
    f.EnqueueTextCommand(17, token);
    f.Render();
    CHECK(f.renderer.CallOrder() ==
          std::vector<std::string>{"retain", "draw"});
    CHECK(f.BatchKeyBefore() == f.BatchKeyAfter());
}

// 위 케이스가 재는 것은 순서뿐이다. 무엇을 붙들었는지가 없으면 정체성과
// 토큰을 뒤바꾸거나 둘 다 버리는 구현도 같은 로그를 남긴다.
TEST_CASE("a submitted text command hands over its exact page identity and token") {
    RenderQueueFixture f;
    auto token = std::make_shared<int>(1);
    f.EnqueueTextCommand(17, token);
    f.Render();
    REQUIRE(f.renderer.retained.size() == 1U);
    CHECK(f.renderer.retained.front().first == 17U);
    CHECK(f.renderer.retained.front().second == token);
}

TEST_CASE("a culled command transfers no page token") {
    RenderQueueFixture f;
    f.SetCameraBounds(AABB(-10.0f, -10.0f, 20.0f, 20.0f));
    auto drawn = std::make_shared<int>(1);
    auto culled = std::make_shared<int>(2);
    f.EnqueueTextCommand(17, drawn);
    f.EnqueueCulledTextCommand(18, culled);
    f.Render();
    // 두 방향을 함께 못 박는다. 그려진 쪽이 붙들리고 컬링된 쪽이 붙들리지
    // 않아야 하며, 한쪽만 보면 "아무것도 붙들지 않는" 구현도 통과한다.
    REQUIRE(f.renderer.retained.size() == 1U);
    CHECK(f.renderer.retained.front().first == 17U);
    CHECK(f.renderer.CallOrder() ==
          std::vector<std::string>{"retain", "draw"});
    CHECK(f.renderer.stats.submittedCommands == 1U);
}

// 위 케이스가 붙드는 것은 sprite 갈래 하나다. 아래 셋이 나머지 두 갈래와
// 비배치 경로를 같은 순서 계약 아래로 끌어들인다.
TEST_CASE("a shared-geometry text command retains immediately before its draw") {
    RenderQueueFixture f;
    auto token = std::make_shared<int>(1);
    f.EnqueueTextGeometryCommand(21, token, /*batchable=*/true);
    f.Render();
    CHECK(f.renderer.CallOrder() ==
          std::vector<std::string>{"retain", "draw"});
    REQUIRE(f.renderer.retained.size() == 1U);
    CHECK(f.renderer.retained.front().first == 21U);
    CHECK(f.renderer.retained.front().second == token);
}

TEST_CASE("an indexed-geometry text command retains immediately before its draw") {
    RenderQueueFixture f;
    auto token = std::make_shared<int>(1);
    f.EnqueueTextIndexedGeometryCommand(22, token);
    f.Render();
    CHECK(f.renderer.CallOrder() ==
          std::vector<std::string>{"retain", "draw"});
    REQUIRE(f.renderer.retained.size() == 1U);
    CHECK(f.renderer.retained.front().first == 22U);
    CHECK(f.renderer.retained.front().second == token);
}

// 비배치 경로의 계약은 flush → retain → draw → flush다. 앞 Flush보다 위에서
// 붙들면 이 명령의 지분이 *앞* batch의 제출 순서에 매달리는데, 앞뒤 Flush가
// 로그에 없으면 그 이동이 관찰되지 않는다.
TEST_CASE("a nonbatchable text command retains between its leading flush and draw") {
    RenderQueueFixture f;
    auto sprite = std::make_shared<int>(1);
    f.EnqueueNonBatchableTextCommand(23, sprite);
    f.Render();
    CHECK(f.renderer.CallOrder() ==
          std::vector<std::string>{"flush", "retain", "draw", "flush"});
    REQUIRE(f.renderer.retained.size() == 1U);
    CHECK(f.renderer.retained.front().first == 23U);

    RenderQueueFixture g;
    auto geometry = std::make_shared<int>(2);
    g.EnqueueTextGeometryCommand(24, geometry, /*batchable=*/false);
    g.Render();
    CHECK(g.renderer.CallOrder() ==
          std::vector<std::string>{"flush", "retain", "draw", "flush"});
    REQUIRE(g.renderer.retained.size() == 1U);
    CHECK(g.renderer.retained.front().first == 24U);
}

TEST_CASE("a command without a page token draws without retaining") {
    RenderQueueFixture f;
    f.EnqueueTextCommand(0, nullptr);
    f.Render();
    CHECK(f.renderer.retained.empty());
    CHECK(f.renderer.CallOrder() == std::vector<std::string>{"draw"});
}

// ── Task 8.2: Renderer::RetainUntilFrameComplete가 던지는 두 조건 ───────────
// 그 함수는 정체성이 0이거나 활성 프레임이 없으면 std::logic_error를 던진다.
// 이 호출은 SpriteBatcher::Begin/End 사이에 있으므로, 던지면 batcher가 열린
// 채로 예외가 RenderSystem2D::Render 밖으로 나간다 — 프레임 하나가 아니라
// batcher 상태가 깨진다. Task 8.2 전에는 resourceLifetime을 채우는 프로덕션
// 생산자가 없어 닿을 수 없었고, 텍스트 소비자를 옮긴 지금은 살아 있다.
//
// 양쪽을 다 요구한다. "붙들지 않는다"만 보면 붙듦을 통째로 지운 구현도
// 통과하므로, 같은 픽스처에서 정상 명령이 실제로 붙들리는 것을 함께 본다.
TEST_CASE("a token without a page identity draws without retaining") {
    RenderQueueFixture f;
    auto orphan = std::make_shared<int>(1);
    auto named = std::make_shared<int>(2);
    // 정체성 0 + 토큰 있음. atlas는 이 모양을 만들지 않지만, 받아 주면 이름
    // 없는 여러 자원이 가짜 page 0 하나로 뭉쳐 서로의 반납을 막는다.
    f.EnqueueTextCommand(0, orphan);
    f.EnqueueTextCommand(31, named);
    f.Render();
    REQUIRE(f.renderer.retained.size() == 1U);
    CHECK(f.renderer.retained.front().first == 31U);
    CHECK(f.renderer.retained.front().second == named);
}

TEST_CASE("a text command outside an active frame draws without retaining") {
    RenderQueueFixture f;
    auto token = std::make_shared<int>(1);
    f.EnqueueTextCommand(17, token);
    // 프레임 밖 Render는 실제로 있다: ParticleSystem/SpriteRenderer/
    // MarrowRenderer의 RenderSprite 경로와, 프레임을 얻지 못한 프레임.
    f.renderer.hasFrame = false;
    f.Render();
    CHECK(f.renderer.retained.empty());
    CHECK(f.renderer.CallOrder() == std::vector<std::string>{"draw"});

    // 같은 명령이 프레임 안에서는 붙들린다. 이 대조가 없으면 위 케이스는
    // 붙듦을 통째로 지운 구현과 구분되지 않는다.
    RenderQueueFixture inFrame;
    inFrame.EnqueueTextCommand(17, token);
    inFrame.Render();
    REQUIRE(inFrame.renderer.retained.size() == 1U);
    CHECK(inFrame.renderer.retained.front().first == 17U);
}

// 페이지 지분이 배치를 바꾸지 않는다는 것이 이 필드들을 BatchKey 밖에 둔
// 이유다. 서로 다른 page를 가리키는 두 명령이 같은 batch에 남아야 하고,
// 정렬도 흔들리지 않아야 한다.
TEST_CASE("page lifetime fields stay outside batch identity and sort order") {
    RenderCommand first;
    first.batchKey.shaderName = "batch";
    first.batchKey.textureStableId = 77U;
    first.sortKey.sortingOrder = 3;
    RenderCommand second = first;
    second.resourceLifetimeIdentity = 4242U;
    second.resourceLifetime = std::make_shared<int>(7);

    CHECK(first.batchKey == second.batchKey);
    CHECK_FALSE(first.batchKey < second.batchKey);
    CHECK_FALSE(second.batchKey < first.batchKey);
    CHECK(first.sortKey == second.sortKey);
    CHECK_FALSE(first.sortKey < second.sortKey);
    CHECK_FALSE(second.sortKey < first.sortKey);

    // 반대편: 배치 정체성에 실제로 속한 필드는 여전히 두 키를 갈라야 한다.
    // 이것이 없으면 operator==를 "언제나 같음"으로 굳혀도 위가 통과한다.
    second.batchKey.textureStableId = 78U;
    CHECK(first.batchKey != second.batchKey);
}

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
    // Batch grouping uses stable shader/texture identities rather than native
    // pointers or transient resource slots.
    BatchKey k1;
    k1.shaderName = "sprite";
    k1.shaderRevision = 7;
    k1.textureStableId = 42;
    k1.blendMode = BlendMode::Alpha;
    k1.isBatchable = true;

    BatchKey k2;
    k2.shaderName = "sprite";
    k2.shaderRevision = 7;
    k2.textureStableId = 42;
    k2.blendMode = BlendMode::Alpha;
    k2.isBatchable = true;

    CHECK(k1 == k2);

    BatchKey k3;
    k3.shaderName = "text";
    k3.shaderRevision = 7;
    k3.textureStableId = 42;
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
