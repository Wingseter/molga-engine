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

    void RetainUntilFrameComplete(molga::ResourceLifetimeDomain domain,
                                  std::uint64_t pageIdentity,
                                  std::shared_ptr<const void> pageLifetime) {
        log_->push_back("retain");
        retainedDomains.push_back(domain);
        retained.emplace_back(pageIdentity, std::move(pageLifetime));
    }

    // ── Task 11.2: 진짜 Renderer의 클립 상태 호출 두 개 ──────────────────────
    // 로그에 값까지 남긴다. "set"만 남기면 A를 두 번 세우고 B를 건너뛴
    // 구현이 같은 로그를 낸다.
    bool FailThisScissorCall() {
        const std::size_t call = scissorCalls_++;
        return call >= failScissorCallIndex &&
               call < failScissorCallIndex + failScissorCallCount;
    }

    bool SetPassScissor(molga::PixelRectU32 scissor, std::string* errorOut) {
        if (FailThisScissorCall()) {
            if (errorOut) *errorOut = "injected scissor failure";
            return false;
        }
        log_->push_back("set " + RectText(scissor));
        appliedScissors.push_back(scissor);
        return true;
    }

    bool ResetPassScissor(std::string* errorOut) {
        if (FailThisScissorCall()) {
            if (errorOut) *errorOut = "injected scissor reset failure";
            return false;
        }
        log_->push_back("reset full");
        appliedScissors.push_back(molga::PixelRectU32{});
        return true;
    }

    static std::string RectText(const molga::PixelRectU32& rect) {
        return std::to_string(rect.x) + "," + std::to_string(rect.y) + "," +
               std::to_string(rect.width) + "," + std::to_string(rect.height);
    }

    const std::vector<std::string>& CallOrder() const noexcept { return *log_; }

    RecordingStats stats;
    bool hasFrame = true;
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const void>>> retained;
    std::vector<molga::ResourceLifetimeDomain> retainedDomains;
    std::vector<molga::PixelRectU32> appliedScissors;
    // ── 실패를 주입할 클립 상태 호출의 0-기반 번호 ──────────────────────────
    // **그 호출 하나만** 실패하고 나머지는 성공한다. 예전 모양("그 뒤로 전부
    // 실패")으로 두면 실패 뒤의 복원 시도까지 실패하므로, "패스를 되돌렸다"와
    // "되돌리지 않았다"가 같은 로그와 같은 appliedScissors를 낸다 — 픽스처가
    // 재려는 차이를 픽스처가 지운다. 기본값은 어떤 호출과도 같지 않다.
    static constexpr std::size_t kNeverFail =
        std::numeric_limits<std::size_t>::max();
    std::size_t failScissorCallIndex = kNeverFail;
    // 그 자리에서 연속으로 몇 번 실패시킬지. 복원까지 실패하는 경우를 만들려면
    // 둘이 필요하다 — 하나로는 "복원이 성공했다"만 만들 수 있다.
    std::size_t failScissorCallCount = 1U;

private:
    std::size_t scissorCalls_ = 0;
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
        lastSubmitComplete = SubmitVisibleCommands(
            queue.GetCommands(), cameraBounds, renderer, batcher, &lastError);
    }

    // 클립 하나를 실은 배치 가능한 sprite. 세 개 이상을 이웃으로 놓을 수
    // 있어야 "같은 클립은 flush하지 않는다"와 "다른 클립은 flush한다"가 한
    // 시퀀스 안에서 함께 관찰된다.
    void EnqueueClippedSprite(std::optional<molga::PixelRectU32> scissor) {
        RenderCommand command = TextCommand(0U, nullptr);
        command.scissor = scissor;
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    void EnqueueUiCommand(const molga::ui::UIDrawOrderKey& order) {
        EnqueueUiCommandInPass(order, 100);
    }

    // ── 한 패스 안에 두 생산자를 놓을 수 있어야 한다 ────────────────────────
    // 기본 UI 헬퍼는 cameraPass 100을 찍는다. 그 값 때문에 키를 가진 명령과
    // 갖지 않은 명령이 어떤 픽스처에서도 같은 패스를 공유하지 못했고, 그래서
    // 비교자의 갈래 선택이 문제가 되는 그 경계가 한 번도 시험되지 않았다.
    void EnqueueUiCommandInPass(const molga::ui::UIDrawOrderKey& order,
                                int cameraPass) {
        RenderCommand command = TextCommand(0U, nullptr);
        command.sortKey.cameraPass = cameraPass;
        command.uiDrawOrder = order;
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    // 레거시 UISystem::CollectRender가 내는 모양 그대로: UI 패스 번호를 쓰되
    // uiDrawOrder는 싣지 않는다(src/UI/UISystem.cpp의 sortKey.cameraPass = 1).
    void EnqueueLegacyUiCommand(int cameraPass) {
        RenderCommand command = TextCommand(0U, nullptr);
        command.sortKey.cameraPass = cameraPass;
        enqueuedKeys.push_back(command.batchKey);
        queue.Submit(command);
    }

    std::vector<std::uint64_t> SubmissionIndicesAfterSort() const {
        std::vector<std::uint64_t> out;
        for (const RenderCommand& command : queue.GetCommands()) {
            out.push_back(command.sortKey.submissionIndex);
        }
        return out;
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
    bool lastSubmitComplete = false;
    std::string lastError;

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

// ═══ Task 11.2 Step 1a/1b/6b/7a-7c: 클립은 상태이지 배치 정체성이 아니다 ════

// Step 1a. 명령 넷을 A, A, B, 클립 없음으로 놓는다. 하나나 둘로는 아무것도
// 재지 못한다: 하나면 전이가 없고, 둘이면 "언제나 flush한다"와 "같은 값에는
// flush하지 않는다"가 같은 로그를 낸다. 인접한 같은 값 A가 둘 있어야 그
// 구분이 로그에 남는다.
TEST_CASE("equal adjacent scissors keep batching and a clip change flushes") {
    RenderQueueFixture f;
    const molga::PixelRectU32 a{10U, 20U, 30U, 40U};
    const molga::PixelRectU32 b{50U, 60U, 70U, 80U};
    f.EnqueueClippedSprite(a);
    f.EnqueueClippedSprite(a);
    f.EnqueueClippedSprite(b);
    f.EnqueueClippedSprite(std::nullopt);
    f.Render();

    CHECK(f.lastSubmitComplete);
    CHECK((f.log == std::vector<std::string>{
              "set 10,20,30,40", "draw", "draw", "flush",
              "set 50,60,70,80", "draw", "flush", "reset full", "draw"}));
    // 클립 전이로 생긴 flush는 정확히 둘이다. 위 시퀀스가 그것을 이미 담고
    // 있지만, 계수기를 따로 못 박아 두면 시퀀스를 손보는 나중의 편집이 이
    // 계약을 조용히 지나칠 수 있다.
    std::size_t flushes = 0U;
    for (const std::string& entry : f.log) {
        if (entry == "flush") ++flushes;
    }
    CHECK(flushes == 2U);
    // 세운 사각형이 실제로 그 값이다. 값 없는 "set" 로그만 보면 A를 두 번
    // 세우고 B를 건너뛴 구현도 통과한다.
    REQUIRE(f.renderer.appliedScissors.size() == 3U);
    CHECK(f.renderer.appliedScissors[0] == a);
    CHECK(f.renderer.appliedScissors[1] == b);
    CHECK(f.renderer.appliedScissors[2] == molga::PixelRectU32{});
}

// 위 케이스는 마지막 명령이 클립을 갖지 않아 끝에서의 복원을 재지 못한다.
// 클립이 남은 채로 끝나는 큐가 그 나머지 절반이다.
TEST_CASE("a clip still active at queue end is flushed and restored") {
    RenderQueueFixture f;
    const molga::PixelRectU32 a{1U, 2U, 3U, 4U};
    f.EnqueueClippedSprite(std::nullopt);
    f.EnqueueClippedSprite(a);
    f.Render();
    CHECK(f.lastSubmitComplete);
    CHECK((f.log == std::vector<std::string>{
              "draw", "flush", "set 1,2,3,4", "draw", "flush", "reset full"}));
}

// Step 7b. 상태 호출이 실패하면 남은 명령은 이전 클립 아래에서 그려지지
// 않는다. 실패해도 계속 그리는 구현은 draw 수에서만 드러난다.
//
// 그리고 실패해도 **패스는 되돌아간다**. 이 케이스가 예전에 재던 것은 로그
// 뿐이었고, 로그는 "A를 세운 채 그냥 나갔다"와 "A를 세웠다가 되돌리고
// 나갔다"를 구별하지 못한다 — 그래서 그 단언은 고장난 동작에 동의했다.
// 남은 클립이 패스에 그대로 있으면 같은 패스를 이어 쓰는 다음 소비자(ImGui
// 오버레이)가 UI 클립 아래에서 그린다. 재야 하는 것은 로그가 아니라
// **패스의 상태**이므로, 마지막으로 세워진 사각형을 못 박는다.
TEST_CASE("a failed clip state call stops the queue and still restores the pass") {
    RenderQueueFixture f;
    const molga::PixelRectU32 a{1U, 2U, 3U, 4U};
    const molga::PixelRectU32 b{5U, 6U, 7U, 8U};
    f.EnqueueClippedSprite(a);
    f.EnqueueClippedSprite(b);
    f.EnqueueClippedSprite(b);
    f.renderer.failScissorCallIndex = 1U;  // A는 세워지고 B에서 실패한다
    f.Render();
    CHECK_FALSE(f.lastSubmitComplete);
    CHECK(f.lastError == "injected scissor failure");
    // 남은 두 명령은 그려지지 않는다.
    CHECK((f.log == std::vector<std::string>{"set 1,2,3,4", "draw", "flush",
                                             "reset full"}));
    // 그리고 패스에 마지막으로 세워진 것은 A가 아니라 전체 사각형이다.
    REQUIRE_FALSE(f.renderer.appliedScissors.empty());
    CHECK(f.renderer.appliedScissors.back() == molga::PixelRectU32{});
    // 보고되는 사유는 첫 실패의 것이다 — 최선 노력의 복원이 그 사유를
    // 덮어쓰면 진단이 원인이 아니라 뒤처리를 가리킨다.
    CHECK(f.lastError != "injected scissor reset failure");
}

// 복원 자체가 실패해도 함수는 거짓을 돌려주고 남은 명령을 그리지 않는다.
// 최선 노력이 "노력했으니 계속 그린다"로 바뀌는 회귀는 여기서만 보인다.
TEST_CASE("a failed clip restore still stops the queue and keeps the first cause") {
    RenderQueueFixture f;
    const molga::PixelRectU32 a{1U, 2U, 3U, 4U};
    const molga::PixelRectU32 b{5U, 6U, 7U, 8U};
    f.EnqueueClippedSprite(a);
    f.EnqueueClippedSprite(b);
    f.EnqueueClippedSprite(std::nullopt);
    // 호출 0/1은 A와 B의 set이다. 호출 2가 마지막 명령의 reset이고, 호출 3이
    // 그 실패 뒤의 최선 노력 복원이다 — 둘 다 실패시킨다.
    f.renderer.failScissorCallIndex = 2U;
    f.renderer.failScissorCallCount = 2U;
    f.Render();
    CHECK_FALSE(f.lastSubmitComplete);
    CHECK(f.lastError == "injected scissor reset failure");
    CHECK((f.log == std::vector<std::string>{"set 1,2,3,4", "draw", "flush",
                                             "set 5,6,7,8", "draw", "flush"}));
    // 되돌리지 못했으므로 패스에는 B가 남아 있다. 이 사실을 숨기지 않는 것이
    // 최선 노력의 계약이다 — 돌아가는 값이 거짓인 이유가 그것이다.
    REQUIRE(f.renderer.appliedScissors.size() == 2U);
    CHECK(f.renderer.appliedScissors.back() == b);
}

// Step 1b. 클립이 batch 정체성이 아니라는 것은 값 하나로 못 박힌다.
TEST_CASE("scissor is not batch identity") {
    molga::RenderCommand a;
    molga::RenderCommand b;
    a.batchKey.shaderName = b.batchKey.shaderName = "batch";
    a.scissor = molga::PixelRectU32{0, 0, 10, 10};
    b.scissor = molga::PixelRectU32{10, 0, 10, 10};
    CHECK(a.batchKey == b.batchKey);
    CHECK_FALSE(a.batchKey < b.batchKey);
    CHECK_FALSE(b.batchKey < a.batchKey);
    // uiDrawOrder도 마찬가지다. 두 필드를 한 케이스에서 함께 보는 이유는
    // 하나만 빠져나가도 UI 명령이 draw order마다 다른 batch로 쪼개지기
    // 때문이다.
    a.uiDrawOrder = molga::ui::UIDrawOrderKey{};
    b.uiDrawOrder = molga::ui::UIDrawOrderKey{};
    b.uiDrawOrder->stableSubmissionIndex = 99U;
    CHECK(a.batchKey == b.batchKey);
}

// Step 6b. siblingPath를 정수 하나로 접은 구현에서만 갈리는 쌍이다.
// {0,5}는 {1}보다 앞서지만(사전식), 마지막 성분이나 합으로 접으면 5 > 1이라
// 순서가 뒤집힌다. 삽입 순서는 정답의 역순이라 정렬이 실제로 움직여야 한다.
TEST_CASE("UI commands sort by the complete draw key, not a flattened sibling path") {
    RenderQueueFixture f;
    molga::ui::UIDrawOrderKey deepLater;
    deepLater.siblingPath = {1U};
    deepLater.componentSortingOrder = 0;
    molga::ui::UIDrawOrderKey shallowEarlier;
    shallowEarlier.siblingPath = {0U, 5U};
    shallowEarlier.componentSortingOrder = 0;
    f.EnqueueUiCommand(deepLater);       // submissionIndex 0
    f.EnqueueUiCommand(shallowEarlier);  // submissionIndex 1
    f.queue.Sort();
    CHECK((f.SubmissionIndicesAfterSort() ==
           std::vector<std::uint64_t>{1U, 0U}));
}

// 완전한 키가 같은 UI 명령들은 배정된 submissionIndex 순서를 지킨다.
// std::sort는 안정 정렬이 아니므로, 동률에서 false만 돌려주는 비교자는 같은
// 입력에 대해 다른 순서를 낼 수 있다. 128개인 이유는 하나다 — 작은 범위는
// 삽입 정렬로 처리되어 우연히 순서가 보존되고, 그 크기에서는 동률 처리를
// 통째로 지워도 이 단언이 통과한다.
TEST_CASE("equal UI draw keys fall back to the assigned submission index") {
    RenderQueueFixture f;
    std::vector<std::uint64_t> expected;
    for (std::uint32_t index = 0; index < 128U; ++index) {
        molga::ui::UIDrawOrderKey key;
        key.siblingPath = {index % 2U};
        f.EnqueueUiCommand(key);
    }
    for (std::uint64_t index = 0; index < 128U; index += 2U) expected.push_back(index);
    for (std::uint64_t index = 1; index < 128U; index += 2U) expected.push_back(index);
    f.queue.Sort();
    CHECK((f.SubmissionIndicesAfterSort() == expected));
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 11.2 close-out: 비교자는 큐 전체에 대한 **하나의 전순서**여야 한다.
//
// 예전 비교자는 두 명령이 모두 uiDrawOrder를 가질 때만 완전한 키로 비교하고
// 나머지는 sortKey로 떨어졌다. 한 범위 위에 서로 다른 두 순서가 있으면 관계는
// 추이적이지 않고, 비추이적 비교자를 받은 std::sort는 **미정의 동작**이다.
//
// 이 셋이 그 순환이다(전부 cameraPass 1, 나머지 sortKey 필드는 기본값):
//   submissionIndex 0: 키 K_hi를 가진 UI 명령
//   submissionIndex 1: 키가 없는 레거시 UI 명령
//   submissionIndex 2: 키 K_lo(< K_hi)를 가진 UI 명령
// 옛 비교자: comp(2,0)=참(완전한 키), comp(0,1)=참(sortKey), comp(1,2)=참.
//
// 그리고 그 레거시 명령은 가상이 아니다: src/UI/UISystem.cpp의
// CollectRender가 cameraPass 1을 uiDrawOrder 없이 낸다. Task 12.3이 한
// 표면만 옮기는 첫 프레임이 정확히 이 큐다.
//
// 단언은 libc++의 정렬 내부가 아니라 계약을 붙든다: 키를 가진 두 명령은
// 언제나 키 순서로 나온다. 옛 비교자는 그 둘을 뒤집힌 채로 남긴다.
TEST_CASE("a queue mixing key-bearing and key-less UI commands has one total order") {
    // UIRenderCollector.h의 kUISnapshotCameraPass와 레거시
    // UISystem::CollectRender가 쓰는 값이 같다는 것이 이 케이스의 전제다.
    // 헤더를 끌어오지 않고 그 값을 여기 적는 이유는 하나다 — 이 파일이
    // 재는 것은 RenderQueue이지 UI 수집기가 아니다.
    constexpr int kUiCameraPass = 1;
    RenderQueueFixture f;
    molga::ui::UIDrawOrderKey high;
    high.siblingPath = {9U};
    molga::ui::UIDrawOrderKey low;
    low.siblingPath = {1U};
    REQUIRE(low < high);

    f.EnqueueUiCommandInPass(high, kUiCameraPass);   // 0
    f.EnqueueLegacyUiCommand(kUiCameraPass);         // 1
    f.EnqueueUiCommandInPass(low, kUiCameraPass);    // 2
    f.queue.Sort();

    const std::vector<std::uint64_t> order = f.SubmissionIndicesAfterSort();
    REQUIRE(order.size() == 3U);
    const auto positionOf = [&order](std::uint64_t submissionIndex) {
        for (std::size_t at = 0; at < order.size(); ++at) {
            if (order[at] == submissionIndex) return at;
        }
        FAIL("submission index missing from the sorted queue");
        return std::size_t{0};
    };
    // 키를 가진 두 명령은 키 순서로 나온다. 옛 비교자는 0을 맨 앞에 두고
    // 2를 맨 뒤에 남긴다.
    CHECK(positionOf(2U) < positionOf(0U));
    // 그리고 전체 순서는 결정적이다: 같은 패스 안에서 키 없는 무리가 먼저다.
    CHECK((order == std::vector<std::uint64_t>{1U, 2U, 0U}));
}

// 그 전순서가 월드 명령을 UI 뒤로 밀지 않는다. cameraPass가 여전히 가장
// 바깥의 항이라는 것이 이 케이스다 — 그것을 잃으면 UI가 월드 밑에 그려진다.
TEST_CASE("camera pass still outranks the UI draw key") {
    constexpr int kUiCameraPass = 1;
    RenderQueueFixture f;
    molga::ui::UIDrawOrderKey first;
    first.siblingPath = {0U};
    f.EnqueueUiCommandInPass(first, kUiCameraPass); // 0
    f.EnqueueLegacyUiCommand(0);                                       // 1 (월드 패스)
    f.queue.Sort();
    CHECK((f.SubmissionIndicesAfterSort() == std::vector<std::uint64_t>{1U, 0U}));
}
