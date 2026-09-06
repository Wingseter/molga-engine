// ── Task 6.2: GPU 제출 fence만이 page 수명을 끝낸다 ─────────────────────────
// 이 파일이 재는 계약은 하나다: 제출된 프레임이 가리키던 atlas page 토큰은
// 그 프레임의 fence가 신호한 다음에만 반납된다. 프레임 경계도, 참조 계수도
// 반납의 근거가 아니다.
//
// fence는 가짜다. 진짜 SDL fence로는 "아직 신호하지 않았다"를 결정적으로
// 관찰할 수 없기 때문이다(GPU가 먼저 끝내 버리면 그 단언은 사라진다).
// 진짜 fence 쪽 배선은 test_rendering_sdlgpu가 실제 장치 위에서 잰다.
#include "Core/Bootstrap.h"
#include "Rendering/GpuRetirementQueue.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/Renderer.h"
#include "doctest.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace {

// ── The mockable fence ──────────────────────────────────────────────────────
// 질의 횟수를 함께 센다. Poll이 이미 반납한 항목을 계속 다시 묻고 있으면
// 계수기가 자란다 — "신호한 뒤에도 항목이 남아 있다"를 만료 여부만으로는
// 구별할 수 없다.
class FakeGpuCompletionFence final : public molga::IGpuCompletionFence {
public:
    bool IsSignaled() const noexcept override {
        ++queries_;
        return signaled_;
    }
    void Signal() noexcept { signaled_ = true; }
    std::size_t QueryCount() const noexcept { return queries_; }

private:
    mutable std::size_t queries_ = 0;
    bool signaled_ = false;
};

std::unique_ptr<molga::IGpuCompletionFence> MakeFakeFence(
    FakeGpuCompletionFence*& rawOut) {
    auto fence = std::make_unique<FakeGpuCompletionFence>();
    rawOut = fence.get();
    return fence;
}

// ── The renderer fixture ────────────────────────────────────────────────────
// 활성 FrameContext가 있어야만 RetainUntilFrameComplete를 관찰할 수 있으므로,
// 진짜(보이지 않는) 창과 장치를 연다. test_rendering_sdlgpu의 모든 케이스가
// 같은 방식으로 헤드리스 장치를 쓴다.
struct RendererFixture {
    RendererFixture() {
        WindowConfig config;
        config.title = "Molga GPU retirement";
        config.width = 64;
        config.height = 64;
        config.visible = false;
        host = EngineInit(config);
        REQUIRE(host);
        std::string error;
        REQUIRE_MESSAGE(renderer.Init(&error), error);
    }

    ~RendererFixture() {
        renderer.Shutdown();
        EngineShutdown(host);
    }

    RendererFixture(const RendererFixture&) = delete;
    RendererFixture& operator=(const RendererFixture&) = delete;

    // frameIndex는 렌더러가 읽는 값이 아니다(Renderer는 프레임 번호를 갖지
    // 않는다). 어느 프레임에서 무엇이 어긋났는지 실패 메시지로 남기기 위한
    // 라벨이고, 케이스들이 프레임을 구분해 부르도록 강제한다.
    void BeginFrame(std::uint64_t frameIndex) {
        molga::BeginFrameResult acquired = host->BeginFrame();
        REQUIRE_MESSAGE(acquired.status == molga::FrameAcquireStatus::Acquired,
                        "frame " << frameIndex << ": " << acquired.error);
        std::string error;
        REQUIRE_MESSAGE(renderer.BeginFrame(std::move(acquired.frame), &error),
                        "frame " << frameIndex << ": " << error);
        activeFrameIndex = frameIndex;
    }

    void SubmitAndCompleteFrame() {
        std::string error;
        REQUIRE_MESSAGE(renderer.SubmitFrame(&error),
                        "frame " << activeFrameIndex << ": " << error);
        REQUIRE_MESSAGE(host->Graphics().WaitIdle(&error),
                        "frame " << activeFrameIndex << ": " << error);
    }

    std::unique_ptr<EngineHost> host;
    Renderer renderer;
    std::uint64_t activeFrameIndex = 0;
};

// 주입 플래그는 프로세스 전역이다. 사이에 REQUIRE 하나가 끼어들어 예외로
// 빠져나가면 그 뒤의 모든 케이스가 fence 획득이 망가진 채로 돈다. 되돌림을
// 소멸자에 맡긴다.
class ScopedFenceAcquisitionFailure {
public:
    ScopedFenceAcquisitionFailure() {
        molga::detail::SetGpuFenceAcquisitionFailureInjectionForTest(true);
    }
    ScopedFenceAcquisitionFailure(const ScopedFenceAcquisitionFailure&) = delete;
    ScopedFenceAcquisitionFailure& operator=(
        const ScopedFenceAcquisitionFailure&) = delete;
    ~ScopedFenceAcquisitionFailure() {
        molga::detail::SetGpuFenceAcquisitionFailureInjectionForTest(false);
    }
};

// ── The shutdown-order subprocess ───────────────────────────────────────────
// 실패한 GPU idle wait는 프로세스를 죽인다. 관찰하려면 죽어도 되는 프로세스가
// 하나 필요하므로, test_text_runtime_dependencies가 쓰는 것과 같은
// posix_spawn 방식으로 전용 child를 띄운다. 셸을 거치지 않으므로 공백이나 셸
// 메타문자가 든 경로도 인자를 바꾸지 못한다.
struct SubprocessResult {
    int exitCode = -1;
    int terminationSignal = 0;
    std::string stdoutText;
    std::string stderrText;
    std::set<std::string> markers;
};

std::string ReadAllBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 실행마다 고유한 디렉터리 하나. 실패한 CHECK가 임시 트리에 잔해를 남기지
// 않도록 RAII로 지운다.
class SubprocessScratchRoot {
public:
    SubprocessScratchRoot() {
        static unsigned counter = 0;
        const fs::path base = fs::canonical(fs::temp_directory_path());
        fs::path candidate;
        do {
            candidate = base / ("molga-gpu-retirement-" +
                                std::to_string(static_cast<long>(::getpid())) +
                                "-" + std::to_string(counter++));
        } while (fs::exists(candidate));
        REQUIRE(fs::create_directory(candidate));
        path_ = fs::canonical(candidate);
    }

    SubprocessScratchRoot(const SubprocessScratchRoot&) = delete;
    SubprocessScratchRoot& operator=(const SubprocessScratchRoot&) = delete;

    ~SubprocessScratchRoot() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const noexcept { return path_; }

private:
    fs::path path_;
};

SubprocessResult RunRendererShutdownSubprocess(
    const std::vector<std::string>& flags) {
    const SubprocessScratchRoot scratch;
    const fs::path outPath = scratch.Path() / "stdout.txt";
    const fs::path errPath = scratch.Path() / "stderr.txt";
    const fs::path markerPath = scratch.Path() / "shutdown-markers.txt";

    std::vector<std::string> argv;
    argv.push_back(MOLGA_GPU_RETIREMENT_PROBE);
    for (const std::string& flag : flags) argv.push_back(flag);
    argv.push_back("--marker-file");
    argv.push_back(markerPath.string());

    // doctest의 REQUIRE는 예외를 던지므로 file actions와 child는 되감기는
    // 소유자가 있어야 한다.
    struct FileActions {
        posix_spawn_file_actions_t value{};
        bool initialized = false;
        FileActions() { initialized = posix_spawn_file_actions_init(&value) == 0; }
        FileActions(const FileActions&) = delete;
        FileActions& operator=(const FileActions&) = delete;
        ~FileActions() {
            if (initialized) posix_spawn_file_actions_destroy(&value);
        }
    } actions;
    REQUIRE(actions.initialized);
    REQUIRE(posix_spawn_file_actions_addopen(
                &actions.value, STDOUT_FILENO, outPath.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0);
    REQUIRE(posix_spawn_file_actions_addopen(
                &actions.value, STDERR_FILENO, errPath.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0);

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& argument : argv) {
        raw.push_back(const_cast<char*>(argument.c_str()));
    }
    raw.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, raw[0], &actions.value, nullptr,
                                    raw.data(), environ);
    REQUIRE(spawned == 0);

    struct ChildReaper {
        pid_t pid = 0;
        bool reaped = false;
        ~ChildReaper() {
            if (!reaped) {
                int discarded = 0;
                ::waitpid(pid, &discarded, 0);
            }
        }
    } reaper{pid, false};

    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);
    reaper.reaped = true;

    SubprocessResult result;
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.terminationSignal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    result.stdoutText = ReadAllBytes(outPath);
    result.stderrText = ReadAllBytes(errPath);
    std::istringstream markers(ReadAllBytes(markerPath));
    for (std::string line; std::getline(markers, line);) {
        if (!line.empty()) result.markers.insert(line);
    }
    return result;
}

} // namespace

TEST_CASE("submitted page lifetime releases only after its fence signals") {
    FakeGpuCompletionFence* rawFence = nullptr;
    molga::GpuRetirementQueue queue;
    auto lifetime = std::make_shared<int>(7);
    std::weak_ptr<const void> weak = lifetime;
    auto fence = MakeFakeFence(rawFence);
    queue.Enqueue(std::move(fence), {lifetime});
    lifetime.reset();
    queue.Poll();
    CHECK_FALSE(weak.expired());
    rawFence->Signal();
    queue.Poll();
    CHECK(weak.expired());
}

TEST_CASE("the pending submission count follows enqueue and retirement") {
    // CHECK_FALSE 한쪽만 있는 케이스는 성공 증인이 없다. 계수기가 양쪽으로
    // 움직이는 것과, Poll이 실제로 fence에 물어본다는 것을 여기서 못 박는다.
    FakeGpuCompletionFence* rawFence = nullptr;
    molga::GpuRetirementQueue queue;
    CHECK(queue.PendingSubmissionCount() == 0U);
    queue.Enqueue(MakeFakeFence(rawFence), {std::make_shared<int>(1)});
    CHECK(queue.PendingSubmissionCount() == 1U);
    CHECK(rawFence->QueryCount() == 0U);
    queue.Poll();
    CHECK(rawFence->QueryCount() == 1U);
    CHECK(queue.PendingSubmissionCount() == 1U);
    rawFence->Signal();
    queue.Poll();
    CHECK(queue.PendingSubmissionCount() == 0U);
}

TEST_CASE("a fence that signals out of insertion order retires only its own") {
    // 세 제출을 순서대로 넣고 가운데 것만 신호시킨다. fence 신호 순서가
    // 삽입 순서와 같은 픽스처는 "제 fence를 본다"와 "앞에서부터 하나씩
    // 버린다"를 구별하지 못한다.
    molga::GpuRetirementQueue queue;
    FakeGpuCompletionFence* first = nullptr;
    FakeGpuCompletionFence* second = nullptr;
    FakeGpuCompletionFence* third = nullptr;

    auto firstToken = std::make_shared<int>(1);
    auto secondToken = std::make_shared<int>(2);
    auto thirdToken = std::make_shared<int>(3);
    std::weak_ptr<const void> firstWeak = firstToken;
    std::weak_ptr<const void> secondWeak = secondToken;
    std::weak_ptr<const void> thirdWeak = thirdToken;

    queue.Enqueue(MakeFakeFence(first), {firstToken});
    queue.Enqueue(MakeFakeFence(second), {secondToken});
    queue.Enqueue(MakeFakeFence(third), {thirdToken});
    firstToken.reset();
    secondToken.reset();
    thirdToken.reset();

    second->Signal();
    queue.Poll();
    CHECK(queue.PendingSubmissionCount() == 2U);
    CHECK_FALSE(firstWeak.expired());
    CHECK(secondWeak.expired());
    CHECK_FALSE(thirdWeak.expired());

    // 남은 둘은 삽입 순서를 유지한다: 세 번째를 신호시키면 첫 번째는 그대로다.
    third->Signal();
    queue.Poll();
    CHECK(queue.PendingSubmissionCount() == 1U);
    CHECK_FALSE(firstWeak.expired());
    CHECK(thirdWeak.expired());

    first->Signal();
    queue.Poll();
    CHECK(queue.PendingSubmissionCount() == 0U);
    CHECK(firstWeak.expired());
}

TEST_CASE("one submission retires every token it carried, together") {
    molga::GpuRetirementQueue queue;
    FakeGpuCompletionFence* rawFence = nullptr;
    auto pageA = std::make_shared<int>(11);
    auto pageB = std::make_shared<int>(22);
    std::weak_ptr<const void> weakA = pageA;
    std::weak_ptr<const void> weakB = pageB;
    queue.Enqueue(MakeFakeFence(rawFence), {pageA, pageB});
    pageA.reset();
    pageB.reset();
    queue.Poll();
    CHECK_FALSE(weakA.expired());
    CHECK_FALSE(weakB.expired());
    rawFence->Signal();
    queue.Poll();
    CHECK(weakA.expired());
    CHECK(weakB.expired());
}

TEST_CASE("missing submission fence retains resources until idle drain") {
    molga::GpuRetirementQueue queue;
    auto lifetime = std::make_shared<int>(9);
    std::weak_ptr<const void> weak = lifetime;
    queue.RetainWithoutFence({lifetime});
    lifetime.reset();
    queue.Poll();
    CHECK_FALSE(weak.expired());
    queue.DrainAfterGpuIdle();
    CHECK(weak.expired());
}

TEST_CASE("a null fence is retained rather than released without proof") {
    // Enqueue가 fence 없이 불리는 것은 호출자의 오류다. 그래도 여기서 토큰을
    // 놓아 버리면 제출된 명령 밑에서 텍스처가 사라진다. 증거가 없을 때의
    // 안전한 쪽은 "붙들고 있기"다.
    molga::GpuRetirementQueue queue;
    auto lifetime = std::make_shared<int>(5);
    std::weak_ptr<const void> weak = lifetime;
    queue.Enqueue(nullptr, {lifetime});
    lifetime.reset();
    CHECK(queue.PendingSubmissionCount() == 0U);
    queue.Poll();
    CHECK_FALSE(weak.expired());
    queue.DrainAfterGpuIdle();
    CHECK(weak.expired());
}

TEST_CASE("an idle drain releases unsignaled submissions as well") {
    // 장치가 idle임이 증명된 뒤에는 신호하지 않은 fence도 더 기다릴 이유가
    // 없다. 여기서 놓아주지 않으면 fence 획득이 실패한 프로세스가 종료할 때
    // page를 영원히 붙들고 있게 된다.
    molga::GpuRetirementQueue queue;
    FakeGpuCompletionFence* rawFence = nullptr;
    auto fenced = std::make_shared<int>(1);
    auto unfenced = std::make_shared<int>(2);
    std::weak_ptr<const void> fencedWeak = fenced;
    std::weak_ptr<const void> unfencedWeak = unfenced;
    queue.Enqueue(MakeFakeFence(rawFence), {fenced});
    queue.RetainWithoutFence({unfenced});
    fenced.reset();
    unfenced.reset();
    queue.Poll();
    CHECK_FALSE(fencedWeak.expired());
    CHECK_FALSE(unfencedWeak.expired());
    CHECK(queue.PendingSubmissionCount() == 1U);
    queue.DrainAfterGpuIdle();
    CHECK(fencedWeak.expired());
    CHECK(unfencedWeak.expired());
    CHECK(queue.PendingSubmissionCount() == 0U);
}

TEST_CASE("one submitted frame retains one token per atlas page") {
    RendererFixture f;
    f.BeginFrame(9);
    auto page = std::make_shared<int>(3);
    f.renderer.RetainUntilFrameComplete(17, page);
    f.renderer.RetainUntilFrameComplete(17, page);
    CHECK_THROWS_AS(f.renderer.RetainUntilFrameComplete(
        17, std::make_shared<int>(99)), std::logic_error);
    f.renderer.RetainUntilFrameComplete(18, std::make_shared<int>(4));
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 2);
    f.SubmitAndCompleteFrame();
}

TEST_CASE("the active-frame retain guard rejects exactly what it must") {
    RendererFixture f;
    auto page = std::make_shared<int>(1);

    // 활성 프레임 밖의 호출은 거절된다. 이 순서가 뒤집히면 프레임이 없는데도
    // 토큰이 조용히 쌓인다.
    CHECK_THROWS_AS(f.renderer.RetainUntilFrameComplete(21, page),
                    std::logic_error);
    // 널 토큰은 프레임 밖에서도 무시된다 — 널 검사가 프레임 검사보다 앞선다.
    CHECK_NOTHROW(f.renderer.RetainUntilFrameComplete(21, nullptr));

    f.BeginFrame(1);
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 0U);
    // 널 토큰은 아무것도 남기지 않는다.
    CHECK_NOTHROW(f.renderer.RetainUntilFrameComplete(21, nullptr));
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 0U);
    // 0은 적법한 page 정체성이 아니다. 받아 주면 tofu handle의 pageIdentity 0이
    // 하나의 가짜 page로 뭉쳐서, 그 자리에 진짜 page 토큰이 들어갈 수 없다.
    CHECK_THROWS_AS(f.renderer.RetainUntilFrameComplete(0, page),
                    std::logic_error);
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 0U);
    // 성공 증인: 정상 호출은 정확히 하나를 남긴다.
    CHECK_NOTHROW(f.renderer.RetainUntilFrameComplete(21, page));
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 1U);
    f.SubmitAndCompleteFrame();
}

TEST_CASE("submitting a frame hands its retained pages to the retirement queue") {
    RendererFixture f;
    auto page = std::make_shared<int>(1);
    std::weak_ptr<const void> weak = page;

    f.BeginFrame(1);
    f.renderer.RetainUntilFrameComplete(31, page);
    page.reset();
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 1U);
    f.SubmitAndCompleteFrame();
    // 제출 자체는 반납이 아니다. 프레임 소유의 map은 비었지만 토큰은 살아 있다.
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 0U);
    CHECK_FALSE(weak.expired());

    // 다음 프레임의 시작이 Poll을 돌린다. 앞 프레임의 fence는 위 WaitIdle로
    // 이미 신호했으므로 여기서 반납된다.
    f.BeginFrame(2);
    CHECK(weak.expired());
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 0U);
    f.SubmitAndCompleteFrame();
}

// fence 획득이 실패한 제출은 언제 끝났는지 알 방법이 없다. 그때 토큰을 놓으면
// 정확히 이 마일스톤이 막으려는 일 — 제출된 명령 밑에서 사라지는 텍스처 — 이
// 난다. 프레임 경계도, 성공한 WaitIdle 뒤의 Poll도 그것을 풀어 주지 못하고,
// 오직 종료의 idle drain만이 풀어 준다.
//
// 실패를 주입하는 이유: 진짜 장치에서 fence 획득 실패는 요구해서 만들 수 없다.
TEST_CASE("a submission whose fence was lost holds its pages until the drain") {
    RendererFixture f;
    auto page = std::make_shared<int>(1);
    std::weak_ptr<const void> weak = page;
    std::string error;

    f.BeginFrame(1);
    f.renderer.RetainUntilFrameComplete(41, page);
    page.reset();
    REQUIRE(f.renderer.ActiveFrameRetainedPageCount() == 1U);

    {
        const ScopedFenceAcquisitionFailure injected;
        // 제출은 성공으로 보고된다. SDL은 백엔드로 넘기기 전에 명령 버퍼를
        // 소비 완료로 표시하므로 이 프레임은 실제로 제출되고 제시되었다.
        // fence가 없다는 것은 "제출되지 않았다"가 아니라 "언제 끝나는지 알 수
        // 없다"는 뜻이고, 그 둘을 하나의 false로 뭉치면 화면에 나간 프레임
        // 때문에 애플리케이션이 종료된다(runtime_main의 제출 실패 경로).
        CHECK_MESSAGE(f.renderer.SubmitFrame(&error), error);
    }
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 0U);
    CHECK_FALSE(weak.expired());

    // 장치를 idle까지 몰고 프레임 경계를 두 번 넘겨도 놓지 않는다. Poll은
    // fence가 있는 제출만 본다.
    REQUIRE_MESSAGE(f.host->Graphics().WaitIdle(&error), error);
    f.BeginFrame(2);
    CHECK_FALSE(weak.expired());
    f.SubmitAndCompleteFrame();
    f.BeginFrame(3);
    CHECK_FALSE(weak.expired());
    f.SubmitAndCompleteFrame();

    // 성공한 idle wait를 증명한 종료만이 그것을 풀어 준다.
    f.renderer.Shutdown();
    CHECK(weak.expired());
}

// ── Task 6.2: 막힌 반납은 무한히 자라지 않는다 ──────────────────────────────
// fence를 얻지 못하는 제출이 계속되면(또는 fence 질의가 불가능한 장치라면)
// Poll은 아무것도 반납하지 못한다. 그대로 두면 붙든 토큰이 프레임마다 늘고,
// atlas page의 쓰기 봉인이 영영 풀리지 않아 예산이 차는 순간부터 새 glyph가
// 전부 tofu가 된다. 무한히 자라는 실패 경로는 fail-closed가 아니다.
//
// 상한에 닿으면 렌더러가 증거를 만들어서 푼다: 성공한 GPU idle wait는 fence
// 신호보다 강한 증거이므로 "제출이 끝난 뒤에만 놓는다"는 계약은 그대로다.
// 양쪽을 잰다 — 상한 아래에서는 놓지 않고, 넘기면 놓는다.
TEST_CASE("a stalled retirement backlog is bounded by a forced idle drain") {
    RendererFixture f;
    auto page = std::make_shared<int>(1);
    std::weak_ptr<const void> weak = page;
    std::string error;
    const ScopedFenceAcquisitionFailure injected;

    f.BeginFrame(1);
    f.renderer.RetainUntilFrameComplete(61, page);
    page.reset();
    REQUIRE_MESSAGE(f.renderer.SubmitFrame(&error), error);

    // 상한 아래. 프레임 경계를 여러 번 넘어도 증거가 없으므로 놓지 않는다.
    constexpr std::uint64_t kBelowBound = 8U;
    std::uint64_t frameIndex = 2U;
    for (; frameIndex <= kBelowBound; ++frameIndex) {
        f.BeginFrame(frameIndex);
        CHECK_FALSE(weak.expired());
        f.renderer.RetainUntilFrameComplete(
            100U + frameIndex, std::make_shared<std::uint64_t>(frameIndex));
        REQUIRE_MESSAGE(f.renderer.SubmitFrame(&error), error);
    }
    REQUIRE_FALSE(weak.expired());

    // 상한을 넘기면 정확히 한 번의 강제 idle wait가 전부를 푼다. 상한(32)의
    // 두 배보다 넉넉히 돌려 놓고, 그 안에 풀리지 않으면 실패로 본다.
    constexpr std::uint64_t kAboveBound = 80U;
    for (; frameIndex <= kAboveBound && !weak.expired(); ++frameIndex) {
        f.BeginFrame(frameIndex);
        f.renderer.RetainUntilFrameComplete(
            100U + frameIndex, std::make_shared<std::uint64_t>(frameIndex));
        REQUIRE_MESSAGE(f.renderer.SubmitFrame(&error), error);
    }
    CHECK(weak.expired());
}

// Renderer::CurrentFrame()은 공개 탈출구다. 프레임을 렌더러 밖에서 닫으면
// (FrameContext::Submit을 직접 부르면) 그 프레임이 붙들고 있던 page 토큰은
// 어떤 fence와도 짝지어지지 않은 채 남는다. 그 명령은 이미 GPU에 갔으므로
// 여기서 토큰을 놓으면 제출된 명령 밑에서 텍스처가 사라진다 — Impl::ResetFrame
// 이 그것을 unfenced 목록으로 넘기는 이유이고, 이 케이스가 그 유일한 증인이다.
TEST_CASE("a frame closed outside the renderer keeps its pages to the drain") {
    RendererFixture f;
    auto page = std::make_shared<int>(1);
    std::weak_ptr<const void> weak = page;
    std::string error;

    f.BeginFrame(1);
    f.renderer.RetainUntilFrameComplete(51, page);
    page.reset();
    REQUIRE(f.renderer.ActiveFrameRetainedPageCount() == 1U);
    molga::FrameContext* frame = f.renderer.CurrentFrame();
    REQUIRE(frame != nullptr);
    REQUIRE_MESSAGE(frame->Submit(&error), error);
    // 렌더러는 이 제출을 보지 못했으므로 토큰은 아직 프레임 map에 있다.
    REQUIRE(f.renderer.ActiveFrameRetainedPageCount() == 1U);

    // 다음 프레임의 시작이 버려진 프레임을 치운다. 그 자리가 ResetFrame이다.
    f.BeginFrame(2);
    CHECK(f.renderer.ActiveFrameRetainedPageCount() == 0U);
    CHECK_FALSE(weak.expired());

    // 성공한 idle wait도 프레임 경계도 놓아 주지 않는다: fence가 없는 토큰은
    // Poll의 대상이 아니다.
    f.SubmitAndCompleteFrame();
    f.BeginFrame(3);
    CHECK_FALSE(weak.expired());
    f.SubmitAndCompleteFrame();

    f.renderer.Shutdown();
    CHECK(weak.expired());
}

TEST_CASE("failed GPU idle wait aborts before resource teardown") {
    const SubprocessResult result = RunRendererShutdownSubprocess(
        {"--inject-gpu-idle-wait-failure", "--write-shutdown-markers"});
    CHECK(result.terminationSignal == SIGABRT);
    CHECK(result.stderrText.find("GPU_IDLE_WAIT_FAILED") != std::string::npos);
    CHECK(result.markers.count("atlas-destroyed") == 0);
    CHECK(result.markers.count("device-destroyed") == 0);
}

// 위 케이스가 재지 못하는 것: 그 두 marker는 Renderer::Shutdown이 *돌아온
// 뒤에* probe가 쓴다. abort를 반납 drain 뒤로 옮기든 GPU 자원 파괴 루프 뒤로
// 옮기든 위 케이스는 그대로 통과한다 — 어느 쪽이든 프로세스는 Shutdown 안에서
// 죽고 두 marker는 남지 않기 때문이다. 그래서 Shutdown *안*의 단계를 본다.
TEST_CASE("the failed idle wait aborts before anything inside Shutdown runs") {
    const SubprocessResult result = RunRendererShutdownSubprocess(
        {"--inject-gpu-idle-wait-failure", "--write-shutdown-markers"});
    REQUIRE(result.terminationSignal == SIGABRT);
    // 죽는 순간까지 이 제출은 반납되지 않았다. 0이면 drain이 abort보다 먼저
    // 돌았다는 뜻이다.
    CHECK(result.stderrText.find("pending submissions: 1") !=
          std::string::npos);
    CHECK(result.markers.count("stage:idle-wait-proven") == 0);
    CHECK(result.markers.count("stage:retirement-drained") == 0);
    CHECK(result.markers.count("stage:gpu-resources-destroyed") == 0);
}

TEST_CASE("a successful GPU idle wait tears down atlas then device") {
    // 위 케이스의 성공 증인. 이것이 없으면 child를 언제나 죽게 만들어도, 또
    // marker를 아예 쓰지 않게 만들어도 위 케이스는 그대로 통과한다.
    const SubprocessResult result =
        RunRendererShutdownSubprocess({"--write-shutdown-markers"});
    CHECK(result.terminationSignal == 0);
    CHECK_MESSAGE(result.exitCode == 0, result.stderrText);
    CHECK(result.stderrText.find("GPU_IDLE_WAIT_FAILED") == std::string::npos);
    CHECK(result.markers.count("atlas-destroyed") == 1);
    CHECK(result.markers.count("device-destroyed") == 1);
    // 순서까지 못 박는다. Shutdown 안에서 idle 증명 -> 반납 drain -> GPU 자원
    // 파괴 순이고, 그다음에 atlas가, 마지막에 장치가 없어진다.
    CHECK(result.stdoutText.find(
              "order:stage:idle-wait-proven,stage:retirement-drained,"
              "stage:gpu-resources-destroyed,atlas-destroyed,"
              "device-destroyed") != std::string::npos);
}
