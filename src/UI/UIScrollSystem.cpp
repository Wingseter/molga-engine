#include "UI/UIScrollSystem.h"

#include "Core/World.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/GameObject.h"
#include "UI/UIHierarchy.h"
#include "UI/UIRuntimeInvalidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace molga::ui {

namespace {

using Raw = std::int32_t;

constexpr std::int64_t kRawMin = std::numeric_limits<Raw>::min();
constexpr std::int64_t kRawMax = std::numeric_limits<Raw>::max();
// 이 표면이 기억하는 스크롤 진단 사실의 상한. Task 11.2가 두 limiter에 세운
// 규칙과 같다: **기억할 수 없는 것은 보고하지도 않는다**. 기억하지 않고 보고만
// 하면 상한이 메모리를 지키는 바로 그 순간에 rate limit이 사라진다.
constexpr std::size_t kMaxRememberedScrollFacts = 256;

bool InRaw(std::int64_t value) noexcept {
    return value >= kRawMin && value <= kRawMax;
}

bool AddRaw(Raw a, Raw b, Raw& out) noexcept {
    const std::int64_t sum = static_cast<std::int64_t>(a) + b;
    if (!InRaw(sum)) return false;
    out = static_cast<Raw>(sum);
    return true;
}

bool SubRaw(Raw a, Raw b, Raw& out) noexcept {
    const std::int64_t diff = static_cast<std::int64_t>(a) - b;
    if (!InRaw(diff)) return false;
    out = static_cast<Raw>(diff);
    return true;
}

Raw ClampRaw(Raw value, Raw low, Raw high) noexcept {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

// 부호 있는 raw 하나의 절댓값을 int64로. int32에서 std::abs를 쓰면 INT32_MIN
// 하나가 UB다 — 승격이 먼저다.
std::int64_t AbsRaw(Raw value) noexcept {
    return value < 0 ? -static_cast<std::int64_t>(value)
                     : static_cast<std::int64_t>(value);
}

// ── 하나의 진단 rate limiter ────────────────────────────────────────────────
// 기억은 월드 세대마다 따로 산다. 하나의 전역 목록이었다면 상한이 프로세스
// 전체에 걸려, 앞선 월드가 남긴 사실 몇 개 때문에 새 월드의 진짜 첫 진단이
// 조용히 사라진다 — 그리고 "정확히 256"이라는 주장이 프로세스 역사에 달린
// 수가 되어 아무것도 재지 못한다. 키 자체에 오브젝트 id와 코드와 문구가
// 들어가므로 같은 월드 안에서 같은 사실은 한 번만 흐른다.
std::string ScrollFactKey(unsigned int objectId,
                          molga::text::TextDiagnosticCode code,
                          const std::string& message) {
    std::string key = std::to_string(objectId);
    key += ':';
    key += std::to_string(static_cast<int>(code));
    key += ':';
    key += message;
    return key;
}

struct IdentityHash {
    std::size_t operator()(const UIRuntimeTargetIdentity& id) const noexcept {
        // 필드별로 섞는다. 오브젝트 표현을 통째로 해시하면 패딩 바이트 때문에
        // 같은 식별자가 다른 해시를 낸다(UIRuntimeIdentity.h의 경고 그대로).
        std::size_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::uint64_t value) {
            hash = (hash ^ static_cast<std::size_t>(value)) * 1099511628211ULL;
        };
        mix(id.worldGeneration);
        mix(id.objectId);
        mix(static_cast<std::uint64_t>(id.componentRuntimeTypeId));
        mix(id.componentInstanceId);
        return hash;
    }
};

// 완전한 식별자의 사전식 전순서. Step 5d가 요구하는 그 순서 하나뿐이다.
bool IdentityLess(const UIRuntimeTargetIdentity& a,
                  const UIRuntimeTargetIdentity& b) noexcept {
    if (a.worldGeneration != b.worldGeneration) {
        return a.worldGeneration < b.worldGeneration;
    }
    if (a.objectId != b.objectId) return a.objectId < b.objectId;
    if (a.componentRuntimeTypeId != b.componentRuntimeTypeId) {
        return a.componentRuntimeTypeId < b.componentRuntimeTypeId;
    }
    return a.componentInstanceId < b.componentInstanceId;
}

// 한 축의 합법 구간과 뷰포트 길이. usable이 거짓이면 그 축은 아무 변위도
// 소비하지 않는다.
//
// measured는 그 구간이 **내용 노드를 실제로 재서** 나왔는가이다. 거짓이면
// 뷰포트만으로 세운 퇴화 구간이므로(아래 ScrollPresence::Degenerate), 저작된
// 시작 위치를 여기서 심으면 안 된다 — 심으면 잴 수 없던 프레임 하나가 그
// 한 번뿐인 seed를 0으로 태운다.
struct AxisExtent {
    Raw legalMin = 0;
    Raw legalMax = 0;
    Raw viewportExtent = 0;
    bool usable = false;
    bool measured = false;
};

// ── "이 스냅샷에 없다"는 "참조가 무너졌다"가 아니다 ─────────────────────────
// FindNode가 빗나가는 이유는 셋이고 서로 다른 사실이다. 하나로 뭉개면:
//  - 잠깐 꺼 둔 탭이 깨진 참조로 보고되고 진단 예산을 영구히 한 칸씩 먹으며,
//  - 그 프레임의 tick 하나가 저작된 시작 위치를 0으로 태워 버리고,
//  - 자기 변위로 클립 밖으로 밀려난 내용이 영원히 그 자리에 갇힌다(축이
//    꺼지므로 오프셋이 다시는 바뀌지 않고, 바뀌지 않으므로 노드가 다시
//    나타나지 않는 흡수 상태다).
enum class ScrollPresence : std::uint8_t {
    // viewport와 content가 둘 다 이 스냅샷에 있다. 유일한 정상 상태다.
    Measured,
    // content가 계층상 활성인데 이 스냅샷에 없다. 뷰포트는 절대 움직이지
    // 않으므로 여전히 있다 — 자기 변위로 클립 밖으로 밀려난 것이다. 합법
    // 구간을 [0,0]으로 두면 탄성 재귀가 오프셋을 0으로 되돌리고 내용이 다시
    // 나타난다. 기억할 상태가 없고 스스로 낫는다.
    Degenerate,
    // 서브트리가 계층상 비활성이다(숨긴 탭, 풀링된 패널). 밟지도, 심지도,
    // 보고하지도 않는다 — 저작은 온전하고 다만 지금 재지 못할 뿐이다.
    Frozen,
    // 참조가 이 세대의 살아 있는 RectTransform을 가리키지 않거나, 뷰포트가
    // 이 표면이 짓는 트리에 아예 없다. 축이 꺼지고 ReferenceInvalid가 난다.
    Unusable,
};

// ResolveScroll이 실패한 **이유**. 실패 하나를 bool로 뭉개면 잠깐 꺼 둔
// 컴포넌트와 사라진 컴포넌트가 같은 답이 되고, AdvanceTick이 살아 있는
// 사용자 오프셋을 죽은 대상의 것으로 오해해 지운다.
enum class ScrollResolution : std::uint8_t {
    Ok,
    // 대상이 더 이상 이 세대의 UIScrollView가 아니다. 그 상태는 죽은 대상의
    // 것이므로 회수한다 — 여기 하나만 회수할 자격이 있다.
    Retired,
    // 컴포넌트는 살아 있고 잠깐 꺼졌을 뿐이다. 얼린다.
    Disabled,
    // 저작된 값이 검증을 통과하지 못했다. Step 5f가 요구하는 대로 이전
    // 상태를 그대로 둔다.
    FailedClosed,
};

struct ResolvedScroll {
    UIScrollView* view = nullptr;
    unsigned int objectId = 0;
    std::uint64_t authoredRevision = 0;
    UIRuntimeTargetIdentity viewportIdentity;
    UIRuntimeTargetIdentity contentIdentity;
    AxisExtent x;
    AxisExtent y;
    ScrollPresence presence = ScrollPresence::Unusable;
    bool horizontal = false;
    bool vertical = false;
    UIScrollMovement movement = UIScrollMovement::Clamped;
    bool inertia = true;
    molga::Fixed26_6 sensitivity = molga::Fixed26_6::FromRaw(64);
    molga::Fixed26_6 deceleration = molga::Fixed26_6::FromRaw(0);
    molga::Fixed26_6 elasticity = molga::Fixed26_6::FromRaw(0);
    molga::Fixed26_6 normalizedX = molga::Fixed26_6::FromRaw(0);
    molga::Fixed26_6 normalizedY = molga::Fixed26_6::FromRaw(0);
};

const UILayoutNodeSnapshot* FindNode(const UISnapshot& snapshot,
                                     const UIRuntimeTargetIdentity& identity) {
    if (!identity) return nullptr;
    for (const auto& node : snapshot.nodes) {
        // 네 필드를 전부 본다. 숫자 id만 맞춰 보면 다른 세대의 같은 번호가
        // 같은 대상으로 읽힌다 — Step 4a가 금지하는 바로 그 재지정이다.
        if (node.rectTransform == identity) return &node;
    }
    return nullptr;
}

} // namespace

// ── Step 5c: 검증된 Q6 곱 ───────────────────────────────────────────────────
std::optional<molga::Fixed26_6> MulQ6NearestAway(molga::Fixed26_6 a,
                                                 molga::Fixed26_6 b) noexcept {
    const std::int64_t product =
        static_cast<std::int64_t>(a.Raw()) * static_cast<std::int64_t>(b.Raw());
    // 절댓값 연산은 int64로 승격한 **뒤에** 한다. int32에서 하면 INT32_MIN이
    // UB이고, 두 raw의 곱은 애초에 int32에 담기지 않는다.
    const std::int64_t magnitude = product < 0 ? -product : product;
    const std::int64_t quotient = magnitude / molga::Fixed26_6::Scale;
    const std::int64_t remainder = magnitude % molga::Fixed26_6::Scale;
    // 0에서 먼 쪽. remainder * 2 == Scale인 정확한 절반도 바깥으로 간다.
    const std::int64_t rounded =
        remainder * 2 >= molga::Fixed26_6::Scale ? quotient + 1 : quotient;
    const std::int64_t signed_ = product < 0 ? -rounded : rounded;
    if (!InRaw(signed_)) return std::nullopt;
    return molga::Fixed26_6::FromRaw(static_cast<Raw>(signed_));
}

// ── 결정적 tick ─────────────────────────────────────────────────────────────
bool UIDeterministicTickIsValid(const UIDeterministicTick& tick) noexcept {
    if (tick.tickIndex == 0) return false;
    const Raw delta = tick.deltaSeconds.Raw();
    return delta >= 1 && delta <= kUIDeterministicTickMaxDeltaRaw;
}

bool UIDeterministicTickBatchIsValid(
    const std::vector<UIDeterministicTick>& ticks) noexcept {
    if (ticks.empty()) return false;
    std::uint64_t previous = 0;
    for (const auto& tick : ticks) {
        if (!UIDeterministicTickIsValid(tick)) return false;
        if (tick.tickIndex <= previous) return false;
        previous = tick.tickIndex;
    }
    return true;
}

std::string EncodeCanonicalTicks(const std::vector<UIDeterministicTick>& ticks) {
    nlohmann::ordered_json out = nlohmann::ordered_json::array();
    for (const auto& tick : ticks) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["tickIndex"] = tick.tickIndex;
        entry["deltaSecondsRaw"] = tick.deltaSeconds.Raw();
        out.push_back(std::move(entry));
    }
    return out.dump();
}

std::vector<UIDeterministicTick> DecodeCanonicalTicks(const std::string& text) {
    std::vector<UIDeterministicTick> ticks;
    const auto parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) return {};
    for (const auto& entry : parsed) {
        if (!entry.is_object()) return {};
        const auto index = entry.find("tickIndex");
        const auto delta = entry.find("deltaSecondsRaw");
        if (index == entry.end() || delta == entry.end()) return {};
        // 부동소수 지속시간도 타임스탬프도 받지 않는다. unsigned가 아닌
        // tickIndex와 정수가 아닌 delta는 여기서 통째로 거절된다.
        if (!index->is_number_unsigned() || !delta->is_number_integer()) {
            return {};
        }
        const std::int64_t deltaRaw = delta->get<std::int64_t>();
        if (!InRaw(deltaRaw)) return {};
        UIDeterministicTick tick;
        tick.tickIndex = index->get<std::uint64_t>();
        tick.deltaSeconds = molga::Fixed26_6::FromRaw(static_cast<Raw>(deltaRaw));
        ticks.push_back(tick);
    }
    // 묶음 전체가 유효할 때만 그 묶음이다. 나쁜 원소 하나를 건너뛰고 나머지를
    // 돌려주면 리플레이가 원본보다 짧은 걸음을 걷는다.
    if (!UIDeterministicTickBatchIsValid(ticks)) return {};
    return ticks;
}

// ── 구현 ────────────────────────────────────────────────────────────────────
struct UIScrollSystem::Impl {
    struct Entry {
        UIScrollState state;
        UIRuntimeTargetIdentity viewport;
        UIRuntimeTargetIdentity content;
        // 축마다 한 번뿐인 저작 시작 위치 seed를 이미 썼는가([0]=x, [1]=y).
        // 식별자를 처음 본 순간이 아니라 **그 축을 처음 잰 순간**에 쓴다:
        // 서브트리가 아직 스냅샷에 없는 프레임에 온 tick 하나가 0으로 그
        // 한 번을 태우면, 저작된 시작 위치는 그 월드 세대 내내 돌아오지
        // 않는다. 런타임 표의 필드이며 저작 컴포넌트에는 들어가지 않는다.
        bool seeded[2] = {false, false};
    };

    std::unordered_map<UIRuntimeTargetIdentity, Entry, IdentityHash> states;
    // 검증된 산술이 실패한 식별자. 저작 revision이 바뀌거나 월드가 은퇴할
    // 때까지 그 식별자는 아무것도 발행하지 않는다.
    std::unordered_map<UIRuntimeTargetIdentity, std::uint64_t, IdentityHash>
        failClosed;
    // 월드 세대마다 마지막으로 소비한 tick 번호. 전역 하나였다면 두 월드가
    // 같은 리플레이 열을 먹을 수 없다.
    std::unordered_map<std::uint64_t, std::uint64_t> tickCursor;
    struct WorldFacts {
        std::vector<std::string> keys;
        bool announced = false;
    };
    std::unordered_map<std::uint64_t, WorldFacts> reportedFacts;
    std::vector<UIRuntimeTargetIdentity> lastVisited;

    void Report(molga::text::TextDiagnosticSink& sink,
                molga::text::TextDiagnosticCode code,
                molga::text::TextSeverity severity,
                std::uint64_t worldGeneration, unsigned int objectId,
                const std::string& message, const std::string& remediation) {
        const std::string key = ScrollFactKey(objectId, code, message);
        WorldFacts& facts = reportedFacts[worldGeneration];
        if (std::find(facts.keys.begin(), facts.keys.end(), key) !=
            facts.keys.end()) {
            return;
        }
        if (facts.keys.size() >= kMaxRememberedScrollFacts) {
            if (facts.announced) return;
            facts.announced = true;
            molga::text::TextDiagnostic notice;
            notice.code = molga::text::TextDiagnosticCode::LayoutInvalid;
            notice.severity = molga::text::TextSeverity::Warning;
            notice.subsystem = "ui.scroll";
            notice.message =
                "UI scroll diagnostics reached the " +
                std::to_string(kMaxRememberedScrollFacts) +
                " remembered-fact bound; further distinct scroll diagnostics "
                "are suppressed for this process";
            notice.remediation =
                "fix the reported scroll references and rates, then restart "
                "the session to re-arm scroll diagnostics";
            sink.Report(std::move(notice));
            return;
        }
        facts.keys.push_back(key);
        molga::text::TextDiagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.subsystem = "ui.scroll";
        diagnostic.message = message;
        diagnostic.remediation = remediation;
        diagnostic.componentType = "UIScrollView";
        diagnostic.sceneObjectId = objectId;
        sink.Report(std::move(diagnostic));
    }

    // 검증된 산술이 실패했다. 부분 상태도 감긴 상태도 발행하지 않는다.
    void FailClosed(molga::text::TextDiagnosticSink& sink,
                    const UIRuntimeTargetIdentity& target,
                    std::uint64_t authoredRevision, const std::string& what) {
        failClosed[target] = authoredRevision;
        Report(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
               molga::text::TextSeverity::Error, target.worldGeneration,
               target.objectId, what,
               "keep scroll offsets, velocities and authored rates inside the "
               "signed 26.6 range");
    }

    bool IsFailClosed(const UIRuntimeTargetIdentity& target,
                      std::uint64_t authoredRevision) {
        const auto found = failClosed.find(target);
        if (found == failClosed.end()) return false;
        // 저작이 바뀌면 다시 시도한다. 영원히 닫으면 저작자가 값을 고쳐도
        // 그 스크롤 뷰는 세션이 끝날 때까지 죽어 있다.
        if (found->second != authoredRevision) {
            failClosed.erase(found);
            return false;
        }
        return true;
    }

    std::vector<UIRuntimeTargetIdentity> SortedKeysForWorld(
        std::uint64_t worldGeneration) const {
        std::vector<UIRuntimeTargetIdentity> keys;
        keys.reserve(states.size());
        for (const auto& entry : states) {
            if (entry.first.worldGeneration != worldGeneration) continue;
            keys.push_back(entry.first);
        }
        // 해시 컨테이너를 직접 순회하면 순회 순서가 삽입 이력에 달리고, 그
        // 순서가 그대로 출력의 순서가 된다. 명시적으로 정렬한다.
        std::sort(keys.begin(), keys.end(), IdentityLess);
        return keys;
    }
};

UIScrollSystem::UIScrollSystem() : impl_(std::make_unique<Impl>()) {}
UIScrollSystem::~UIScrollSystem() = default;

UIScrollSystem& UIScrollSystem::Get() {
    // 의도적으로 파괴하지 않는다 — TextureBindingRegistry와 같은 이유다.
    //
    // 이 표는 정적 저장 수명을 가진 다른 시설이 *죽는 동안* 손을 뻗는 자리다:
    // 정적 World 하나가 종료 시점에 소멸하면 그 소멸자가
    // molga::ui::NotifyUIWorldReleased -> UISystem::OnWorldReleased -> 여기로
    // 온다. 평범한 Meyers 싱글턴이면 나중에 만들어진 이쪽이 UISystem보다 먼저
    // 죽으므로(UISystem::Get()이 이 표보다 항상 앞선다), 그 사이에 도착한
    // 알림은 이미 파괴된 unordered_map을 건드린다. Task 11.1이 정확히 이
    // 모양의 결함을 종료 시점 크래시로 배웠다.
    //
    // 종료 시점에 남는 것은 값 몇 개뿐이다. 이 표에는 GPU 자원도 외부 수명
    // 토큰도 없다.
    static UIScrollSystem* system = new UIScrollSystem();
    return *system;
}

namespace {

bool ResolveAxisExtent(const UILayoutNodeSnapshot* viewportNode,
                       const UILayoutNodeSnapshot* contentNode, bool vertical,
                       AxisExtent& out) {
    if (!viewportNode || !contentNode) return false;
    const Raw viewportLength = vertical ? viewportNode->logicalRect.height.Raw()
                                        : viewportNode->logicalRect.width.Raw();
    const Raw contentLength = vertical ? contentNode->logicalRect.height.Raw()
                                       : contentNode->logicalRect.width.Raw();
    Raw span = 0;
    if (!SubRaw(viewportLength, contentLength, span)) return false;
    out.legalMin = span < 0 ? span : 0;
    out.legalMax = 0;
    out.viewportExtent = viewportLength;
    out.usable = true;
    out.measured = true;
    return true;
}

// 내용을 재지 못한 프레임의 한 축. 뷰포트 길이만 알고 합법 구간은 [0,0]이다.
// Step 4b는 "합법 구간을 스냅샷 N에서 계산한다"고 말하고, 길이를 모르는 내용은
// 뷰포트에 들어가는 내용과 똑같이 [0,0]을 낸다 — 그래서 이것은 그 문장의
// 해석이지 예외가 아니다. 여기에 기억되는 상태는 없다.
AxisExtent DegenerateExtent(Raw viewportLength) noexcept {
    AxisExtent extent;
    extent.legalMin = 0;
    extent.legalMax = 0;
    extent.viewportExtent = viewportLength;
    extent.usable = true;
    extent.measured = false;
    return extent;
}

ScrollResolution ResolveScroll(World& world, const UISnapshot& snapshot,
                               const UIRuntimeTargetIdentity& target,
                               UIScrollSystem::Impl& impl,
                               molga::text::TextDiagnosticSink& sink,
                               ResolvedScroll& out) {
    Component* component = ResolveTarget(world, target);
    auto* view = dynamic_cast<UIScrollView*>(component);
    if (!view) {
        impl.Report(sink, molga::text::TextDiagnosticCode::ReferenceInvalid,
                    molga::text::TextSeverity::Error, target.worldGeneration,
                    target.objectId,
                    "UI scroll target no longer names a UIScrollView in this "
                    "world generation",
                    "route scroll input through a complete runtime identity "
                    "captured from the live component");
        return ScrollResolution::Retired;
    }
    if (!view->IsEnabled()) {
        // 꺼진 컴포넌트는 **살아 있다**. 사라진 것과 같은 답을 내면 그 상태가
        // 회수되고, 다시 켠 프레임에 사용자의 위치가 저작된 시작 위치로
        // 되돌아간다. 진단도 내지 않는다 — 잠깐 끄는 것은 저작이 고른 정상
        // 상태이고, Error를 내면 꺼진 뷰마다 진단 예산을 영구히 먹는다.
        return ScrollResolution::Disabled;
    }
    out.view = view;
    out.objectId = target.objectId;
    out.authoredRevision = view->AuthoredRevision();
    out.horizontal = view->Horizontal();
    out.vertical = view->Vertical();
    out.movement = view->Movement();
    out.inertia = view->Inertia();

    // ── Step 4b: 저작된 float은 정확히 한 번 변환하고 그 자리에서 검사한다 ──
    const auto sensitivity = molga::Fixed26_6::FromFloat(view->ScrollSensitivity());
    const auto deceleration =
        molga::Fixed26_6::FromFloat(view->DecelerationRate());
    const auto elasticity = molga::Fixed26_6::FromFloat(view->Elasticity());
    const auto normalizedX =
        molga::Fixed26_6::FromFloat(view->InitialNormalizedX());
    const auto normalizedY =
        molga::Fixed26_6::FromFloat(view->InitialNormalizedY());
    const bool converted = sensitivity && deceleration && elasticity &&
                           normalizedX && normalizedY;
    if (!converted) {
        impl.FailClosed(sink, target, out.authoredRevision,
                        "a UIScrollView authored rate is not a finite 26.6 "
                        "quantity");
        return ScrollResolution::FailedClosed;
    }
    const bool ranged =
        sensitivity->Raw() >= 0 && deceleration->Raw() >= 0 &&
        normalizedX->Raw() >= 0 && normalizedX->Raw() <= 64 &&
        normalizedY->Raw() >= 0 && normalizedY->Raw() <= 64 &&
        (out.movement != UIScrollMovement::Elastic || elasticity->Raw() > 0);
    if (!ranged) {
        impl.FailClosed(sink, target, out.authoredRevision,
                        "a UIScrollView authored rate left its legal range "
                        "(normalized positions in [0,1], non-negative "
                        "sensitivity/deceleration, positive elastic rate)");
        return ScrollResolution::FailedClosed;
    }
    out.sensitivity = *sensitivity;
    out.deceleration = *deceleration;
    out.elasticity = *elasticity;
    out.normalizedX = *normalizedX;
    out.normalizedY = *normalizedY;

    // ── Step 4a: viewport/content 참조를 완전한 식별자로 ────────────────────
    // 해석된 오브젝트도 함께 들고 나온다. "이 스냅샷에 없다"의 이유를 가르는
    // 사실이 그 오브젝트의 계층 활성 여부이기 때문이다.
    GameObject* viewportObject = nullptr;
    GameObject* contentObject = nullptr;
    const auto resolveRect = [&](SceneObjectRef ref, const char* what,
                                 GameObject*& objectOut)
        -> UIRuntimeTargetIdentity {
        objectOut = nullptr;
        if (!ref.IsSet()) {
            impl.Report(
                sink, molga::text::TextDiagnosticCode::ReferenceInvalid,
                molga::text::TextSeverity::Error, target.worldGeneration,
                target.objectId,
                std::string("UIScrollView has no authored ") + what +
                    " reference; scrolling is disabled for it",
                "point the UIScrollView at a RectTransform inside the same "
                "active Canvas tree");
            return {};
        }
        GameObject* object = ref.Resolve(world);
        RectTransform* rect = object ? object->GetComponent<RectTransform>()
                                     : nullptr;
        if (!rect) {
            impl.Report(
                sink, molga::text::TextDiagnosticCode::ReferenceInvalid,
                molga::text::TextSeverity::Error, target.worldGeneration,
                target.objectId,
                std::string("UIScrollView ") + what +
                    " reference is missing, replaced, or has no RectTransform",
                "point the UIScrollView at a RectTransform inside the same "
                "active Canvas tree");
            return {};
        }
        objectOut = object;
        return CaptureTarget(world, *rect);
    };

    out.viewportIdentity =
        resolveRect(view->Viewport(), "viewport", viewportObject);
    out.contentIdentity =
        resolveRect(view->Content(), "content", contentObject);

    const UILayoutNodeSnapshot* viewportNode =
        FindNode(snapshot, out.viewportIdentity);
    const UILayoutNodeSnapshot* contentNode =
        FindNode(snapshot, out.contentIdentity);

    // 세 가지 서로 다른 사실을 가른다(ScrollPresence의 주석 참조). 검사하는
    // 것은 전부 이 자리에서 실제로 확인할 수 있는 사실뿐이다: 식별자가 비었는가,
    // 노드가 이 스냅샷에 있는가, 그 오브젝트의 계층이 활성인가.
    const bool viewportBroken = !out.viewportIdentity;
    const bool contentBroken = !out.contentIdentity;
    if (viewportBroken || contentBroken) {
        // resolveRect가 이미 정확한 사실을 보고했다. 여기서 두 번째 진단을
        // 내면 같은 사실이 두 문구로 예산을 두 칸 먹는다.
        out.presence = ScrollPresence::Unusable;
    } else if ((!viewportNode && !IsHierarchyActive(viewportObject)) ||
               (!contentNode && !IsHierarchyActive(contentObject))) {
        out.presence = ScrollPresence::Frozen;
        return ScrollResolution::Ok;
    } else if (viewportNode && contentNode) {
        out.presence = ScrollPresence::Measured;
    } else if (viewportNode) {
        // 뷰포트는 있는데 내용만 없다. 뷰포트는 변위를 받지 않으므로 이것은
        // 내용이 자기 오프셋으로 클립 밖까지 밀려난 경우다.
        out.presence = ScrollPresence::Degenerate;
    } else {
        // 뷰포트 자체가 활성인데도 이 표면의 트리에 없다. 참조는 이 표면이
        // 짓지 않는 무언가를 가리키고 있다 — 그것은 진짜 잘못된 참조다.
        out.presence = ScrollPresence::Unusable;
        impl.Report(sink, molga::text::TextDiagnosticCode::ReferenceInvalid,
                    molga::text::TextSeverity::Error, target.worldGeneration,
                    target.objectId,
                    "UIScrollView viewport is not laid out in this snapshot; "
                    "scrolling is disabled for it",
                    "keep the viewport and content inside the active Canvas "
                    "tree this surface builds");
    }

    if (out.presence == ScrollPresence::Measured) {
        if (!ResolveAxisExtent(viewportNode, contentNode, false, out.x)) {
            out.x = AxisExtent{};
        }
        if (!ResolveAxisExtent(viewportNode, contentNode, true, out.y)) {
            out.y = AxisExtent{};
        }
    } else if (out.presence == ScrollPresence::Degenerate) {
        out.x = DegenerateExtent(viewportNode->logicalRect.width.Raw());
        out.y = DegenerateExtent(viewportNode->logicalRect.height.Raw());
    } else {
        out.x = AxisExtent{};
        out.y = AxisExtent{};
    }
    // 참조가 무너진 축은 저작이 켜 두었더라도 아무 변위도 소비하지 않는다.
    out.horizontal = out.horizontal && out.x.usable;
    out.vertical = out.vertical && out.y.usable;
    return ScrollResolution::Ok;
}

// Step 4b: 저작된 정규화 위치에서 시작 오프셋을 딱 한 번 만든다.
bool InitialOffset(const AxisExtent& extent, molga::Fixed26_6 normalized,
                   Raw& out) {
    Raw span = 0;
    if (!SubRaw(extent.legalMin, extent.legalMax, span)) return false;
    const auto scaled = MulQ6NearestAway(molga::Fixed26_6::FromRaw(span),
                                         normalized);
    if (!scaled) return false;
    return AddRaw(extent.legalMax, scaled->Raw(), out);
}

// ── Step 5e: 한 축의 정확한 재귀 ────────────────────────────────────────────
bool StepAxis(const ResolvedScroll& config, const AxisExtent& extent,
              molga::Fixed26_6 deltaSeconds, Raw& offset, Raw& velocity) {
    const auto q6 = [](Raw a, Raw b, Raw& out) {
        const auto result = MulQ6NearestAway(molga::Fixed26_6::FromRaw(a),
                                             molga::Fixed26_6::FromRaw(b));
        if (!result) return false;
        out = result->Raw();
        return true;
    };
    const Raw delta = deltaSeconds.Raw();

    Raw travelled = 0;
    if (!q6(velocity, delta, travelled)) return false;
    Raw integrated = 0;
    if (!AddRaw(offset, travelled, integrated)) return false;

    Raw decayStep = 0;
    if (!q6(config.deceleration.Raw(), delta, decayStep)) return false;
    decayStep = ClampRaw(decayStep, 0, molga::Fixed26_6::Scale);
    const Raw retained = static_cast<Raw>(molga::Fixed26_6::Scale) - decayStep;
    Raw nextVelocity = 0;
    if (!q6(velocity, retained, nextVelocity)) return false;

    Raw nextOffset = 0;
    if (config.movement == UIScrollMovement::Clamped) {
        nextOffset = ClampRaw(integrated, extent.legalMin, extent.legalMax);
        // 경계에서 바깥으로 향하는 속도는 죽인다. 남겨 두면 다음 tick이 같은
        // 경계를 다시 밀어 상태가 영원히 "변했다"고 보고한다.
        if (nextOffset == extent.legalMin && nextVelocity < 0) nextVelocity = 0;
        if (nextOffset == extent.legalMax && nextVelocity > 0) nextVelocity = 0;
    } else {
        const Raw half = extent.viewportExtent / 2;
        const Raw overscrollLimit =
            half > molga::Fixed26_6::Scale
                ? half
                : static_cast<Raw>(molga::Fixed26_6::Scale);
        Raw low = 0;
        Raw high = 0;
        if (!SubRaw(extent.legalMin, overscrollLimit, low) ||
            !AddRaw(extent.legalMax, overscrollLimit, high)) {
            return false;
        }
        const Raw bounded = ClampRaw(integrated, low, high);
        const Raw legal = ClampRaw(bounded, extent.legalMin, extent.legalMax);
        Raw overscroll = 0;
        if (!SubRaw(bounded, legal, overscroll)) return false;
        Raw returnStep = 0;
        if (!q6(config.elasticity.Raw(), delta, returnStep)) return false;
        returnStep = ClampRaw(returnStep, 0, molga::Fixed26_6::Scale);
        Raw correction = 0;
        if (!q6(overscroll, returnStep, correction)) return false;
        if (!SubRaw(bounded, correction, nextOffset)) return false;
        // 마지막 1 raw는 재귀로 절대 닫히지 않는다(곱이 0으로 반올림된다).
        // 여기서 붙이지 않으면 정지한 스크롤이 영원히 "변했다"를 보고한다.
        Raw settled = ClampRaw(nextOffset, extent.legalMin, extent.legalMax);
        Raw residual = 0;
        if (!SubRaw(nextOffset, settled, residual)) return false;
        if (AbsRaw(residual) <= 1 && AbsRaw(nextVelocity) <= 1) {
            nextOffset = settled;
            nextVelocity = 0;
        }
    }
    offset = nextOffset;
    velocity = nextVelocity;
    return true;
}

} // namespace

const UIScrollState* UIScrollSystem::State(
    const UIRuntimeTargetIdentity& target) const {
    const auto found = impl_->states.find(target);
    return found == impl_->states.end() ? nullptr : &found->second.state;
}

void UIScrollSystem::SeedStateForTesting(const UIRuntimeTargetIdentity& target,
                                         const UIScrollState& state) {
    Impl::Entry& entry = impl_->states[target];
    entry.state = state;
    // 심어 둔 상태는 **이미 존재하는 상태**다. 저작된 시작 위치는 상태가 없는
    // 식별자에만 닿으므로, 여기서 seed를 소진해 두지 않으면 다음 해석이 손으로
    // 세운 시작점을 저작 위치로 덮어쓴다.
    entry.seeded[0] = true;
    entry.seeded[1] = true;
}

void UIScrollSystem::OnWorldReleased(std::uint64_t worldGeneration) {
    if (worldGeneration == 0) return;
    const auto sameWorld = [worldGeneration](const auto& entry) {
        return entry.first.worldGeneration == worldGeneration;
    };
    for (auto it = impl_->states.begin(); it != impl_->states.end();) {
        it = sameWorld(*it) ? impl_->states.erase(it) : std::next(it);
    }
    for (auto it = impl_->failClosed.begin(); it != impl_->failClosed.end();) {
        it = sameWorld(*it) ? impl_->failClosed.erase(it) : std::next(it);
    }
    impl_->tickCursor.erase(worldGeneration);
    // 기억된 진단 사실은 그 월드의 것이다. 들고 있으면 그 월드의 256칸이
    // 죽은 채로 영원히 남는다.
    impl_->reportedFacts.erase(worldGeneration);
    impl_->lastVisited.erase(
        std::remove_if(impl_->lastVisited.begin(), impl_->lastVisited.end(),
                       [worldGeneration](const UIRuntimeTargetIdentity& id) {
                           return id.worldGeneration == worldGeneration;
                       }),
        impl_->lastVisited.end());
}

std::size_t UIScrollSystem::StateCount() const noexcept {
    return impl_->states.size();
}

std::size_t UIScrollSystem::StateCountForWorld(
    std::uint64_t worldGeneration) const noexcept {
    std::size_t total = 0;
    for (const auto& entry : impl_->states) {
        if (entry.first.worldGeneration == worldGeneration) ++total;
    }
    return total;
}

std::uint64_t UIScrollSystem::LastTickIndexForWorld(
    std::uint64_t worldGeneration) const noexcept {
    const auto found = impl_->tickCursor.find(worldGeneration);
    return found == impl_->tickCursor.end() ? 0 : found->second;
}

bool UIScrollSystem::IsFailClosed(
    const UIRuntimeTargetIdentity& target) const noexcept {
    return impl_->failClosed.find(target) != impl_->failClosed.end();
}

std::vector<UIRuntimeTargetIdentity>
UIScrollSystem::LastVisitedIdentitiesForTesting() const {
    return impl_->lastVisited;
}

std::vector<UIScrollDisplacementCacheIdentity>
UIScrollSystem::DisplacementsForWorld(std::uint64_t worldGeneration) const {
    std::vector<UIScrollDisplacementCacheIdentity> out;
    if (worldGeneration == 0) return out;
    for (const auto& key : impl_->SortedKeysForWorld(worldGeneration)) {
        const auto& entry = impl_->states.at(key);
        UIScrollDisplacementCacheIdentity identity;
        identity.scrollTarget = key;
        identity.offsetXRaw = entry.state.offset.x.Raw();
        identity.offsetYRaw = entry.state.offset.y.Raw();
        identity.viewport = entry.viewport;
        identity.content = entry.content;
        out.push_back(identity);
    }
    return out;
}

std::string UIScrollSystem::StableStateJson(
    std::uint64_t worldGeneration) const {
    nlohmann::ordered_json out = nlohmann::ordered_json::array();
    for (const auto& key : impl_->SortedKeysForWorld(worldGeneration)) {
        const auto& entry = impl_->states.at(key);
        nlohmann::ordered_json record = nlohmann::ordered_json::object();
        // 프로세스 순번은 나가지 않는다. worldGeneration과 componentInstanceId를
        // 넣으면 독립적으로 만든 두 실행이 절대 같은 바이트를 낼 수 없어
        // 결정성 비교 자체가 성립하지 않는다.
        record["objectId"] = key.objectId;
        record["offsetXRaw"] = entry.state.offset.x.Raw();
        record["offsetYRaw"] = entry.state.offset.y.Raw();
        record["velocityXRaw"] = entry.state.velocity.x.Raw();
        record["velocityYRaw"] = entry.state.velocity.y.Raw();
        record["viewportObjectId"] = entry.viewport.objectId;
        record["contentObjectId"] = entry.content.objectId;
        out.push_back(std::move(record));
    }
    return out.dump();
}

namespace {

// Step 6b: 바뀐 오프셋을 발행하기 **전에** 스크롤 변위 축을 올린다. 소진되면
// 이전 오프셋이 그대로 보이고 캐시가 통째로 우회된다 — 올리지 않고 발행하면
// warm 빠른 경로의 도장이 새 오프셋에 옛 이름을 붙인다.
bool AcquireScrollDisplacement(UIScrollSystem::Impl& impl,
                               molga::text::TextDiagnosticSink& sink,
                               std::uint64_t worldGeneration,
                               unsigned int objectId) {
    if (UIRuntimeInvalidationClock::Advance(
            UIRuntimeGenerationKind::ScrollDisplacement)) {
        return true;
    }
    impl.Report(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
                molga::text::TextSeverity::Blocker, worldGeneration, objectId,
                "the UI scroll displacement generation is exhausted; this "
                "offset is not published and snapshot caching is bypassed",
                "restart the process to reset the runtime generation clocks");
    return false;
}

// 저작된 정규화 위치를 그 축에서 딱 한 번 실현한다. **식별자를 처음 본
// 프레임이 아니라 그 축을 처음 잰 프레임**에 심는다 — 서브트리가 아직
// 스냅샷에 없는 프레임에 온 tick 하나가 0으로 seed를 태우면 저작된 시작
// 위치는 그 월드 세대 내내 돌아오지 않기 때문이다. 이미 심은 축은 다시
// 건드리지 않는다(매 프레임 다시 심으면 저작 위치가 사용자의 스크롤을
// 되돌린다).
//
// seededOffset은 이 호출이 오프셋을 실제로 **움직였는가**이다. 호출부가
// 그 tick의 더럽힘 플래그에 접어 넣는다: seed는 다음 Build에서 content
// 서브트리를 옮기는 진짜 변위이므로, 아무것도 더럽히지 않았다고 보고하면
// arrangementDirty로 재빌드를 거르는 orchestrator가 그 프레임을 통째로
// 건너뛴다.
bool EnsureState(UIScrollSystem::Impl& impl, const ResolvedScroll& config,
                 const UIRuntimeTargetIdentity& target,
                 molga::text::TextDiagnosticSink& sink, bool& seededOffset) {
    seededOffset = false;
    // 퇴화 구간에서는 심지 않는다. usable하지만 measured가 아닌 축은 내용
    // 길이를 모르는 축이고, 거기서 유도한 시작 위치는 언제나 0이다.
    const auto seedable = [](const AxisExtent& extent) {
        return extent.usable && extent.measured;
    };
    auto found = impl.states.find(target);
    const bool exists = found != impl.states.end();
    UIScrollSystem::Impl::Entry fresh;
    UIScrollSystem::Impl::Entry& entry = exists ? found->second : fresh;

    const Raw previousX = entry.state.offset.x.Raw();
    const Raw previousY = entry.state.offset.y.Raw();
    Raw x = previousX;
    Raw y = previousY;
    const bool seedX = !entry.seeded[0] && seedable(config.x);
    const bool seedY = !entry.seeded[1] && seedable(config.y);
    if ((seedX && !InitialOffset(config.x, config.normalizedX, x)) ||
        (seedY && !InitialOffset(config.y, config.normalizedY, y))) {
        impl.FailClosed(sink, target, config.authoredRevision,
                        "a UIScrollView initial normalized position left the "
                        "signed 26.6 range");
        return false;
    }
    if (x != previousX || y != previousY) {
        // 저작된 시작 위치가 0이 아니면 그것도 발행되는 변위다. 여기서만은
        // 취득 실패가 발행을 막지 않는다: 되돌아갈 "이전 오프셋"이 없고,
        // 소진된 시계는 이미 cacheable을 내려 두 캐시를 통째로 우회시키므로
        // warm 도장이 이 값에 옛 이름을 붙일 경로 자체가 없다. 차단 진단은
        // 그대로 나간다.
        AcquireScrollDisplacement(impl, sink, target.worldGeneration,
                                  target.objectId);
        seededOffset = true;
    }
    if (seedX) entry.seeded[0] = true;
    if (seedY) entry.seeded[1] = true;
    entry.state.offset = molga::FixedPoint{molga::Fixed26_6::FromRaw(x),
                                           molga::Fixed26_6::FromRaw(y)};
    entry.viewport = config.viewportIdentity;
    entry.content = config.contentIdentity;
    if (!exists) impl.states.emplace(target, entry);
    return true;
}

} // namespace

UIScrollMutation UIScrollSystem::ApplyInput(
    World& world, const UISnapshot& snapshot,
    const UIRuntimeTargetIdentity& target, const UIScrollInput& input,
    molga::text::TextDiagnosticSink& sink) {
    UIScrollMutation mutation;
    Impl& impl = *impl_;
    if (!target || target.worldGeneration != world.Generation()) {
        impl.Report(sink, molga::text::TextDiagnosticCode::ReferenceInvalid,
                    molga::text::TextSeverity::Error, world.Generation(),
                    target.objectId,
                    "UI scroll input names an identity from another world "
                    "generation",
                    "capture the scroll identity from the live world before "
                    "routing input");
        return mutation;
    }
    ResolvedScroll config;
    if (ResolveScroll(world, snapshot, target, impl, sink, config) !=
        ScrollResolution::Ok) {
        return mutation;
    }
    // 계층이 비활성인 서브트리는 이 스냅샷에서 잴 수 없다. 밟지도 심지도
    // 않는다 — 이전 변위는 그대로 발행된 채로 남는다.
    if (config.presence == ScrollPresence::Frozen) return mutation;
    if (impl.IsFailClosed(target, config.authoredRevision)) return mutation;
    bool seededOffset = false;
    if (!EnsureState(impl, config, target, sink, seededOffset)) return mutation;

    const UIScrollState previous = impl.states.at(target).state;
    UIScrollState next = previous;

    // ── Step 5a: 부호 있는 변위와 이름 붙은 축 ──────────────────────────────
    const auto applyAxis = [&](bool enabled, const AxisExtent& extent,
                               Raw logicalDelta, bool named, Raw axisValue,
                               molga::Fixed26_6& offset,
                               molga::Fixed26_6& velocity) -> bool {
        // 꺼진 축은 변위를 **소비하지 않는다**. 여기서 값을 읽어 버리면
        // "소비하고 버림"이 되어, 축을 다시 켠 프레임에 이미 사라진 변위가
        // 되돌아오지 않는다.
        if (!enabled) return true;
        Raw combined = logicalDelta;
        if (named && !AddRaw(combined, axisValue, combined)) return false;
        Raw scaled = 0;
        const auto product =
            MulQ6NearestAway(molga::Fixed26_6::FromRaw(combined),
                             config.sensitivity);
        if (!product) return false;
        scaled = product->Raw();
        Raw nextOffset = 0;
        if (!AddRaw(offset.Raw(), scaled, nextOffset)) return false;
        Raw nextVelocity = 0;
        if (config.inertia) {
            // 속도 자극은 변위와 같은 값이다. "변위 / 경과 시간"이었다면 UI가
            // 벽시계 간격을 몰래 표본화해야 한다.
            if (!AddRaw(velocity.Raw(), scaled, nextVelocity)) return false;
        }
        if (config.movement == UIScrollMovement::Clamped) {
            // Step 5b: 합법 구간으로 즉시 자르고 경계 바깥으로 향하는 속도를
            // 죽인다.
            nextOffset = ClampRaw(nextOffset, extent.legalMin, extent.legalMax);
            if (nextOffset == extent.legalMin && nextVelocity < 0) {
                nextVelocity = 0;
            }
            if (nextOffset == extent.legalMax && nextVelocity > 0) {
                nextVelocity = 0;
            }
        } else {
            // 탄성은 자르지 않고 되돌린다. 다만 Step 5e가 tick마다 쓰는 그
            // 과주행 한계로 묶는다 — 묶지 않으면 한 번의 플링이 오프셋을
            // 26.6 범위 밖으로 밀어 다음 tick이 통째로 fail-closed가 된다.
            const Raw half = extent.viewportExtent / 2;
            const Raw overscrollLimit =
                half > molga::Fixed26_6::Scale
                    ? half
                    : static_cast<Raw>(molga::Fixed26_6::Scale);
            Raw low = 0;
            Raw high = 0;
            if (!SubRaw(extent.legalMin, overscrollLimit, low) ||
                !AddRaw(extent.legalMax, overscrollLimit, high)) {
                return false;
            }
            nextOffset = ClampRaw(nextOffset, low, high);
        }
        offset = molga::Fixed26_6::FromRaw(nextOffset);
        velocity = molga::Fixed26_6::FromRaw(nextVelocity);
        return true;
    };

    const bool ok =
        applyAxis(config.horizontal, config.x, input.logicalDelta.x.Raw(),
                  input.axis == UIScrollAxis::Horizontal, input.axisValue.Raw(),
                  next.offset.x, next.velocity.x) &&
        applyAxis(config.vertical, config.y, input.logicalDelta.y.Raw(),
                  input.axis == UIScrollAxis::Vertical, input.axisValue.Raw(),
                  next.offset.y, next.velocity.y);
    if (!ok) {
        impl.FailClosed(sink, target, config.authoredRevision,
                        "a UI scroll input left the signed 26.6 range; the "
                        "prior offset and velocity stay published");
        return mutation;
    }

    const bool offsetChanged = next.offset != previous.offset;
    if (offsetChanged &&
        !AcquireScrollDisplacement(impl, sink, target.worldGeneration,
                                   target.objectId)) {
        return mutation;
    }
    impl.states.at(target).state = next;
    // 저작된 시작 위치를 실현한 것도 이 호출이 옮긴 변위다. 여기서 빼면 이
    // 입력이 아무것도 더럽히지 않았다고 보고되고, arrangementDirty로 재빌드를
    // 거르는 Task 12의 orchestrator가 그 프레임을 통째로 건너뛴다.
    const bool dirtied = offsetChanged || seededOffset;
    mutation.changed = dirtied || next.velocity != previous.velocity;
    mutation.arrangementDirty = dirtied;
    mutation.renderDirty = dirtied;
    mutation.textShapeDirty = false;
    return mutation;
}

UIScrollMutation UIScrollSystem::AdvanceTick(
    World& world, const UISnapshot& snapshot, const UIDeterministicTick& tick,
    molga::text::TextDiagnosticSink& sink) {
    UIScrollMutation mutation;
    Impl& impl = *impl_;
    const std::uint64_t worldGeneration = world.Generation();
    impl.lastVisited.erase(
        std::remove_if(impl.lastVisited.begin(), impl.lastVisited.end(),
                       [worldGeneration](const UIRuntimeTargetIdentity& id) {
                           return id.worldGeneration == worldGeneration;
                       }),
        impl.lastVisited.end());

    if (!UIDeterministicTickIsValid(tick)) {
        impl.Report(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
                    molga::text::TextSeverity::Error, worldGeneration, 0,
                    "a UI deterministic tick is not a nonzero index with a "
                    "1..64 raw second delta",
                    "feed the frame's validated UIDeterministicTick stream; "
                    "never sample a wall clock inside a UI subsystem");
        return mutation;
    }
    std::uint64_t& cursor = impl.tickCursor[worldGeneration];
    if (tick.tickIndex <= cursor) {
        impl.Report(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
                    molga::text::TextSeverity::Error, worldGeneration, 0,
                    "a UI deterministic tick index did not increase; the tick "
                    "is not consumed twice",
                    "feed each frame's tick exactly once, in strictly "
                    "increasing index order");
        return mutation;
    }
    cursor = tick.tickIndex;

    // ── Step 4b: 아직 상태가 없는 저작된 스크롤 뷰를 발견한다 ───────────────
    // 발견이 없으면 저작된 initialNormalizedX/Y는 사용자가 스크롤하기 전까지
    // 화면에 절대 닿지 않는다 — 소비자만 있고 생산자가 없는 seam이고, 이
    // 프로그램이 이미 두 번 만난 결함 모양이다. 순회는 World의 오브젝트
    // 순서이므로 결정적이고, 이미 상태가 있는 식별자는 손대지 않는다.
    bool anySeeded = false;
    for (const auto& object : world.Objects()) {
        if (!object) continue;
        auto* view = object->GetComponent<UIScrollView>();
        if (!view || !view->IsEnabled()) continue;
        const UIRuntimeTargetIdentity identity = CaptureTarget(world, *view);
        if (!identity || impl.states.count(identity) != 0) continue;
        ResolvedScroll discovered;
        if (ResolveScroll(world, snapshot, identity, impl, sink, discovered) !=
            ScrollResolution::Ok) {
            continue;
        }
        // 서브트리가 이 스냅샷에 없는 동안에는 상태를 만들지 않는다. 만들면
        // 그 항목의 한 번뿐인 seed가 0으로 소진되고, 패널을 켠 뒤에는 이미
        // 존재하는 항목이라 저작된 시작 위치가 다시는 적용되지 않는다.
        if (discovered.presence == ScrollPresence::Frozen) continue;
        if (impl.IsFailClosed(identity, discovered.authoredRevision)) continue;
        bool discoverySeeded = false;
        EnsureState(impl, discovered, identity, sink, discoverySeeded);
        anySeeded = anySeeded || discoverySeeded;
    }

    // ── Step 5d: 완전한 식별자 순서로만 순회한다 ────────────────────────────
    const std::vector<UIRuntimeTargetIdentity> keys =
        impl.SortedKeysForWorld(worldGeneration);
    bool anyOffsetChanged = false;
    bool anyChanged = false;
    struct Pending {
        UIRuntimeTargetIdentity target;
        UIScrollState state;
        UIRuntimeTargetIdentity viewport;
        UIRuntimeTargetIdentity content;
        bool offsetChanged = false;
    };
    std::vector<Pending> pending;
    pending.reserve(keys.size());
    std::vector<UIRuntimeTargetIdentity> retired;

    for (const auto& key : keys) {
        impl.lastVisited.push_back(key);
        ResolvedScroll config;
        const ScrollResolution resolution =
            ResolveScroll(world, snapshot, key, impl, sink, config);
        if (resolution == ScrollResolution::Retired) {
            // 컴포넌트가 교체되었거나 사라졌다. 그 상태는 죽은 대상의 것이다.
            // **이것 하나만** 회수할 자격이 있다.
            retired.push_back(key);
            continue;
        }
        if (resolution != ScrollResolution::Ok) {
            // 잠깐 꺼졌거나(Disabled) 저작된 값이 범위를 벗어났다
            // (FailedClosed). 둘 다 대상이 **살아 있는** 경우이므로 상태를
            // 지우지 않는다: Step 5f는 검증 실패가 "이전 상태를 그대로 둔다"고
            // 말하고, 지우면 다시 켜거나 값을 고친 프레임에 사용자의 위치가
            // 저작된 시작 위치로 되돌아간다. ApplyInput도 같은 판정을 지나므로
            // 두 입구가 같은 거절을 다르게 읽을 수 없다.
            continue;
        }
        if (config.presence == ScrollPresence::Frozen) continue;
        if (impl.IsFailClosed(key, config.authoredRevision)) continue;
        bool seededOffset = false;
        if (!EnsureState(impl, config, key, sink, seededOffset)) continue;
        anySeeded = anySeeded || seededOffset;
        const UIScrollState previous = impl.states.at(key).state;
        UIScrollState next = previous;
        Raw offsetX = next.offset.x.Raw();
        Raw velocityX = next.velocity.x.Raw();
        Raw offsetY = next.offset.y.Raw();
        Raw velocityY = next.velocity.y.Raw();
        bool ok = true;
        if (config.horizontal) {
            ok = StepAxis(config, config.x, tick.deltaSeconds, offsetX,
                          velocityX);
        }
        if (ok && config.vertical) {
            ok = StepAxis(config, config.y, tick.deltaSeconds, offsetY,
                          velocityY);
        }
        if (!ok) {
            impl.FailClosed(sink, key, config.authoredRevision,
                            "a UI scroll step left the signed 26.6 range; the "
                            "prior offset and velocity stay published");
            continue;
        }
        next.offset = molga::FixedPoint{molga::Fixed26_6::FromRaw(offsetX),
                                        molga::Fixed26_6::FromRaw(offsetY)};
        next.velocity = molga::FixedPoint{molga::Fixed26_6::FromRaw(velocityX),
                                          molga::Fixed26_6::FromRaw(velocityY)};
        Pending record;
        record.target = key;
        record.state = next;
        record.viewport = config.viewportIdentity;
        record.content = config.contentIdentity;
        record.offsetChanged = next.offset != previous.offset;
        anyOffsetChanged = anyOffsetChanged || record.offsetChanged;
        anyChanged = anyChanged || record.offsetChanged ||
                     next.velocity != previous.velocity;
        pending.push_back(record);
    }

    for (const auto& key : retired) impl.states.erase(key);

    if (anyOffsetChanged &&
        !AcquireScrollDisplacement(impl, sink, worldGeneration, 0)) {
        // 이전 오프셋이 그대로 보인다. 부분 발행은 하지 않는다 — 절반만
        // 새 오프셋인 프레임은 어떤 캐시 이름으로도 정확히 가리켜지지 않는다.
        return mutation;
    }
    for (const auto& record : pending) {
        auto& entry = impl.states.at(record.target);
        entry.state = record.state;
        entry.viewport = record.viewport;
        entry.content = record.content;
    }
    // 저작된 시작 위치를 실현한 tick도 content 서브트리를 옮긴 tick이다.
    // 그 tick이 아무것도 더럽히지 않았다고 보고하면, arrangementDirty로
    // 재빌드를 거르는 Task 12의 orchestrator가 그 프레임을 건너뛴다.
    const bool dirtied = anyOffsetChanged || anySeeded;
    mutation.changed = anyChanged || anySeeded;
    mutation.arrangementDirty = dirtied;
    mutation.renderDirty = dirtied;
    mutation.textShapeDirty = false;
    return mutation;
}

} // namespace molga::ui
