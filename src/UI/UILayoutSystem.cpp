#include "UI/UILayoutSystem.h"

#include "Common/Log.h"

#include "Core/World.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIAccessibility.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIContentSizeFitter.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Components/UILayoutElement.h"
#include "ECS/Components/UILayoutGroup.h"
#include "ECS/Components/UIMask.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/Components/UISelectable.h"
#include "ECS/Components/UITextInput.h"
#include "ECS/GameObject.h"
#include "Rendering/TextureBindingRegistry.h"
#include "Text/TextLayoutService.h"
#include "UI/UIHierarchy.h"
#include "UI/UILayoutTypes.h"
#include "UI/UIRuntimeInvalidation.h"
#include "UI/UIScrollSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <variant>

namespace molga::ui {

namespace {

using molga::Fixed26_6;
using molga::FixedRect;
using molga::FixedSize;

// 배치 산술은 26.6 raw 정수로만 한다. 중간값을 64bit로 들고 다니는 것은
// 포화나 랩어라운드를 허용하려는 것이 아니라 그 반대다: 32bit 안에서 더하면
// 넘침이 조용히 랩되지만, 64bit 중간값은 마지막에 int32 범위를 명시적으로
// 확인할 수 있다. 범위를 벗어나면 그 프레임은 스냅샷 없이 실패한다.
using Raw = std::int64_t;
constexpr Raw kRawMin = std::numeric_limits<std::int32_t>::min();
constexpr Raw kRawMax = std::numeric_limits<std::int32_t>::max();
constexpr std::size_t kNoNode = static_cast<std::size_t>(-1);

bool InRawRange(Raw value) noexcept {
    return value >= kRawMin && value <= kRawMax;
}

Fixed26_6 ToFixed(Raw value) noexcept {
    return Fixed26_6::FromRaw(static_cast<std::int32_t>(value));
}

struct RawRect {
    Raw x = 0, y = 0, width = 0, height = 0;
};

// 확정된 raw 사각형 하나를 26.6 값으로. 범위를 벗어나면 nullopt다 —
// static_cast로 잘라 내면 잘못된 사각형이 정상으로 보이고, 그것이 바로
// "포화도 랩도 없다"는 제약이 금지하는 일이다.
std::optional<molga::FixedRect> ToFixedRect(const RawRect& rect) noexcept {
    if (!InRawRange(rect.x) || !InRawRange(rect.y) || !InRawRange(rect.width) ||
        !InRawRange(rect.height)) {
        return std::nullopt;
    }
    return molga::FixedRect{ToFixed(rect.x), ToFixed(rect.y),
                            ToFixed(rect.width), ToFixed(rect.height)};
}

// 저작된 float 하나를 26.6 raw로. 유한하지 않거나 범위를 벗어나면 실패다 —
// 그런 값을 0으로 대체하면 잘못된 배치가 정상으로 보인다.
bool AuthoredToRaw(float value, Raw& out) {
    const auto fixed = Fixed26_6::FromFloat(value);
    if (!fixed) return false;
    out = fixed->Raw();
    return true;
}

// (value * numerator) / denominator. Fixed26_6::CheckedMulDiv이 0에서 먼 쪽
// 반올림과 넘침 검사를 한 곳에서 정의하므로 그것을 그대로 쓴다.
bool MulDiv(Raw value, Raw numerator, Raw denominator, Raw& out) {
    if (!InRawRange(value)) return false;
    const auto result =
        Fixed26_6::CheckedMulDiv(ToFixed(value), numerator, denominator);
    if (!result) return false;
    out = result->Raw();
    return true;
}

// ── 노드 하나 ───────────────────────────────────────────────────────────────
struct LayoutNode {
    GameObject* object = nullptr;
    RectTransform* rect = nullptr;
    UICanvas* canvas = nullptr;
    UILayoutGroup* group = nullptr;
    UILayoutElement* element = nullptr;
    UIContentSizeFitter* fitter = nullptr;
    UIMask* mask = nullptr;
    std::size_t parent = kNoNode;
    std::vector<std::size_t> children;       // 활성 자식 노드, 형제 순서
    std::vector<std::size_t> layoutChildren; // 그 중 부모 레이아웃에 참여하는 것
    UIDrawOrderKey drawOrder;
    bool canvasRoot = false;
    Raw minWidth = 0, minHeight = 0;
    Raw prefWidth = 0, prefHeight = 0;
    Raw intrinsicWidth = 0, intrinsicHeight = 0;
    std::uint64_t intrinsicGeneration = 0;
    std::string intrinsicIdentity;
    RawRect resolved;
    bool cyclicWidth = false;
    bool cyclicHeight = false;
    // 조상 마스크와의 교집합. 비어 있는 교집합은 노드를 통째로 제거한다 —
    // 렌더에서만 사라지고 hit 대상으로 남으면 보이지 않는 버튼이 생긴다.
    //
    // Step 4b가 요구한 그대로 optional<FixedRect>다. bool + RawRect 쌍이었을
    // 때는 게시되는 클립이 마지막에 bare static_cast<int32_t>로 좁혀졌고, 그
    // 좁힘이 곧 Global Constraint가 금지하는 조용한 랩이었다.
    std::optional<molga::FixedRect> logicalClip;
    bool dropped = false;
};

const UICanvas* NearestEnabledCanvas(const GameObject* object) {
    for (const GameObject* node = object; node; node = node->GetParent()) {
        if (const auto* canvas = node->GetComponent<UICanvas>()) {
            if (canvas->IsEnabled()) return canvas;
        }
    }
    return nullptr;
}

// 활성 UI Canvas 서브트리의 뿌리인가. 조상에 이미 켜진 Canvas가 있으면
// 중첩 Canvas일 뿐 뿌리가 아니다.
bool IsCanvasRoot(const GameObject& object) {
    const auto* canvas = object.GetComponent<UICanvas>();
    if (!canvas || !canvas->IsEnabled()) return false;
    return NearestEnabledCanvas(object.GetParent()) == nullptr;
}

template <typename T>
T* EnabledComponent(GameObject& object) {
    T* component = object.GetComponent<T>();
    return (component && component->IsEnabled()) ? component : nullptr;
}

std::string CanonicalPayload(const Component& component) {
    nlohmann::json payload = nlohmann::json::object();
    component.Serialize(payload);
    return payload.dump();
}

} // namespace

// ── Step 5c ─────────────────────────────────────────────────────────────────
UILayoutDriver ResolveDriver(bool canvasRoot, bool parentDrives,
                             bool fitterDrives) noexcept {
    if (canvasRoot) return UILayoutDriver::Canvas;
    if (parentDrives) return UILayoutDriver::ParentGroup;
    if (fitterDrives) return UILayoutDriver::SelfFitter;
    return UILayoutDriver::AuthoredRect;
}

// ── 등록부 ──────────────────────────────────────────────────────────────────
UIIntrinsicLayoutRegistry& UIIntrinsicLayoutRegistry::Get() {
    static UIIntrinsicLayoutRegistry registry;
    return registry;
}

bool UIIntrinsicLayoutRegistry::Publish(const UIRuntimeTargetIdentity& identity,
                                        std::string contentIdentity,
                                        molga::FixedSize intrinsicSize) {
    if (!identity) return false;
    auto& bucket = byWorld_[identity.worldGeneration];
    for (auto& entry : bucket) {
        if (entry.first != identity) continue;
        // 같은 내용을 다시 게시하는 것은 교체가 아니다. 세대를 올리면 매
        // 프레임 재게시하는 소비자가 캐시를 통째로 무력화한다.
        if (entry.second.contentIdentity == contentIdentity &&
            entry.second.intrinsicSize == intrinsicSize) {
            return false;
        }
        entry.second.contentIdentity = std::move(contentIdentity);
        entry.second.intrinsicSize = intrinsicSize;
        entry.second.generation = ++nextGeneration_;
        NotifyUISemanticMutation();
        return true;
    }
    UIIntrinsicLayoutRecord record;
    record.contentIdentity = std::move(contentIdentity);
    record.intrinsicSize = intrinsicSize;
    record.generation = ++nextGeneration_;
    bucket.emplace_back(identity, std::move(record));
    NotifyUISemanticMutation();
    return true;
}

std::optional<UIIntrinsicLayoutRecord> UIIntrinsicLayoutRegistry::Find(
    const UIRuntimeTargetIdentity& identity) const {
    if (!identity) return std::nullopt;
    const auto found = byWorld_.find(identity.worldGeneration);
    if (found == byWorld_.end()) return std::nullopt;
    for (const auto& entry : found->second) {
        if (entry.first == identity) return entry.second;
    }
    return std::nullopt;
}

void UIIntrinsicLayoutRegistry::ReleaseWorld(std::uint64_t worldGeneration) {
    byWorld_.erase(worldGeneration);
}

UITextureContentRegistry& UITextureContentRegistry::Get() {
    static UITextureContentRegistry registry;
    return registry;
}

bool UITextureContentRegistry::Publish(const std::string& textureGuid,
                                       UITextureContentIdentity identity) {
    if (textureGuid.empty()) return false;
    auto& slot = byGuid_[textureGuid];
    if (slot == identity) return false;
    slot = std::move(identity);
    NotifyUISemanticMutation();
    return true;
}

std::optional<UITextureContentIdentity> UITextureContentRegistry::Find(
    const std::string& textureGuid) const {
    const auto found = byGuid_.find(textureGuid);
    if (found == byGuid_.end()) return std::nullopt;
    return found->second;
}

// ── Step 5c: GUID 하나가 지금 묶여 있는 런타임 바인딩 ───────────────────────
UITextureBindingRegistry& UITextureBindingRegistry::Get() {
    static UITextureBindingRegistry registry;
    return registry;
}

UITextureBindingPublishResult UITextureBindingRegistry::Publish(
    const std::string& textureGuid,
    std::shared_ptr<const molga::TextureBindingLifetime> lifetime) {
    if (textureGuid.empty() || !lifetime) {
        return UITextureBindingPublishResult::Unchanged;
    }
    const auto& identity = lifetime->Identity();
    auto found = byGuid_.find(textureGuid);
    if (found != byGuid_.end() && found->second.binding == identity &&
        found->second.lifetime == lifetime) {
        // 같은 값을 다시 게시하는 것은 변경이 아니다. 변경으로 세면 매 프레임
        // 재게시하는 소비자가 캐시를 통째로 무력화한다.
        return UITextureBindingPublishResult::Unchanged;
    }
    // 취득이 먼저다. 소진된 뒤에 슬롯을 갈아 끼우면 옛 세대가 새 바인딩을
    // 가리키게 되고, 그것이 정확히 스냅샷 캐시가 죽은 핸들을 돌려주는 경로다.
    if (!UIRuntimeInvalidationClock::Advance(
            UIRuntimeGenerationKind::TextureBinding)) {
        return UITextureBindingPublishResult::Exhausted;
    }
    UITextureRuntimeBinding record;
    record.binding = identity;
    record.lifetime = std::move(lifetime);
    byGuid_[textureGuid] = std::move(record);
    return UITextureBindingPublishResult::Published;
}

std::optional<UITextureRuntimeBinding> UITextureBindingRegistry::Find(
    const std::string& textureGuid) const {
    const auto found = byGuid_.find(textureGuid);
    if (found == byGuid_.end()) return std::nullopt;
    return found->second;
}

bool UITextureBindingRegistry::Retire(const std::string& textureGuid) {
    const auto found = byGuid_.find(textureGuid);
    if (found == byGuid_.end()) return false;
    byGuid_.erase(found);
    // 게시된 기록이 달라졌으므로 축도 움직여야 한다. 소진되었다면 그 시계는
    // 이미 cacheable을 내렸고 모든 캐시가 우회되므로, 여기서 기록을 붙들고
    // 있을 이유가 없다 — 붙들면 죽은 텍스처의 핸들이 영원히 반납되지 않는다.
    UIRuntimeInvalidationClock::Advance(
        UIRuntimeGenerationKind::TextureBinding);
    return true;
}

void UITextureBindingRegistry::Clear() {
    if (byGuid_.empty()) return;
    byGuid_.clear();
    UIRuntimeInvalidationClock::Advance(
        UIRuntimeGenerationKind::TextureBinding);
}

const EmptyUITextInputVisualStateProvider&
EmptyUITextInputVisualStateProvider::Instance() {
    static const EmptyUITextInputVisualStateProvider provider;
    return provider;
}

// ── 키 비교 ─────────────────────────────────────────────────────────────────
bool UILayoutFastPathStamp::operator==(
    const UILayoutFastPathStamp& other) const noexcept {
    return surfaceWindowId == other.surfaceWindowId &&
           worldGeneration == other.worldGeneration &&
           logicalViewport == other.logicalViewport &&
           viewportGeneration == other.viewportGeneration &&
           semanticDirtyGeneration == other.semanticDirtyGeneration &&
           scrollDisplacementGeneration == other.scrollDisplacementGeneration &&
           textureBindingGeneration == other.textureBindingGeneration &&
           deviceGeneration == other.deviceGeneration;
}

// ── 요청 하나의 필드별 비교 ─────────────────────────────────────────────────
// TextLayoutRequest에는 ==가 없다. 해시만 맞춰 보고 넘어가면 서로 다른 두
// 편집 상태가 한 기하 항목을 공유하고, 그때 화면에 나오는 것은 편집 이전의
// 줄이다. 그래서 중첩 구조까지 전부 편다.
bool EqualLayoutRequests(const molga::text::TextLayoutRequest& a,
                         const molga::text::TextLayoutRequest& b) {
    if (a.utf8 != b.utf8) return false;
    if (a.visualRevision != b.visualRevision) return false;
    if (a.diagnosticContext.assetGuid != b.diagnosticContext.assetGuid ||
        a.diagnosticContext.sceneObjectId !=
            b.diagnosticContext.sceneObjectId ||
        a.diagnosticContext.componentType !=
            b.diagnosticContext.componentType) {
        return false;
    }
    const auto& x = a.style;
    const auto& y = b.style;
    if (x.fontFamilyGuid != y.fontFamilyGuid ||
        x.legacyFontGuid != y.legacyFontGuid) {
        return false;
    }
    if (x.fontRequest.weight != y.fontRequest.weight ||
        x.fontRequest.stretchPercent != y.fontRequest.stretchPercent ||
        x.fontRequest.slant != y.fontRequest.slant) {
        return false;
    }
    if (x.shape.fontSize.Raw() != y.shape.fontSize.Raw() ||
        x.shape.language != y.shape.language ||
        x.shape.clusterPolicy != y.shape.clusterPolicy ||
        x.shape.orderedFeatures.size() != y.shape.orderedFeatures.size()) {
        return false;
    }
    for (std::size_t i = 0; i < x.shape.orderedFeatures.size(); ++i) {
        const auto& fa = x.shape.orderedFeatures[i];
        const auto& fb = y.shape.orderedFeatures[i];
        if (fa.tag != fb.tag || fa.value != fb.value ||
            fa.sourceBytes.begin != fb.sourceBytes.begin ||
            fa.sourceBytes.end != fb.sourceBytes.end) {
            return false;
        }
    }
    if (x.analysis.locale != y.analysis.locale ||
        x.analysis.baseDirection != y.analysis.baseDirection) {
        return false;
    }
    if (x.wrap != y.wrap || x.overflow != y.overflow ||
        x.maxLines != y.maxLines ||
        x.lineSpacing.Raw() != y.lineSpacing.Raw() ||
        x.horizontal != y.horizontal || x.vertical != y.vertical ||
        x.ellipsisUtf8 != y.ellipsisUtf8) {
        return false;
    }
    const auto sameConstraint = [](const std::optional<molga::Fixed26_6>& p,
                                   const std::optional<molga::Fixed26_6>& q) {
        if (p.has_value() != q.has_value()) return false;
        return !p || p->Raw() == q->Raw();
    };
    return sameConstraint(a.constraints.width, b.constraints.width) &&
           sameConstraint(a.constraints.height, b.constraints.height);
}

bool UITextInputGeometryCacheIdentity::operator==(
    const UITextInputGeometryCacheIdentity& other) const {
    return input == other.input &&
           EqualLayoutRequests(effectiveRequest, other.effectiveRequest);
}

bool UILayoutGeometryCacheKey::operator==(
    const UILayoutGeometryCacheKey& other) const {
    if (!(worldGeneration == other.worldGeneration &&
          viewport == other.viewport &&
          viewportGeneration == other.viewportGeneration &&
          canvasScaleRevisions == other.canvasScaleRevisions &&
          hierarchyAndSiblingRevisions == other.hierarchyAndSiblingRevisions &&
          rectAndLayoutRevisions == other.rectAndLayoutRevisions &&
          intrinsicGenerations == other.intrinsicGenerations)) {
        return false;
    }
    if (inputGeometry.size() != other.inputGeometry.size()) return false;
    for (std::size_t i = 0; i < inputGeometry.size(); ++i) {
        if (!(inputGeometry[i] == other.inputGeometry[i])) return false;
    }
    // Step 6b: 원래 필드를 전부 다시 본다. 해시가 같아도 여기서 갈린다.
    if (scrollDisplacements.size() != other.scrollDisplacements.size()) {
        return false;
    }
    for (std::size_t i = 0; i < scrollDisplacements.size(); ++i) {
        if (!(scrollDisplacements[i] == other.scrollDisplacements[i])) {
            return false;
        }
    }
    return true;
}

bool UIVisualCacheIdentity::operator==(
    const UIVisualCacheIdentity& other) const {
    return sceneObjectId == other.sceneObjectId &&
           componentTypeName == other.componentTypeName &&
           componentSchemaVersion == other.componentSchemaVersion &&
           authoredRevision == other.authoredRevision &&
           canonicalAuthoredPayload == other.canonicalAuthoredPayload &&
           immutableTextLayoutIdentity == other.immutableTextLayoutIdentity &&
           textureGuid == other.textureGuid &&
           textureContentSha256 == other.textureContentSha256 &&
           textureContentStableId == other.textureContentStableId;
}

bool UIInteractionCacheIdentity::operator==(
    const UIInteractionCacheIdentity& other) const {
    if (sceneObjectId != other.sceneObjectId ||
        componentTypeName != other.componentTypeName ||
        componentSchemaVersion != other.componentSchemaVersion ||
        authoredRevision != other.authoredRevision || active != other.active ||
        interactable != other.interactable || focusable != other.focusable ||
        acceptsTextInput != other.acceptsTextInput ||
        maskEnabled != other.maskEnabled ||
        navigationMode != other.navigationMode) {
        return false;
    }
    for (std::size_t i = 0; i < explicitNavigation.size(); ++i) {
        if (explicitNavigation[i].targetId !=
            other.explicitNavigation[i].targetId) {
            return false;
        }
    }
    return true;
}

bool UISnapshotCacheKey::operator==(const UISnapshotCacheKey& other) const {
    return surfaceWindowId == other.surfaceWindowId &&
           geometry == other.geometry &&
           semanticDirtyGeneration == other.semanticDirtyGeneration &&
           scrollDisplacementGeneration == other.scrollDisplacementGeneration &&
           textureBindingGeneration == other.textureBindingGeneration &&
           deviceGeneration == other.deviceGeneration &&
           visualContent == other.visualContent &&
           interaction == other.interaction &&
           runtimeBindings == other.runtimeBindings &&
           inputVisualStates == other.inputVisualStates;
}

bool UISnapshotWorldDeviceSlotKey::operator==(
    const UISnapshotWorldDeviceSlotKey& other) const noexcept {
    return worldGeneration == other.worldGeneration &&
           deviceGeneration == other.deviceGeneration;
}

std::size_t FoldUILayoutCacheHash(std::size_t state,
                                  std::uint64_t value) noexcept {
    return (state ^ static_cast<std::size_t>(value)) * 1099511628211ULL;
}

// 해시는 후보를 좁히기만 한다. 값 비교는 언제나 원래의 순서 있는 필드로 한다.
std::size_t HashUILayoutGeometryCacheKey(
    const UILayoutGeometryCacheKey& key) noexcept {
    std::size_t hash = kUILayoutCacheHashSeed;
    auto mix = [&hash](std::uint64_t value) {
        hash = FoldUILayoutCacheHash(hash, value);
    };
    mix(key.worldGeneration);
    mix(static_cast<std::uint64_t>(key.viewport.width.Raw()));
    mix(static_cast<std::uint64_t>(key.viewport.height.Raw()));
    mix(key.viewportGeneration);
    for (const auto* vector :
         {&key.canvasScaleRevisions, &key.hierarchyAndSiblingRevisions,
          &key.rectAndLayoutRevisions, &key.intrinsicGenerations}) {
        mix(vector->size());
        for (const auto value : *vector) mix(value);
    }
    // 입력 편집 기하도 후보를 좁히는 데 참여한다. 넣지 않으면 편집 전후가
    // 같은 bucket에 쌓여 조회가 선형 탐색이 되고, 그것은 진단 없이 성능으로만
    // 드러난다. 값 비교는 언제나 operator==가 다시 한다.
    mix(key.inputGeometry.size());
    // 접는 순서는 계약이다. "해시가 같아도 원래 필드를 전부 다시 본다"는 주장은
    // 실제로 충돌하는 두 키를 만들어 보이지 않으면 시험할 수 없고, 서로 다른
    // 키만 넣어 본 시험은 해시만 믿는 구현에서도 전부 통과한다. FNV-1a의 한
    // 걸음이 가역이므로, 자유롭게 값을 정할 수 있는 필드가 갈라지는 필드
    // 바로 뒤에 오면 충돌 쌍을 만들 수 있다: 정체성 쪽은 objectId 뒤의
    // componentInstanceId가, 요청 쪽은 utf8 해시 뒤의 visualRevision이 그 자리다.
    for (const auto& entry : key.inputGeometry) {
        mix(entry.input.objectId);
        mix(entry.input.componentInstanceId);
        mix(molga::text::CacheBytesHash(
            entry.effectiveRequest.style.fontFamilyGuid));
        mix(static_cast<std::uint64_t>(
            entry.effectiveRequest.style.shape.fontSize.Raw()));
        mix(molga::text::CacheBytesHash(entry.effectiveRequest.utf8));
        mix(entry.effectiveRequest.visualRevision);
    }
    // 스크롤 변위도 후보를 좁히는 데 참여한다. 넣지 않으면 오프셋만 다른 두
    // 키가 같은 bucket에 쌓여 조회가 선형 탐색이 되고, 그것은 진단 없이
    // 성능으로만 드러난다. 값 비교는 언제나 operator==가 다시 한다.
    mix(key.scrollDisplacements.size());
    for (const auto& entry : key.scrollDisplacements) {
        mix(entry.scrollTarget.objectId);
        mix(entry.scrollTarget.componentInstanceId);
        mix(static_cast<std::uint64_t>(entry.offsetXRaw));
        mix(static_cast<std::uint64_t>(entry.offsetYRaw));
        mix(entry.viewport.objectId);
        mix(entry.content.objectId);
    }
    return hash;
}

// ── Step 4a: 검증된 26.6 사각형 교집합 ──────────────────────────────────────
std::optional<molga::FixedRect> IntersectFixedRects(
    const molga::FixedRect& a, const molga::FixedRect& b) noexcept {
    const auto maxEdge = [](const molga::FixedRect& r, bool horizontal)
        -> std::optional<std::int64_t> {
        const std::int64_t origin = horizontal ? r.x.Raw() : r.y.Raw();
        const std::int64_t extent = horizontal ? r.width.Raw() : r.height.Raw();
        const std::int64_t sum = origin + extent;
        // 두 int32의 합은 int64에 언제나 들어가지만, 결과는 다시 26.6이어야
        // 한다. 범위를 벗어나면 실패다 — 포화시키면 잘리지 않아야 할 것이
        // 조용히 잘린다.
        if (sum < kRawMin || sum > kRawMax) return std::nullopt;
        return sum;
    };
    const auto ax1 = maxEdge(a, true);
    const auto ay1 = maxEdge(a, false);
    const auto bx1 = maxEdge(b, true);
    const auto by1 = maxEdge(b, false);
    if (!ax1 || !ay1 || !bx1 || !by1) return std::nullopt;
    const std::int64_t x0 = std::max<std::int64_t>(a.x.Raw(), b.x.Raw());
    const std::int64_t y0 = std::max<std::int64_t>(a.y.Raw(), b.y.Raw());
    const std::int64_t x1 = std::min(*ax1, *bx1);
    const std::int64_t y1 = std::min(*ay1, *by1);
    // 반열린 사각형이라 오른쪽/아래 변은 바깥이다. x1 == x0은 빈 교집합이다.
    if (x1 <= x0 || y1 <= y0) return std::nullopt;
    const std::int64_t width = x1 - x0;
    const std::int64_t height = y1 - y0;
    if (!InRawRange(x0) || !InRawRange(y0) || !InRawRange(width) ||
        !InRawRange(height)) {
        return std::nullopt;
    }
    return molga::FixedRect{ToFixed(x0), ToFixed(y0), ToFixed(width),
                            ToFixed(height)};
}

std::string UILabelIntrinsicContentIdentity(
    const molga::text::TextLayoutRequest& request) {
    const auto& style = request.style;
    std::string id;
    id.reserve(request.utf8.size() + 96U);
    const auto field = [&id](std::string_view value) {
        // 길이를 함께 적는다. 구분자만으로는 ("a|b", "")와 ("a", "b|")가
        // 같은 바이트를 낸다.
        id += std::to_string(value.size());
        id += ':';
        id.append(value);
        id += '|';
    };
    const auto number = [&id](std::int64_t value) {
        id += std::to_string(value);
        id += '|';
    };
    field(request.utf8);
    field(style.fontFamilyGuid);
    field(style.legacyFontGuid);
    field(style.analysis.locale);
    number(style.shape.fontSize.Raw());
    number(style.lineSpacing.Raw());
    number(static_cast<std::int64_t>(style.analysis.baseDirection));
    number(static_cast<std::int64_t>(style.wrap));
    number(static_cast<std::int64_t>(style.overflow));
    number(static_cast<std::int64_t>(style.maxLines));
    number(static_cast<std::int64_t>(style.horizontal));
    number(static_cast<std::int64_t>(style.vertical));
    return id;
}

// ── Step 4e: 확정된 배치가 실제로 낼 명령 수 ────────────────────────────────
namespace {

// 부호 있는 내림/올림 나눗셈. C++의 정수 나눗셈은 0 쪽으로 자르므로 음수에서
// floor/ceil과 갈린다 — 음의 논리 원점을 가진 뷰포트가 정확히 그 경우다.
std::int64_t FloorDiv(std::int64_t n, std::int64_t d) noexcept {
    std::int64_t q = n / d;
    if ((n % d != 0) && ((n < 0) != (d < 0))) --q;
    return q;
}

std::int64_t CeilDiv(std::int64_t n, std::int64_t d) noexcept {
    std::int64_t q = n / d;
    if ((n % d != 0) && ((n < 0) == (d < 0))) ++q;
    return q;
}

bool SafeMul(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    const std::int64_t limit = std::numeric_limits<std::int64_t>::max();
    if (a > 0 ? (b > 0 ? a > limit / b : b < -limit / a)
              : (b > 0 ? a < -limit / b : a < -limit / -b)) {
        return false;
    }
    out = a * b;
    return true;
}

} // namespace

// ── Step 6: 논리 -> 물리 변환의 유일한 자리 ─────────────────────────────────
// 변환은 여기 한 번만 적혀 있고, 사각형만 필요한 쪽(scissor, 솔리드)은 같은
// 결과의 rect만 읽는다. 자름의 비율까지 함께 돌려주는 이유는 스프라이트
// 하나 때문이다 — 아래 ToPhysicalSpriteOutward의 주석이 그 이유다.
std::optional<UIPhysicalTransform::SpriteQuad>
UIPhysicalTransform::ToPhysicalSpriteOutward(
    const molga::FixedRect& rect) const noexcept {
    const std::int64_t logicalW = logicalViewport.width.Raw();
    const std::int64_t logicalH = logicalViewport.height.Raw();
    if (logicalW <= 0 || logicalH <= 0) return std::nullopt;
    if (physicalViewport.width == 0 || physicalViewport.height == 0) {
        return std::nullopt;
    }
    if (rect.width.Raw() <= 0 || rect.height.Raw() <= 0) return std::nullopt;

    const auto axis = [](std::int64_t origin, std::int64_t extent,
                         std::int64_t viewportOrigin, std::int64_t logicalExtent,
                         std::uint32_t physicalExtent, std::uint32_t& outMin,
                         std::uint32_t& outSize, float& outLowFraction,
                         float& outHighFraction) -> bool {
        const std::int64_t relMin = origin - viewportOrigin;
        const std::int64_t relMax = relMin + extent;
        const std::int64_t physical = static_cast<std::int64_t>(physicalExtent);
        std::int64_t minProduct = 0;
        std::int64_t maxProduct = 0;
        if (!SafeMul(relMin, physical, minProduct) ||
            !SafeMul(relMax, physical, maxProduct)) {
            return false;
        }
        // 바깥쪽으로 연다: 최소 변은 floor, 최대 변은 ceil. 반올림이면 1픽셀
        // 폭 클립이 통째로 사라진다.
        const std::int64_t openLow = FloorDiv(minProduct, logicalExtent);
        const std::int64_t openHigh = CeilDiv(maxProduct, logicalExtent);
        if (openHigh <= openLow) return false;
        const std::int64_t low = std::max<std::int64_t>(openLow, 0);
        const std::int64_t high = std::min<std::int64_t>(openHigh, physical);
        if (high <= low) return false;
        outMin = static_cast<std::uint32_t>(low);
        outSize = static_cast<std::uint32_t>(high - low);
        // 자르기 전 구간 위에서 살아남은 구간이 차지하는 비율. 분모는
        // openHigh - openLow > 0이므로 0으로 나누지 않는다. 자르지 않은
        // 사각형은 정확히 0과 1을 낸다(뺄셈 결과가 0과 분모 그대로다).
        const double span = static_cast<double>(openHigh - openLow);
        outLowFraction = static_cast<float>(static_cast<double>(low - openLow) /
                                            span);
        outHighFraction =
            static_cast<float>(static_cast<double>(high - openLow) / span);
        return true;
    };

    SpriteQuad quad;
    std::uint32_t offsetX = 0;
    std::uint32_t offsetY = 0;
    if (!axis(rect.x.Raw(), rect.width.Raw(), logicalViewport.x.Raw(), logicalW,
              physicalViewport.width, offsetX, quad.rect.width, quad.u0,
              quad.u1)) {
        return std::nullopt;
    }
    if (!axis(rect.y.Raw(), rect.height.Raw(), logicalViewport.y.Raw(), logicalH,
              physicalViewport.height, offsetY, quad.rect.height, quad.v0,
              quad.v1)) {
        return std::nullopt;
    }
    // 물리 뷰포트 원점은 마지막에 한 번만 더한다.
    const std::uint64_t absoluteX =
        static_cast<std::uint64_t>(physicalViewport.x) + offsetX;
    const std::uint64_t absoluteY =
        static_cast<std::uint64_t>(physicalViewport.y) + offsetY;
    if (absoluteX > std::numeric_limits<std::uint32_t>::max() ||
        absoluteY > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    quad.rect.x = static_cast<std::uint32_t>(absoluteX);
    quad.rect.y = static_cast<std::uint32_t>(absoluteY);
    return quad;
}

std::optional<molga::PixelRectU32> UIPhysicalTransform::ToPhysicalOutward(
    const molga::FixedRect& rect) const noexcept {
    const auto quad = ToPhysicalSpriteOutward(rect);
    if (!quad) return std::nullopt;
    return quad->rect;
}

std::optional<molga::FixedPoint> UIPhysicalTransform::ToLogicalPoint(
    double outputPixelX, double outputPixelY) const noexcept {
    if (!std::isfinite(outputPixelX) || !std::isfinite(outputPixelY)) {
        return std::nullopt;
    }
    const std::int64_t logicalW = logicalViewport.width.Raw();
    const std::int64_t logicalH = logicalViewport.height.Raw();
    if (logicalW <= 0 || logicalH <= 0) return std::nullopt;
    if (physicalViewport.width == 0 || physicalViewport.height == 0) {
        return std::nullopt;
    }
    const double minX = static_cast<double>(physicalViewport.x);
    const double minY = static_cast<double>(physicalViewport.y);
    const double maxX = minX + static_cast<double>(physicalViewport.width);
    const double maxY = minY + static_cast<double>(physicalViewport.height);
    // 반열린 구간이다. 오른쪽/아래 가장자리 픽셀은 이 뷰포트 밖이다.
    if (outputPixelX < minX || outputPixelX >= maxX) return std::nullopt;
    if (outputPixelY < minY || outputPixelY >= maxY) return std::nullopt;

    // ── Task 11.3: 배율은 double이 아니라 검증된 유리수다 ───────────────────
    // 부동소수 걸음은 딱 하나 남는다: 들어오는 값이 float 픽셀 위치이므로
    // 그것을 정수 분자로 못 박는 반올림 한 번은 피할 수 없다. 그 뒤의 배율은
    // Fixed26_6::CheckedMulDiv 하나가 정의한다 — 이 하위 시스템의 다른 모든
    // 변환과 같은 0에서 먼 쪽 반올림, 같은 넘침 거절이다.
    //
    // 분자를 26.6이 아니라 **Q16**으로 잡는 것이 요점이다. 26.6으로 잡으면
    // 1/64 픽셀보다 미세한 포인터 위치가 통째로 사라져, 논리 단위가 픽셀보다
    // 촘촘한 표면에서 이 변환이 지금보다 **덜** 정확해진다. 1/65536 픽셀은
    // 어떤 실제 포인터보다 촘촘하고, L/P가 65536 미만인 한 출력의 1/64 논리
    // 단위 양자화보다 언제나 미세하다.
    //
    // double 곱셈은 relative * logicalExtent가 2^53을 넘는 순간부터 조용히
    // 부정확해지고 그 부정확함을 보고하지 않는다. 여기서는 분자가 int32에
    // 들어가지 않으면(즉 물리 뷰포트가 32768픽셀보다 넓으면) 값을 지어내는
    // 대신 실패를 보고한다. hit-test에서 그 실패는 "그 점은 이 표면 밖"이므로
    // fail-closed다.
    //
    // 반열린 경계 검사는 위에서 이미 끝났고 여기서 바뀌지 않는다.
    constexpr std::int64_t kInverseSubPixel = 65536;
    const auto axis = [](double pixel, double viewportMin,
                         std::uint32_t physicalExtent, std::int64_t logicalExtent,
                         std::int64_t logicalOrigin,
                         std::int64_t& out) -> bool {
        const double relative = pixel - viewportMin;
        const double relativeNumerator =
            relative * static_cast<double>(kInverseSubPixel);
        if (!std::isfinite(relativeNumerator)) return false;
        const double rounded = relativeNumerator >= 0.0
                                   ? std::floor(relativeNumerator + 0.5)
                                   : std::ceil(relativeNumerator - 0.5);
        if (rounded < static_cast<double>(kRawMin) ||
            rounded > static_cast<double>(kRawMax)) {
            return false;
        }
        // CheckedMulDiv는 (raw * numerator) / denominator를 0에서 먼 쪽으로
        // 반올림하고 int32 범위를 검사한다. 여기서 raw는 26.6 값이 아니라
        // Q16 분자다 — 이 함수가 정의하는 것은 타입이 아니라 그 산술 규칙이고,
        // 규칙의 사본을 두 벌 두지 않는 것이 이 호출의 이유다.
        const auto scaled = Fixed26_6::CheckedMulDiv(
            Fixed26_6::FromRaw(static_cast<std::int32_t>(rounded)),
            logicalExtent,
            static_cast<std::int64_t>(physicalExtent) * kInverseSubPixel);
        if (!scaled) return false;
        const std::int64_t value = logicalOrigin + scaled->Raw();
        if (!InRawRange(value)) return false;
        out = value;
        return true;
    };

    std::int64_t x = 0;
    std::int64_t y = 0;
    if (!axis(outputPixelX, minX, physicalViewport.width, logicalW,
              logicalViewport.x.Raw(), x) ||
        !axis(outputPixelY, minY, physicalViewport.height, logicalH,
              logicalViewport.y.Raw(), y)) {
        return std::nullopt;
    }
    return molga::FixedPoint{ToFixed(x), ToFixed(y)};
}

std::optional<TextAffine2D> UIPhysicalTransform::LayoutToOutputAffine(
    molga::FixedPoint logicalOrigin) const noexcept {
    const std::int64_t logicalW = logicalViewport.width.Raw();
    const std::int64_t logicalH = logicalViewport.height.Raw();
    if (logicalW <= 0 || logicalH <= 0) return std::nullopt;
    if (physicalViewport.width == 0 || physicalViewport.height == 0) {
        return std::nullopt;
    }
    // 논리 단위는 raw/64다. 배율은 물리 픽셀 / 논리 단위이므로 64를 곱한다.
    const double scaleX = static_cast<double>(physicalViewport.width) * 64.0 /
                          static_cast<double>(logicalW);
    const double scaleY = static_cast<double>(physicalViewport.height) * 64.0 /
                          static_cast<double>(logicalH);
    const double originX =
        (static_cast<double>(logicalOrigin.x.Raw() - logicalViewport.x.Raw())) /
        64.0;
    const double originY =
        (static_cast<double>(logicalOrigin.y.Raw() - logicalViewport.y.Raw())) /
        64.0;
    const double tx = static_cast<double>(physicalViewport.x) + originX * scaleX;
    const double ty = static_cast<double>(physicalViewport.y) + originY * scaleY;
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) ||
        !std::isfinite(tx) || !std::isfinite(ty)) {
        return std::nullopt;
    }
    const double floatLimit =
        static_cast<double>(std::numeric_limits<float>::max());
    if (std::abs(scaleX) > floatLimit || std::abs(scaleY) > floatLimit ||
        std::abs(tx) > floatLimit || std::abs(ty) > floatLimit) {
        return std::nullopt;
    }
    TextAffine2D affine;
    affine.m00 = static_cast<float>(scaleX);
    affine.m11 = static_cast<float>(scaleY);
    affine.m01 = 0.0f;
    affine.m10 = 0.0f;
    affine.tx = static_cast<float>(tx);
    affine.ty = static_cast<float>(ty);
    return affine;
}

std::optional<TextRasterPolicy> UIPhysicalTransform::RasterPolicy(
    molga::text::TextDiagnosticSink& sink) const {
    // Task 8의 검증된 정수 규칙 하나를 그대로 쓴다. affine의 float에서
    // 되짚으면 같은 화면에서 항목마다 다른 래스터 높이가 나온다.
    return TextRasterPolicy::FromUiScale(
        molga::FixedSize{logicalViewport.width, logicalViewport.height},
        molga::PixelSize{static_cast<int>(physicalViewport.width),
                         static_cast<int>(physicalViewport.height)},
        sink);
}

namespace {

// 상한 없이 커지는 것은 하나도 없다. 성장한 순간에만 세므로, 이미 확보된
// 용량 안에서 덮어쓰는 warm 프레임은 아무것도 세지 않는다.
template <class T>
void ScratchResize(std::vector<T>& scratch, std::size_t size,
                   std::uint64_t& allocations) {
    const std::size_t before = scratch.capacity();
    scratch.resize(size);
    if (scratch.capacity() > before) ++allocations;
}

void ScratchAssign(std::string& scratch, const std::string& value,
                   std::uint64_t& allocations) {
    const std::size_t before = scratch.capacity();
    scratch.assign(value);
    if (scratch.capacity() > before) ++allocations;
}

} // namespace

// ── 배치 계산 ───────────────────────────────────────────────────────────────
namespace {

struct LayoutBuilder {
    World* world = nullptr;
    molga::text::TextDiagnosticSink* sink = nullptr;
    std::vector<LayoutNode> nodes;
    bool failed = false;
    // 순환이 보고된 축 집합. 같은 순환을 프레임마다 다시 보고하면 로그가
    // 순환 하나로 가득 찬다.
    std::vector<std::string>* reportedCycles = nullptr;

    void Fail(const std::string& message) {
        if (failed) return;
        failed = true;
        molga::text::TextDiagnostic diagnostic;
        diagnostic.code = molga::text::TextDiagnosticCode::LayoutInvalid;
        diagnostic.severity = molga::text::TextSeverity::Error;
        diagnostic.subsystem = "ui.layout";
        diagnostic.message = message;
        diagnostic.remediation =
            "author finite, in-range UI geometry values";
        if (sink) sink->Report(std::move(diagnostic));
    }

    Raw Checked(Raw value) {
        if (!InRawRange(value)) Fail("UI layout value left the 26.6 range");
        return value;
    }
};

Raw AuthoredOrFail(LayoutBuilder& builder, float value) {
    Raw raw = 0;
    if (!AuthoredToRaw(value, raw)) {
        builder.Fail("UI authored value is not a finite 26.6 quantity");
        return 0;
    }
    return raw;
}

// 저작된 RectTransform 하나를 부모 사각형 안에서 확정한다. float 경로
// (RectTransform::ResolveIn)와 같은 식을 raw 정수로 다시 쓴 것이다 — 그래야
// 같은 저작 입력이 실행마다 정확히 같은 raw를 낸다.
RawRect ResolveAuthored(LayoutBuilder& builder, const RectTransform& rect,
                        const RawRect& parent) {
    const Raw anchorMinX = AuthoredOrFail(builder, rect.GetAnchorMin().x);
    const Raw anchorMinY = AuthoredOrFail(builder, rect.GetAnchorMin().y);
    const Raw anchorMaxX = AuthoredOrFail(builder, rect.GetAnchorMax().x);
    const Raw anchorMaxY = AuthoredOrFail(builder, rect.GetAnchorMax().y);
    const Raw pivotX = AuthoredOrFail(builder, rect.GetPivot().x);
    const Raw pivotY = AuthoredOrFail(builder, rect.GetPivot().y);
    const Raw posX = AuthoredOrFail(builder, rect.GetAnchoredPosition().x);
    const Raw posY = AuthoredOrFail(builder, rect.GetAnchoredPosition().y);
    const Raw sizeDeltaX = AuthoredOrFail(builder, rect.GetSizeDelta().x);
    const Raw sizeDeltaY = AuthoredOrFail(builder, rect.GetSizeDelta().y);
    if (builder.failed) return RawRect{};

    auto axis = [&builder](Raw parentOrigin, Raw parentSize, Raw anchorMin,
                           Raw anchorMax, Raw pivot, Raw position,
                           Raw sizeDelta, Raw& outOrigin, Raw& outSize) {
        Raw span = 0;
        if (!MulDiv(parentSize, anchorMax - anchorMin, Fixed26_6::Scale, span)) {
            builder.Fail("UI anchor span overflowed");
            return;
        }
        outSize = builder.Checked(span + sizeDelta);
        Raw base = 0;
        Raw pivotTerm = 0;
        if (!MulDiv(parentSize, anchorMin, Fixed26_6::Scale, base) ||
            !MulDiv(parentSize, (anchorMax - anchorMin) * pivot,
                    static_cast<Raw>(Fixed26_6::Scale) * Fixed26_6::Scale,
                    pivotTerm)) {
            builder.Fail("UI anchor reference overflowed");
            return;
        }
        Raw pivotOffset = 0;
        if (!MulDiv(outSize, pivot, Fixed26_6::Scale, pivotOffset)) {
            builder.Fail("UI pivot offset overflowed");
            return;
        }
        outOrigin = builder.Checked(parentOrigin + base + pivotTerm + position -
                                    pivotOffset);
    };

    RawRect result;
    axis(parent.x, parent.width, anchorMinX, anchorMaxX, pivotX, posX,
         sizeDeltaX, result.x, result.width);
    axis(parent.y, parent.height, anchorMinY, anchorMaxY, pivotY, posY,
         sizeDeltaY, result.y, result.height);
    return result;
}

// 크기가 드라이버 때문에 저작값과 달라지면 원점도 함께 움직인다. pivot이
// 0.5라면 폭이 늘어난 만큼 절반이 왼쪽으로 나가야 한다 — 원점을 그대로 두면
// 사각형이 pivot을 무시하고 한쪽으로만 자란다.
Raw ShiftOriginForSize(LayoutBuilder& builder, Raw authoredOrigin,
                       Raw authoredSize, Raw finalSize, float pivot) {
    const Raw pivotRaw = AuthoredOrFail(builder, pivot);
    if (builder.failed) return authoredOrigin;
    Raw shift = 0;
    if (!MulDiv(authoredSize - finalSize, pivotRaw, Fixed26_6::Scale, shift)) {
        builder.Fail("UI pivot re-anchoring overflowed");
        return authoredOrigin;
    }
    return builder.Checked(authoredOrigin + shift);
}

} // namespace

namespace {

// ── Step 5a: 활성 Canvas 트리를 정확한 형제 DFS 순서로 모은다 ───────────────
// UIDrawOrderKey는 이 순회에서 한 번만 만들어진다. 나중에 두 번째 순회 인덱스를
// 만들면 두 순서가 갈릴 수 있고, 그때 렌더와 hit-test가 서로 다른 순서를 본다.
void GatherSubtree(LayoutBuilder& builder, GameObject* object,
                   std::size_t parentNode, std::vector<std::uint32_t> path,
                   std::int32_t canvasSortingOrder, bool canvasRoot) {
    if (!object || !object->IsActive()) return;

    std::size_t nodeIndex = parentNode;
    RectTransform* rect = EnabledComponent<RectTransform>(*object);
    if (rect) {
        LayoutNode node;
        node.object = object;
        node.rect = rect;
        node.canvas = object->GetComponent<UICanvas>();
        node.group = EnabledComponent<UILayoutGroup>(*object);
        node.element = EnabledComponent<UILayoutElement>(*object);
        node.fitter = EnabledComponent<UIContentSizeFitter>(*object);
        node.mask = EnabledComponent<UIMask>(*object);
        node.parent = parentNode;
        node.canvasRoot = canvasRoot;
        node.drawOrder.canvasSortingOrder = canvasSortingOrder;
        node.drawOrder.siblingPath = path;
        node.drawOrder.stableSubmissionIndex = builder.nodes.size();
        // 이 단계의 노드는 RectTransform 하나를 대표한다. 레코드별 정렬 순서는
        // Task 11.1이 렌더/hit 페이로드와 함께 가져오므로, 여기서는 그 오브젝트가
        // 저작한 시각 순서 하나만 싣는다.
        if (auto* image = object->GetComponent<UIImage>()) {
            node.drawOrder.componentSortingOrder = image->GetSortingOrder();
        } else if (auto* label = object->GetComponent<UILabel>()) {
            node.drawOrder.componentSortingOrder = label->GetSortingOrder();
        } else if (auto* button = object->GetComponent<UIButton>()) {
            node.drawOrder.componentSortingOrder = button->GetSortingOrder();
        }
        nodeIndex = builder.nodes.size();
        builder.nodes.push_back(std::move(node));
        if (parentNode != kNoNode) {
            builder.nodes[parentNode].children.push_back(nodeIndex);
            const bool ignored =
                builder.nodes[nodeIndex].element &&
                builder.nodes[nodeIndex].element->IgnoreLayout();
            if (!ignored) {
                builder.nodes[parentNode].layoutChildren.push_back(nodeIndex);
            }
        }
    }

    const auto& children = object->GetChildren();
    for (std::size_t i = 0; i < children.size(); ++i) {
        std::vector<std::uint32_t> childPath = path;
        childPath.push_back(static_cast<std::uint32_t>(i));
        GatherSubtree(builder, children[i], nodeIndex, std::move(childPath),
                      canvasSortingOrder, false);
    }
}

// ── 격자 칸 수 ──────────────────────────────────────────────────────────────
// Flexible은 사용 가능한 폭을 알아야 열 수가 정해지므로 측정 단계에서는
// 답할 수 없다. 측정에서는 한 줄(열 = 자식 수)로 보고, 확정된 사각형이 생긴
// 배치 단계에서만 실제 열 수를 계산한다.
std::size_t GridColumns(const UILayoutGroup& group, std::size_t count,
                        std::optional<Raw> innerWidth, Raw cellWidth,
                        Raw spacingX) {
    if (count == 0) return 1;
    switch (group.GridConstraint()) {
        case UIGridConstraint::FixedColumns:
            return std::max<std::size_t>(1, group.ConstraintCount());
        case UIGridConstraint::FixedRows: {
            const std::size_t rows =
                std::max<std::size_t>(1, group.ConstraintCount());
            return (count + rows - 1) / rows;
        }
        case UIGridConstraint::Flexible:
            break;
    }
    if (!innerWidth) return count;
    const auto columns = molga::CheckedFloorDiv(*innerWidth + spacingX,
                                                cellWidth + spacingX);
    if (!columns || *columns < 1) return 1;
    return std::min<std::size_t>(count, static_cast<std::size_t>(*columns));
}

std::size_t GridRows(std::size_t count, std::size_t columns) {
    if (columns == 0) return 1;
    return std::max<std::size_t>(1, (count + columns - 1) / columns);
}

// ── Step 5b: 자식에서 부모로 고유 제약을 측정한다 ───────────────────────────
void MeasureNode(LayoutBuilder& builder, std::size_t index) {
    LayoutNode& node = builder.nodes[index];
    Raw contentMinW = node.intrinsicWidth;
    Raw contentMinH = node.intrinsicHeight;
    Raw contentPrefW = node.intrinsicWidth;
    Raw contentPrefH = node.intrinsicHeight;

    if (node.group && !node.layoutChildren.empty()) {
        const UILayoutGroup& group = *node.group;
        const Raw padL = AuthoredOrFail(builder, group.PaddingLeft());
        const Raw padR = AuthoredOrFail(builder, group.PaddingRight());
        const Raw padT = AuthoredOrFail(builder, group.PaddingTop());
        const Raw padB = AuthoredOrFail(builder, group.PaddingBottom());
        const Raw spacingX = AuthoredOrFail(builder, group.SpacingX());
        const Raw spacingY = AuthoredOrFail(builder, group.SpacingY());
        const auto count = node.layoutChildren.size();
        const Raw gaps = static_cast<Raw>(count - 1);

        if (group.Mode() == UILayoutMode::Grid) {
            const Raw cellW = AuthoredOrFail(builder, group.CellSizeX());
            const Raw cellH = AuthoredOrFail(builder, group.CellSizeY());
            const std::size_t columns =
                GridColumns(group, count, std::nullopt, cellW, spacingX);
            const std::size_t rows = GridRows(count, columns);
            contentMinW = contentPrefW =
                padL + padR + static_cast<Raw>(columns) * cellW +
                static_cast<Raw>(columns - 1) * spacingX;
            contentMinH = contentPrefH =
                padT + padB + static_cast<Raw>(rows) * cellH +
                static_cast<Raw>(rows - 1) * spacingY;
        } else {
            Raw sumMinMain = 0, sumPrefMain = 0, maxMinCross = 0,
                maxPrefCross = 0;
            const bool horizontal = group.Mode() == UILayoutMode::Horizontal;
            for (const auto child : node.layoutChildren) {
                const LayoutNode& c = builder.nodes[child];
                sumMinMain += horizontal ? c.minWidth : c.minHeight;
                sumPrefMain += horizontal ? c.prefWidth : c.prefHeight;
                maxMinCross =
                    std::max(maxMinCross, horizontal ? c.minHeight : c.minWidth);
                maxPrefCross = std::max(
                    maxPrefCross, horizontal ? c.prefHeight : c.prefWidth);
            }
            const Raw mainSpacing = gaps * (horizontal ? spacingX : spacingY);
            if (horizontal) {
                contentMinW = padL + padR + mainSpacing + sumMinMain;
                contentPrefW = padL + padR + mainSpacing + sumPrefMain;
                contentMinH = padT + padB + maxMinCross;
                contentPrefH = padT + padB + maxPrefCross;
            } else {
                contentMinH = padT + padB + mainSpacing + sumMinMain;
                contentPrefH = padT + padB + mainSpacing + sumPrefMain;
                contentMinW = padL + padR + maxMinCross;
                contentPrefW = padL + padR + maxPrefCross;
            }
        }
    }

    node.minWidth = builder.Checked(contentMinW);
    node.minHeight = builder.Checked(contentMinH);
    node.prefWidth = builder.Checked(contentPrefW);
    node.prefHeight = builder.Checked(contentPrefH);

    if (node.element) {
        // 저작된 0은 "지정 없음"이다. 0을 "최소 0을 강제한다"로 읽으면 요소를
        // 붙이기만 해도 내용 기반 최소값이 사라진다.
        const auto& horizontal = node.element->Horizontal();
        const auto& vertical = node.element->Vertical();
        if (horizontal.minimum > 0.0f) {
            node.minWidth = AuthoredOrFail(builder, horizontal.minimum);
        }
        if (horizontal.preferred > 0.0f) {
            node.prefWidth = AuthoredOrFail(builder, horizontal.preferred);
        }
        if (vertical.minimum > 0.0f) {
            node.minHeight = AuthoredOrFail(builder, vertical.minimum);
        }
        if (vertical.preferred > 0.0f) {
            node.prefHeight = AuthoredOrFail(builder, vertical.preferred);
        }
    }
    node.prefWidth = std::max(node.prefWidth, node.minWidth);
    node.prefHeight = std::max(node.prefHeight, node.minHeight);
}

// ── Step 6b: 선호/유연 몫을 나눈다 ──────────────────────────────────────────
// headroom < 0 은 상한 없음이다. 비례 배분은 언제나 내림이므로 합이 모자라고,
// 남은 raw 한 칸씩은 자격 있는 첫 형제부터 순서대로 간다 — 그 순서가 형제
// 순서이기 때문에 같은 입력이 언제나 같은 사각형을 낸다.
void Distribute(Raw extra, const std::vector<Raw>& weights,
                std::vector<Raw>& headroom, std::vector<Raw>& sizes,
                Raw& remaining) {
    remaining = extra;
    if (extra <= 0) return;
    Raw totalWeight = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 0 && headroom[i] != 0) totalWeight += weights[i];
    }
    if (totalWeight <= 0) return;

    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] <= 0 || headroom[i] == 0) continue;
        const auto share = molga::CheckedFloorDiv(extra * weights[i], totalWeight);
        if (!share) continue;
        Raw give = *share;
        if (headroom[i] > 0) give = std::min(give, headroom[i]);
        give = std::min(give, remaining);
        if (give <= 0) continue;
        sizes[i] += give;
        if (headroom[i] > 0) headroom[i] -= give;
        remaining -= give;
    }

    while (remaining > 0) {
        bool gave = false;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] <= 0 || headroom[i] == 0) continue;
            sizes[i] += 1;
            if (headroom[i] > 0) headroom[i] -= 1;
            remaining -= 1;
            gave = true;
            break;
        }
        if (!gave) break;
    }
}

Raw AlignmentOffset(Raw slack, int alignment) {
    if (slack <= 0) return 0;
    if (alignment == 0) return 0;                 // Left / Top
    if (alignment == 2) return slack;             // Right / Bottom
    const auto half = molga::CheckedFloorDiv(slack, 2);
    return half ? *half : 0;                      // Center / Middle
}

int MainAlignment(const UILayoutGroup& group, bool horizontal) {
    if (horizontal) {
        switch (group.ChildHorizontalAlignment()) {
            case molga::text::TextHorizontalAlignment::Left: return 0;
            case molga::text::TextHorizontalAlignment::Center: return 1;
            case molga::text::TextHorizontalAlignment::Right: return 2;
        }
        return 0;
    }
    switch (group.ChildVerticalAlignment()) {
        case molga::text::TextVerticalAlignment::Top: return 0;
        case molga::text::TextVerticalAlignment::Middle: return 1;
        case molga::text::TextVerticalAlignment::Bottom: return 2;
    }
    return 0;
}

} // namespace

namespace {

// 한 축에서 그 자식의 크기를 실제로 정하는 값. 부모 그룹이 그 축을 통제하지
// 않을 때 무엇이 답인지를 한 곳에서 정한다.
Raw SelfDrivenSize(const LayoutNode& child, bool horizontal, Raw authored) {
    if (!child.fitter) return authored;
    const UIFitMode fit =
        horizontal ? child.fitter->HorizontalFit() : child.fitter->VerticalFit();
    switch (fit) {
        case UIFitMode::Min:
            return horizontal ? child.minWidth : child.minHeight;
        case UIFitMode::Preferred:
            return horizontal ? child.prefWidth : child.prefHeight;
        case UIFitMode::Unconstrained:
            break;
    }
    return authored;
}

bool FitterDrives(const LayoutNode& node, bool horizontal) {
    if (!node.fitter) return false;
    const UIFitMode fit =
        horizontal ? node.fitter->HorizontalFit() : node.fitter->VerticalFit();
    return fit != UIFitMode::Unconstrained;
}

// 그룹 바깥에 있는(또는 무시되는) 자식 하나를 확정한다. 드라이버는
// Canvas > 부모 그룹 > 자기 fitter > 저작 rect 순서 그대로이고, 여기서는
// 부모 그룹이 없으므로 세 번째부터가 후보다.
void ResolveOutsideGroup(LayoutBuilder& builder, std::size_t index,
                         const RawRect& parentRect) {
    LayoutNode& node = builder.nodes[index];
    const RawRect authored = ResolveAuthored(builder, *node.rect, parentRect);
    if (builder.failed) return;
    RawRect result = authored;

    const auto widthDriver =
        ResolveDriver(false, false, !node.cyclicWidth && FitterDrives(node, true));
    if (widthDriver == UILayoutDriver::SelfFitter) {
        result.width = builder.Checked(SelfDrivenSize(node, true, authored.width));
        result.x = ShiftOriginForSize(builder, authored.x, authored.width,
                                      result.width, node.rect->GetPivot().x);
    }
    const auto heightDriver = ResolveDriver(
        false, false, !node.cyclicHeight && FitterDrives(node, false));
    if (heightDriver == UILayoutDriver::SelfFitter) {
        result.height =
            builder.Checked(SelfDrivenSize(node, false, authored.height));
        result.y = ShiftOriginForSize(builder, authored.y, authored.height,
                                      result.height, node.rect->GetPivot().y);
    }
    node.resolved = result;
}

void ArrangeGroup(LayoutBuilder& builder, std::size_t index);

void ArrangeSubtree(LayoutBuilder& builder, std::size_t index) {
    if (builder.failed) return;
    if (builder.nodes[index].group &&
        !builder.nodes[index].layoutChildren.empty()) {
        ArrangeGroup(builder, index);
    }
    const auto children = builder.nodes[index].children;
    const auto layoutChildren = builder.nodes[index].layoutChildren;
    const RawRect parentRect = builder.nodes[index].resolved;
    for (const auto child : children) {
        const bool allocated =
            std::find(layoutChildren.begin(), layoutChildren.end(), child) !=
                layoutChildren.end() &&
            builder.nodes[index].group != nullptr;
        if (!allocated) ResolveOutsideGroup(builder, child, parentRect);
        ArrangeSubtree(builder, child);
    }
}

// ── Step 6a-6d ──────────────────────────────────────────────────────────────
void ArrangeGroup(LayoutBuilder& builder, std::size_t index) {
    const UILayoutGroup& group = *builder.nodes[index].group;
    const RawRect rect = builder.nodes[index].resolved;
    const auto children = builder.nodes[index].layoutChildren;
    const auto count = children.size();

    const Raw padL = AuthoredOrFail(builder, group.PaddingLeft());
    const Raw padR = AuthoredOrFail(builder, group.PaddingRight());
    const Raw padT = AuthoredOrFail(builder, group.PaddingTop());
    const Raw padB = AuthoredOrFail(builder, group.PaddingBottom());
    const Raw spacingX = AuthoredOrFail(builder, group.SpacingX());
    const Raw spacingY = AuthoredOrFail(builder, group.SpacingY());
    if (builder.failed) return;

    RawRect inner;
    inner.x = builder.Checked(rect.x + padL);
    inner.y = builder.Checked(rect.y + padT);
    inner.width = builder.Checked(rect.width - padL - padR);
    inner.height = builder.Checked(rect.height - padT - padB);

    std::vector<RawRect> authored(count);
    for (std::size_t i = 0; i < count; ++i) {
        authored[i] =
            ResolveAuthored(builder, *builder.nodes[children[i]].rect, rect);
    }
    if (builder.failed) return;

    if (group.Mode() == UILayoutMode::Grid) {
        const Raw cellW = AuthoredOrFail(builder, group.CellSizeX());
        const Raw cellH = AuthoredOrFail(builder, group.CellSizeY());
        if (builder.failed) return;
        if (cellW <= 0 || cellH <= 0) {
            builder.Fail("UI grid cell size must be positive");
            return;
        }
        const std::size_t columns =
            GridColumns(group, count, inner.width, cellW, spacingX);
        const std::size_t rows = GridRows(count, columns);
        for (std::size_t i = 0; i < count; ++i) {
            std::size_t column = 0;
            std::size_t row = 0;
            if (group.FillAxis() == UIGridFillAxis::Horizontal) {
                column = i % columns;
                row = i / columns;
            } else {
                row = i % rows;
                column = i / rows;
            }
            const Raw stepX = cellW + spacingX;
            const Raw stepY = cellH + spacingY;
            RawRect placed;
            placed.width = cellW;
            placed.height = cellH;
            const bool fromRight =
                group.StartCorner() == UIGridStartCorner::UpperRight ||
                group.StartCorner() == UIGridStartCorner::LowerRight;
            const bool fromBottom =
                group.StartCorner() == UIGridStartCorner::LowerLeft ||
                group.StartCorner() == UIGridStartCorner::LowerRight;
            placed.x = fromRight
                           ? inner.x + inner.width - cellW -
                                 static_cast<Raw>(column) * stepX
                           : inner.x + static_cast<Raw>(column) * stepX;
            placed.y = fromBottom
                           ? inner.y + inner.height - cellH -
                                 static_cast<Raw>(row) * stepY
                           : inner.y + static_cast<Raw>(row) * stepY;
            LayoutNode& child = builder.nodes[children[i]];
            // 순환 축은 격자 배치를 무시하고 저작 사각형으로 돌아간다.
            child.resolved.x = child.cyclicWidth ? authored[i].x
                                                 : builder.Checked(placed.x);
            child.resolved.width =
                child.cyclicWidth ? authored[i].width : placed.width;
            child.resolved.y = child.cyclicHeight ? authored[i].y
                                                  : builder.Checked(placed.y);
            child.resolved.height =
                child.cyclicHeight ? authored[i].height : placed.height;
        }
        return;
    }

    const bool horizontal = group.Mode() == UILayoutMode::Horizontal;
    const Raw mainSpacing = horizontal ? spacingX : spacingY;
    const Raw innerMain = horizontal ? inner.width : inner.height;
    const Raw innerCross = horizontal ? inner.height : inner.width;
    const bool controlMain =
        horizontal ? group.ControlChildWidth() : group.ControlChildHeight();
    const bool controlCross =
        horizontal ? group.ControlChildHeight() : group.ControlChildWidth();
    const bool expandMain = horizontal ? group.ChildForceExpandWidth()
                                       : group.ChildForceExpandHeight();
    const bool expandCross = horizontal ? group.ChildForceExpandHeight()
                                        : group.ChildForceExpandWidth();

    const Raw gapTotal = static_cast<Raw>(count - 1) * mainSpacing;
    const Raw available = innerMain - gapTotal;

    // Step 6a: 최소값에서 출발한다. 통제하지 않는 축은 저작 크기를 지키되
    // 최소값 아래로는 내려가지 않는다.
    std::vector<Raw> sizes(count, 0);
    std::vector<Raw> mins(count, 0);
    std::vector<Raw> prefs(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const LayoutNode& child = builder.nodes[children[i]];
        mins[i] = horizontal ? child.minWidth : child.minHeight;
        prefs[i] = horizontal ? child.prefWidth : child.prefHeight;
        const Raw authoredMain =
            horizontal ? authored[i].width : authored[i].height;
        sizes[i] = controlMain
                       ? mins[i]
                       : std::max(SelfDrivenSize(child, horizontal, authoredMain),
                                  mins[i]);
    }

    Raw used = 0;
    for (const auto size : sizes) used += size;
    if (controlMain && used < available) {
        // Step 6b: 선호까지 한 번, 그다음 유연 가중치로 한 번.
        std::vector<Raw> weights(count, 0);
        std::vector<Raw> headroom(count, 0);
        for (std::size_t i = 0; i < count; ++i) {
            weights[i] = prefs[i] - mins[i];
            headroom[i] = prefs[i] - sizes[i];
        }
        Raw remaining = 0;
        Distribute(available - used, weights, headroom, sizes, remaining);

        std::vector<Raw> flexWeights(count, 0);
        std::vector<Raw> unbounded(count, -1);
        for (std::size_t i = 0; i < count; ++i) {
            const LayoutNode& child = builder.nodes[children[i]];
            Raw flexible = 0;
            if (child.element) {
                const float authoredFlexible =
                    horizontal ? child.element->Horizontal().flexible
                               : child.element->Vertical().flexible;
                flexible = AuthoredOrFail(builder, authoredFlexible);
            }
            // forceExpand는 유연 가중치 0을 1로 바꾼다. 0으로 두면 남는 공간이
            // 아무에게도 가지 않아 expand가 아무 일도 하지 않는다.
            if (flexible <= 0 && expandMain) flexible = Fixed26_6::Scale;
            flexWeights[i] = flexible;
        }
        if (builder.failed) return;
        Raw leftover = 0;
        Distribute(remaining, flexWeights, unbounded, sizes, leftover);
    }

    Raw finalUsed = 0;
    for (const auto size : sizes) finalUsed += size;
    // Step 6c: 남은 주축 공간은 선행 오프셋만 바꾼다. 저작된 간격은 늘리지 않는다.
    const Raw leadOffset = AlignmentOffset(innerMain - finalUsed - gapTotal,
                                           MainAlignment(group, horizontal));
    const int crossAlignment = MainAlignment(group, !horizontal);

    Raw cursor = (horizontal ? inner.x : inner.y) + leadOffset;
    for (std::size_t i = 0; i < count; ++i) {
        LayoutNode& child = builder.nodes[children[i]];
        const Raw authoredCross =
            horizontal ? authored[i].height : authored[i].width;
        const Raw minCross = horizontal ? child.minHeight : child.minWidth;
        const Raw prefCross = horizontal ? child.prefHeight : child.prefWidth;
        Raw crossSize;
        if (controlCross) {
            crossSize = expandCross ? innerCross
                                    : std::min(std::max(prefCross, minCross),
                                               std::max(innerCross, minCross));
        } else {
            crossSize = SelfDrivenSize(child, !horizontal, authoredCross);
        }
        crossSize = std::max(crossSize, minCross);
        const Raw crossOrigin = (horizontal ? inner.y : inner.x) +
                                AlignmentOffset(innerCross - crossSize,
                                                crossAlignment);

        const bool cyclicMain =
            horizontal ? child.cyclicWidth : child.cyclicHeight;
        const bool cyclicCross =
            horizontal ? child.cyclicHeight : child.cyclicWidth;
        const Raw mainOrigin = cyclicMain
                                   ? (horizontal ? authored[i].x : authored[i].y)
                                   : cursor;
        const Raw mainSize =
            cyclicMain ? (horizontal ? authored[i].width : authored[i].height)
                       : sizes[i];
        const Raw finalCrossOrigin =
            cyclicCross ? (horizontal ? authored[i].y : authored[i].x)
                        : crossOrigin;
        const Raw finalCrossSize =
            cyclicCross ? (horizontal ? authored[i].height : authored[i].width)
                        : crossSize;

        if (horizontal) {
            child.resolved.x = builder.Checked(mainOrigin);
            child.resolved.width = builder.Checked(mainSize);
            child.resolved.y = builder.Checked(finalCrossOrigin);
            child.resolved.height = builder.Checked(finalCrossSize);
        } else {
            child.resolved.y = builder.Checked(mainOrigin);
            child.resolved.height = builder.Checked(mainSize);
            child.resolved.x = builder.Checked(finalCrossOrigin);
            child.resolved.width = builder.Checked(finalCrossSize);
        }
        // 순환 축이라도 흐름에서 자리를 뺏지는 않는다. 빼면 순환 하나가 형제
        // 전체의 위치를 옮겨, 순환과 무관한 노드까지 값이 달라진다.
        cursor = builder.Checked(cursor + sizes[i] + mainSpacing);
    }
}

// ── Step 7: 구동 속성 의존 그래프와 Tarjan ──────────────────────────────────
struct DriverGraph {
    std::vector<std::vector<std::size_t>> edges;
    std::vector<std::size_t> indexOf;
    std::vector<std::size_t> lowlink;
    std::vector<bool> onStack;
    std::vector<std::size_t> stack;
    std::vector<std::vector<std::size_t>> components;
    std::size_t counter = 1;
};

// 텍스트가 자기 축의 폭에 맞춰 줄바꿈하면 그 폭의 고유 크기는 자기 폭에
// 의존한다. 그 노드에 가로 fitter가 붙으면 폭이 폭을 낳는 자기 간선이다.
bool HasSizeDependentIntrinsicWidth(const LayoutNode& node) {
    const auto* label = node.object->GetComponent<UILabel>();
    return label && label->IsEnabled() &&
           label->GetWrapMode() != molga::text::TextWrapMode::NoWrap;
}

void StrongConnect(DriverGraph& graph, std::size_t v) {
    graph.indexOf[v] = graph.counter;
    graph.lowlink[v] = graph.counter;
    ++graph.counter;
    graph.stack.push_back(v);
    graph.onStack[v] = true;
    for (const auto w : graph.edges[v]) {
        if (graph.indexOf[w] == 0) {
            StrongConnect(graph, w);
            graph.lowlink[v] = std::min(graph.lowlink[v], graph.lowlink[w]);
        } else if (graph.onStack[w]) {
            graph.lowlink[v] = std::min(graph.lowlink[v], graph.indexOf[w]);
        }
    }
    if (graph.lowlink[v] != graph.indexOf[v]) return;
    std::vector<std::size_t> component;
    for (;;) {
        const std::size_t w = graph.stack.back();
        graph.stack.pop_back();
        graph.onStack[w] = false;
        component.push_back(w);
        if (w == v) break;
    }
    graph.components.push_back(std::move(component));
}

void MarkLayoutCycles(LayoutBuilder& builder) {
    const std::size_t nodeCount = builder.nodes.size();
    if (nodeCount == 0) return;
    DriverGraph graph;
    graph.edges.assign(nodeCount * 2, {});
    graph.indexOf.assign(nodeCount * 2, 0);
    graph.lowlink.assign(nodeCount * 2, 0);
    graph.onStack.assign(nodeCount * 2, false);

    auto id = [](std::size_t node, bool horizontal) {
        return node * 2 + (horizontal ? 0 : 1);
    };

    for (std::size_t i = 0; i < nodeCount; ++i) {
        const LayoutNode& node = builder.nodes[i];
        for (const bool horizontal : {true, false}) {
            if (FitterDrives(node, horizontal)) {
                // 내 크기는 내 내용에서 나온다: 내용은 레이아웃 자식들이고,
                // 줄바꿈 텍스트라면 나 자신이기도 하다.
                for (const auto child : node.layoutChildren) {
                    graph.edges[id(i, horizontal)].push_back(
                        id(child, horizontal));
                }
                if (horizontal && HasSizeDependentIntrinsicWidth(node)) {
                    graph.edges[id(i, horizontal)].push_back(id(i, horizontal));
                }
            }
            if (node.parent == kNoNode) continue;
            const LayoutNode& parent = builder.nodes[node.parent];
            if (!parent.group) continue;
            const bool parentControls = horizontal
                                            ? parent.group->ControlChildWidth()
                                            : parent.group->ControlChildHeight();
            const bool isLayoutChild =
                std::find(parent.layoutChildren.begin(),
                          parent.layoutChildren.end(),
                          i) != parent.layoutChildren.end();
            const bool gridDrives =
                parent.group->Mode() == UILayoutMode::Grid;
            if (isLayoutChild && (parentControls || gridDrives)) {
                graph.edges[id(i, horizontal)].push_back(
                    id(node.parent, horizontal));
            }
        }
    }

    for (std::size_t v = 0; v < graph.edges.size(); ++v) {
        if (graph.indexOf[v] == 0) StrongConnect(graph, v);
    }

    for (const auto& component : graph.components) {
        bool cyclic = component.size() > 1;
        if (!cyclic) {
            const std::size_t only = component.front();
            cyclic = std::find(graph.edges[only].begin(),
                               graph.edges[only].end(),
                               only) != graph.edges[only].end();
        }
        if (!cyclic) continue;

        bool horizontal = (component.front() % 2) == 0;
        std::vector<unsigned int> objectIds;
        for (const auto member : component) {
            LayoutNode& node = builder.nodes[member / 2];
            if ((member % 2) == 0) {
                node.cyclicWidth = true;
            } else {
                node.cyclicHeight = true;
            }
            objectIds.push_back(node.object->GetID());
        }
        std::sort(objectIds.begin(), objectIds.end());
        objectIds.erase(std::unique(objectIds.begin(), objectIds.end()),
                        objectIds.end());

        std::string key = horizontal ? "width" : "height";
        for (const auto objectId : objectIds) {
            key += ':';
            key += std::to_string(objectId);
        }
        if (builder.reportedCycles) {
            if (std::find(builder.reportedCycles->begin(),
                          builder.reportedCycles->end(),
                          key) != builder.reportedCycles->end()) {
                continue;
            }
            builder.reportedCycles->push_back(key);
        }
        molga::text::TextDiagnostic diagnostic;
        diagnostic.code = molga::text::TextDiagnosticCode::LayoutCycle;
        diagnostic.severity = molga::text::TextSeverity::Warning;
        diagnostic.subsystem = "ui.layout";
        diagnostic.message =
            "UI layout cycle on the " + std::string(horizontal ? "width" : "height") +
            " axis; those objects fall back to their authored RectTransform";
        diagnostic.remediation =
            "break the group/fitter dependency on that axis";
        diagnostic.sceneObjectId = objectIds.front();
        diagnostic.componentType = "UIContentSizeFitter";
        if (builder.sink) builder.sink->Report(std::move(diagnostic));
    }
}

} // namespace

namespace {

// ── 캔버스 배율을 확정된 서브트리에 정확히 한 번 적용한다 ───────────────────
// 스냅샷의 모든 사각형은 *표면 논리 단위*다 — Build가 받은 logicalViewport와
// 같은 공간이고, UISystem::CollectRender/HitTest가 쓰는
// RectTransform::GetScreenRect가 내는 그 공간이다.
//
// ScaleWithViewport 캔버스(그리고 그것이 기본값이다)는 자기 서브트리를
// viewport/scale 크기의 *캔버스* 논리 단위로 확정한다. 그 값을 그대로 게시하면
// 스냅샷 경로와 레거시 경로가 배율만큼 어긋나고, 배율이 다른 캔버스가 한
// 표면에 둘 있으면 UISnapshot::logicalViewport 하나로는 둘 다 옳게 매핑할 수
// 없다(Task 11.2의 UIPhysicalTransform은 표면마다 논리 뷰포트 하나만 받는다).
//
// float 배율을 다시 곱하지 않는다. 캔버스 논리 크기 자체가 이미
// viewport/scale을 26.6으로 확정한 값이므로 viewportRaw/logicalRaw가 그 배율의
// 정확한 유리수 표현이고, 검증된 정수 산술로 적용된다. 그래서 캔버스 뿌리는
// 뷰포트에 정확히 겹치고, ConstantPixelSize에서는 두 값이 같아 아무 일도
// 일어나지 않는다.
void ScaleSubtree(LayoutBuilder& builder, std::size_t index, Raw numeratorX,
                  Raw denominatorX, Raw numeratorY, Raw denominatorY) {
    LayoutNode& node = builder.nodes[index];
    const auto axis = [&builder](Raw value, Raw numerator,
                                 Raw denominator) -> Raw {
        Raw out = 0;
        if (!MulDiv(value, numerator, denominator, out)) {
            builder.Fail("UI canvas scale left the 26.6 range");
            return 0;
        }
        return out;
    };
    node.resolved.x = axis(node.resolved.x, numeratorX, denominatorX);
    node.resolved.width = axis(node.resolved.width, numeratorX, denominatorX);
    node.resolved.y = axis(node.resolved.y, numeratorY, denominatorY);
    node.resolved.height = axis(node.resolved.height, numeratorY, denominatorY);
    // 고유 크기도 같은 공간으로 옮긴다. 한 스냅샷 안에서 사각형과 고유 크기가
    // 다른 단위를 쓰면 그 둘을 비교하는 소비자가 조용히 틀린다.
    node.prefWidth = axis(node.prefWidth, numeratorX, denominatorX);
    node.prefHeight = axis(node.prefHeight, numeratorY, denominatorY);
    for (const auto child : node.children) {
        ScaleSubtree(builder, child, numeratorX, denominatorX, numeratorY,
                     denominatorY);
    }
}

// ── Task 11.3 Step 6a: content 서브트리를 런타임 오프셋만큼 옮긴다 ──────────
// 뷰포트는 움직이지 않고 내용만 움직인다. 그래서 이 함수는 content 노드와 그
// 자손만 건드리고, 그 위의 마스크/뷰포트 사각형은 제자리에 남아 잘라 낸다.
void TranslateSubtree(LayoutBuilder& builder, std::size_t index, Raw dx,
                      Raw dy) {
    LayoutNode& node = builder.nodes[index];
    const Raw x = node.resolved.x + dx;
    const Raw y = node.resolved.y + dy;
    if (!InRawRange(x) || !InRawRange(y)) {
        builder.Fail("UI scroll displacement left the 26.6 range");
        return;
    }
    node.resolved.x = x;
    node.resolved.y = y;
    for (const auto child : node.children) {
        TranslateSubtree(builder, child, dx, dy);
    }
}

// ── Step 4a/4b: 게시되는 클립은 검증된 교집합 하나에서만 나온다 ─────────────
// 예전에는 검증되지 않은 두 번째 사본(IntersectRects)이 실제 경로에 있었고,
// Step 4a가 요구한 IntersectFixedRects는 단위 시험만 붙들고 있었다. 규칙의
// 사본이 둘이면 시험이 가리키는 쪽과 화면에 나오는 쪽이 갈리고, 그 어긋남은
// 관찰되지 않는다. 사본은 하나다.
void ComputeClips(LayoutBuilder& builder, std::size_t index,
                  const std::optional<molga::FixedRect>& inheritedClip,
                  bool droppedAncestor) {
    LayoutNode& node = builder.nodes[index];
    node.logicalClip = inheritedClip;
    node.dropped = droppedAncestor;
    const auto resolved = ToFixedRect(node.resolved);
    if (!resolved) {
        builder.Fail("UI layout value left the 26.6 range");
        return;
    }
    if (inheritedClip && !droppedAncestor) {
        if (!IntersectFixedRects(*inheritedClip, *resolved)) node.dropped = true;
    }

    std::optional<molga::FixedRect> childClip = inheritedClip;
    if (node.mask && node.mask->ClipsDescendants()) {
        childClip = childClip ? IntersectFixedRects(*childClip, *resolved)
                              : resolved;
        if (!childClip) {
            // 빈 교집합은 자손 전체를 제거한다. 여기서 clip을 유지하면 자손이
            // 자기 조상보다 넓은 영역에 그려진다.
            const std::optional<molga::FixedRect> empty = molga::FixedRect{};
            for (const auto child : node.children) {
                ComputeClips(builder, child, empty, true);
            }
            return;
        }
    }
    for (const auto child : node.children) {
        ComputeClips(builder, child, childClip, node.dropped);
    }
}

bool ObjectIsInteractionEligible(GameObject& object) {
    bool eligible = false;
    if (auto* selectable = EnabledComponent<UISelectable>(object)) {
        eligible = eligible || selectable->Interactable();
    }
    if (auto* button = EnabledComponent<UIButton>(object)) {
        eligible = eligible || button->IsInteractable();
    }
    return eligible;
}

} // namespace

namespace {

// 이 표면이 기억하는 페이로드 사실의 상한. 넘으면 더 기억하지 않으므로 그
// 뒤의 같은 사실은 다시 흐른다 — 상한이 진단을 영구히 삼키는 것보다 낫다.
constexpr std::size_t kMaxRememberedPayloadFacts = 256;

// ── Step 3a: 정규 키 조립 ───────────────────────────────────────────────────
UIStableComponentKey MakeStableKey(unsigned int objectId,
                                   const Component& component,
                                   std::uint32_t schemaVersion) {
    UIStableComponentKey key;
    key.sceneObjectId = objectId;
    key.componentTypeName = component.GetTypeName();
    key.componentSchemaVersion = schemaVersion;
    return key;
}

UIFrozenTarget MakeFrozenTarget(const World& world, unsigned int objectId,
                                const Component& component,
                                std::uint32_t schemaVersion) {
    UIFrozenTarget target;
    target.runtimeTarget = CaptureTarget(world, component);
    target.canonicalTarget = MakeStableKey(objectId, component, schemaVersion);
    return target;
}

molga::text::TextHorizontalAlignment ToLayoutAlignment(
    UILabel::HorizontalAlignment value) {
    switch (value) {
        case UILabel::HorizontalAlignment::Center:
            return molga::text::TextHorizontalAlignment::Center;
        case UILabel::HorizontalAlignment::Right:
            return molga::text::TextHorizontalAlignment::Right;
        case UILabel::HorizontalAlignment::Left:
            break;
    }
    return molga::text::TextHorizontalAlignment::Left;
}

molga::text::TextVerticalAlignment ToLayoutAlignment(
    UILabel::VerticalAlignment value) {
    switch (value) {
        case UILabel::VerticalAlignment::Middle:
            return molga::text::TextVerticalAlignment::Middle;
        case UILabel::VerticalAlignment::Bottom:
            return molga::text::TextVerticalAlignment::Bottom;
        case UILabel::VerticalAlignment::Top:
            break;
    }
    return molga::text::TextVerticalAlignment::Top;
}

// 확정된 26.6 사각형을 그대로 제약으로 싣는다. float 화면 사각형을 다시
// 만들지 않는 것이 요점이다 — 두 경로가 각자 반올림하면 같은 라벨이 배치와
// 렌더에서 서로 다른 줄로 접힌다. 음수 크기는 제약이 아니라 오류이므로
// 제약 없음으로 남긴다("폭 0으로 접어라"와 구분되어야 한다).
molga::text::TextLayoutRequest BuildLabelRequestFixed(
    const UILabel& label, unsigned int objectId, const molga::FixedRect& rect) {
    molga::text::TextLayoutRequest request;
    request.utf8 = label.GetText();
    const UILabel::FontFamilyView family = label.ResolveFontFamilyView();
    if (!family.familyGuid.empty()) {
        request.style.fontFamilyGuid = family.familyGuid;
    } else if (!family.faceFontGuids.empty()) {
        request.style.legacyFontGuid = family.faceFontGuids.front();
    }
    if (const auto fontSize = Fixed26_6::FromFloat(label.GetFontSizePx())) {
        request.style.shape.fontSize = *fontSize;
    }
    request.style.analysis.locale = label.GetLocale();
    request.style.analysis.baseDirection = label.GetBaseDirection();
    request.style.wrap = label.GetWrapMode();
    request.style.overflow = label.GetOverflowMode();
    request.style.maxLines = label.GetMaxLines();
    request.style.horizontal = ToLayoutAlignment(label.GetHorizontalAlignment());
    request.style.vertical = ToLayoutAlignment(label.GetVerticalAlignment());
    if (const auto spacing = Fixed26_6::FromFloat(label.GetLineSpacing())) {
        request.style.lineSpacing = *spacing;
    }
    if (rect.width.Raw() >= 0) request.constraints.width = rect.width;
    if (rect.height.Raw() >= 0) request.constraints.height = rect.height;
    request.diagnosticContext.componentType = "UILabel";
    request.diagnosticContext.sceneObjectId = objectId;
    return request;
}

// ── Step 1h/3f: 유효 입력 요청 ──────────────────────────────────────────────
// 최상위 UITextInput::fontFamilyGuid 하나가 family 권한이다. 문단 스타일을
// 복사한 뒤 그 필드를 덮어쓴다 — 중첩된 두 번째 family 값을 소비하거나
// 직렬화하면 저작자가 두 곳을 고쳐야 하고, 그중 하나는 반드시 잊힌다.
molga::text::ParagraphStyle EffectiveInputParagraphStyle(
    const UITextInput& input) {
    molga::text::ParagraphStyle style;
    const UIAuthoredParagraphStyle& authored = input.ParagraphStyle();
    if (const auto fontSize = Fixed26_6::FromFloat(authored.fontSizePx)) {
        style.shape.fontSize = *fontSize;
    }
    if (const auto spacing = Fixed26_6::FromFloat(authored.lineSpacing)) {
        style.lineSpacing = *spacing;
    }
    style.analysis.locale = authored.locale;
    style.analysis.baseDirection = authored.baseDirection;
    style.wrap = authored.wrap;
    style.overflow = authored.overflow;
    style.maxLines = authored.maxLines;
    style.horizontal = authored.horizontal;
    style.vertical = authored.vertical;
    style.fontFamilyGuid = input.FontFamilyGuid();
    return style;
}

// ── Step 3f: 입력창의 유효 요청 한 벌 ───────────────────────────────────────
// 예약 구간을 세는 쪽과 게시하는 쪽이 같은 요청을 봐야 한다. 두 벌이면 예약된
// 칸 수와 실제로 그려질 기록 수가 어긋나고, 그 어긋남은 다음 항목의 정렬 키가
// 이미 쓰인 뒤에야 드러난다.
molga::text::TextLayoutRequest BuildEffectiveInputRequest(
    const UITextInput& input, const std::string& visibleUtf8,
    const molga::FixedSize& logicalViewport,
    unsigned int renderedLabelObjectId) {
    molga::text::TextLayoutRequest request;
    request.utf8 = visibleUtf8;
    request.style = EffectiveInputParagraphStyle(input);
    request.constraints.width = logicalViewport.width;
    request.constraints.height = logicalViewport.height;
    request.diagnosticContext.componentType = "UILabel";
    request.diagnosticContext.sceneObjectId = renderedLabelObjectId;
    return request;
}

// ── Step 4d: 입력창이 소유한 라벨 ───────────────────────────────────────────
// 한 입력창이 요구한 두 참조의 해석 결과. 0은 "그 역할은 유효하지 않다"이며,
// 유효하지 않은 rendered는 입력창의 시각/텍스트 대상을 통째로 끄고, 유효하지
// 않은 placeholder는 placeholder 출력만 끈다.
struct InputLabelClaim {
    GameObject* object = nullptr;
    UITextInput* input = nullptr;
    UILabel* rendered = nullptr;
    UILabel* placeholder = nullptr;
    bool renderedRefAuthored = false;
    bool placeholderRefAuthored = false;
    bool conflicted = false;
};

// Build는 프레임마다 돈다. 배치할 수 없는 라벨은 고쳐질 때까지 계속 그러하므로,
// 상한 없이 흘리면 로그가 그 하나로 가득 차고 진짜 새 진단이 그 안에 묻힌다.
// 같은 사실을 이 표면에서 한 번만 통과시킨다 — 억제된 진단이 정보를 잃지는
// 않는다. 권한 있는 완전한 기록은 진단 스트림이 아니라
// TextLayout::validationFacts이고 그쪽에는 상한이 없다.
class RateLimitedPayloadSink final : public molga::text::TextDiagnosticSink {
public:
    RateLimitedPayloadSink(molga::text::TextDiagnosticSink& target,
                           std::vector<std::string>& seen,
                           std::uint64_t worldGeneration,
                           unsigned int objectId)
        : target_(target),
          seen_(seen),
          worldGeneration_(worldGeneration),
          objectId_(objectId) {}

    void Report(molga::text::TextDiagnostic diagnostic) override {
        // 월드 세대가 키에 들어간다. 씬을 다시 열면 오브젝트 id는 낮은 값부터
        // 다시 쓰이므로, 세대가 없으면 *죽은* 월드의 7번이 이미 보고한 사실
        // 때문에 새 월드 7번의 진짜 첫 진단이 조용히 사라진다.
        std::string key = "label:";
        key += std::to_string(worldGeneration_);
        key += ':';
        key += std::to_string(objectId_);
        key += ':';
        key += std::to_string(static_cast<int>(diagnostic.code));
        key += ':';
        key += diagnostic.message;
        if (std::find(seen_.begin(), seen_.end(), key) != seen_.end()) return;
        // 기억할 수 없으면 보고하지 않는다. 기억하지 않고 보고만 하면 상한이
        // 메모리를 지키는 그 순간에 rate limit이 사라진다(위 NotePayloadFact와
        // 같은 규칙이고, 같은 인계 결함이다).
        if (seen_.size() >= kMaxRememberedPayloadFacts) return;
        seen_.push_back(std::move(key));
        target_.Report(std::move(diagnostic));
    }

private:
    molga::text::TextDiagnosticSink& target_;
    std::vector<std::string>& seen_;
    std::uint64_t worldGeneration_;
    unsigned int objectId_;
};

// 측정 전용 pass의 진단을 버린다. 같은 내용을 렌더 요청이 이미 제 문맥으로
// 보고하므로, 여기서 한 번 더 흘리면 잘못된 문자열 하나가 프레임마다 두 줄씩
// 찍힌다. 상태가 없으므로 프레임마다 만들어도 할당이 없다.
class DiscardingPayloadSink final : public molga::text::TextDiagnosticSink {
public:
    void Report(molga::text::TextDiagnostic) override {}
};

UILabel* ResolveClaimedLabel(World& world, SceneObjectRef ref,
                             const std::vector<unsigned int>& reachable) {
    if (!ref.IsSet()) return nullptr;
    GameObject* object = world.FindById(ref.ObjectId());
    if (!object) return nullptr;
    // 같은 Canvas 트리 안에서 실제로 배치된 오브젝트여야 한다. 비활성 조상
    // 아래의 라벨이나 다른 트리의 라벨을 붙들면, 화면에 없는 라벨이 입력창의
    // 글을 소유했다고 주장하게 된다.
    if (std::find(reachable.begin(), reachable.end(), object->GetID()) ==
        reachable.end()) {
        return nullptr;
    }
    UILabel* label = object->GetComponent<UILabel>();
    if (!label || !label->IsEnabled()) return nullptr;
    return label;
}

} // namespace

// ── Step 4f-4k, 8a-8b ───────────────────────────────────────────────────────
struct UILayoutSystem::Impl {
    struct GeometryEntry {
        std::size_t hash = 0;
        UILayoutGeometryCacheKey key;
        std::vector<UIDrawOrderKey> order;
        std::vector<UILayoutNodeSnapshot> nodes;
        // 클립으로 떨어진 노드는 기하의 일부다. 적중했을 때 이것을 복원하지
        // 않으면 ComputeClips가 돌지 않아 모든 dropped가 false로 남고, 아래
        // 키 벡터가 잘려 나갔어야 할 노드까지 담는다 — 같은 상태가 캐시 적중
        // 여부에 따라 서로 다른 키를 만든다.
        std::vector<std::uint8_t> dropped;
    };
    struct FullSlot {
        UISnapshotWorldDeviceSlotKey slot;
        UISnapshotCacheKey key;
        UISnapshotPtr snapshot;
    };

    // ── 기하 조회 규칙은 한 벌뿐이다 ────────────────────────────────────────
    // 해시는 후보를 좁히기만 하고, 적중은 언제나 원래의 순서 있는 필드를 전부
    // 다시 비교한 뒤에만 난다. 이 규칙이 두 곳에 복사되어 있으면 관찰용
    // 접근자가 프로덕션과 다른 프로그램을 재게 되고, 프로덕션 쪽만 해시를
    // 믿도록 망가져도 접근자를 쓰는 시험은 전부 통과한다.
    template <class List>
    static auto MatchGeometry(List& lru, std::size_t hash,
                              const UILayoutGeometryCacheKey& key) {
        auto it = lru.begin();
        for (; it != lru.end(); ++it) {
            if (it->hash != hash) continue;
            if (it->key == key) break;
        }
        return it;
    }

    // 월드마다 최대 256개. front가 MRU다.
    std::unordered_map<std::uint64_t, std::list<GeometryEntry>> geometry;
    std::vector<FullSlot> fullSlots;

    // 슬롯을 버리는 규칙은 이 한 자리뿐이다. 세 호출자(Build의 게으른 감지,
    // OnDeviceGenerationChanged, ClearFullSnapshotBindingCache)가 각자 지우면
    // 언젠가 한 곳이 조건을 뒤집고, 그때 낡은 장치의 핸들이 살아남는다.
    template <class Predicate>
    void EraseFullSlots(Predicate predicate) {
        fullSlots.erase(
            std::remove_if(fullSlots.begin(), fullSlots.end(), predicate),
            fullSlots.end());
    }

    UILayoutFastPathStamp lastStamp;
    bool hasStamp = false;
    UISnapshotPtr lastSnapshot;

    // 뷰포트 세대는 "지금까지 몇 번 바뀌었나"가 아니라 "이 뷰포트 값의 이름"이다.
    // 단순 카운터로 두면 서로 다른 두 표면이 번갈아 그릴 때 1,2,3,4…로 올라가고,
    // 같은 표면이 같은 뷰포트로 돌아와도 예전 이름을 되찾지 못해 두 빌드가
    // 기하 키를 절대 공유하지 못한다. 그게 정확히 에디터의 모양이다 —
    // Scene View는 패널 픽셀로, Game View는 게임 논리 크기로, 같은 프레임에
    // 같은 월드를 그린다. 그러면 LRU는 100% 미스가 되어 순수한 비용이 된다.
    //
    // 값마다 이름을 기억해 돌려준다. 목록은 MRU 순서로 짧게 유지한다 — 창을
    // 드래그해 크기를 바꾸면 뷰포트 값이 수천 개 지나가므로 무한히 기억할 수는
    // 없다. 밀려난 값이 돌아오면 새 이름을 받는데, 그건 캐시 미스일 뿐 옛
    // 이름을 새 상태에 다시 붙이는 일은 아니다.
    struct ViewportGenerationEntry {
        molga::FixedSize viewport;
        std::uint64_t generation = 0;
    };
    static constexpr std::size_t kRememberedViewports = 8;
    std::vector<ViewportGenerationEntry> viewportGenerations;
    std::size_t lastVisualKeyEntryCount = 0;
    std::uint64_t lastAcquiredViewportGeneration = 0;
    bool viewportGenerationCacheable = true;

    // 이 뷰포트 값의 이름을 돌려준다. 처음 보는 값에만 새 이름을 발급한다.
    std::uint64_t GenerationForViewport(const molga::FixedSize& viewport) {
        for (std::size_t index = 0; index < viewportGenerations.size(); ++index) {
            if (viewportGenerations[index].viewport != viewport) continue;
            ViewportGenerationEntry entry = viewportGenerations[index];
            viewportGenerations.erase(viewportGenerations.begin() +
                                      static_cast<std::ptrdiff_t>(index));
            viewportGenerations.insert(viewportGenerations.begin(), entry);
            return entry.generation;
        }
        // 세대는 감기지 않는다. 소진되면 빠른 경로와 두 캐시를 함께 끈다 —
        // 감아서 재사용하면 옛 항목이 새 뷰포트와 같은 정체성을 갖는다.
        if (lastAcquiredViewportGeneration ==
            std::numeric_limits<std::uint64_t>::max()) {
            viewportGenerationCacheable = false;
            return lastAcquiredViewportGeneration;
        }
        ++lastAcquiredViewportGeneration;
        viewportGenerations.insert(
            viewportGenerations.begin(),
            ViewportGenerationEntry{viewport, lastAcquiredViewportGeneration});
        if (viewportGenerations.size() > kRememberedViewports) {
            viewportGenerations.pop_back();
        }
        return lastAcquiredViewportGeneration;
    }
    std::uint64_t lastDeviceGeneration = 0;

    std::uint64_t geometryBuilds = 0;
    std::uint64_t keyBuilds = 0;
    std::uint64_t keyAllocations = 0;
    std::uint64_t layoutRevision = 0;

    std::vector<std::string> reportedCycles;
    std::string lastUncacheableKey;
    std::optional<UILayoutGeometryCacheKey> lastGeometryKey;
    std::optional<UISnapshotCacheKey> lastSnapshotKey;

    // ── 페이로드 진단의 상한 ────────────────────────────────────────────────
    // Build는 프레임마다 돈다. 없는 텍스처나 배치할 수 없는 라벨은 고쳐질
    // 때까지 계속 없으므로, 상한이 없으면 로그가 그 하나로 가득 차고 진짜
    // 새 진단이 그 안에 묻힌다. 같은 사실을 이 표면에서 한 번만 낸다.
    // reportedCycles와 같은 규약이다.
    std::vector<std::string> reportedPayloadFacts;
    // 상한에 닿았다고 알린 적이 있는가. 억제 자체가 보이지 않으면 "로그가
    // 조용하다"와 "로그가 억제되었다"가 구별되지 않는다.
    bool payloadFactBudgetAnnounced = false;
    // ── 인계받은 결함 1 (Task 11.1 -> 11.2) ─────────────────────────────────
    // 예전 구현은 256칸이 차면 기억을 멈추면서도 **참을 계속 돌려주었다**.
    // 그래서 상한은 메모리를 지키는 동시에 rate limit을 없앴다: 서로 다른
    // 사실을 256개 넘게 내는 장면은 257번째부터 매 프레임 전부 다시 보고되고,
    // 그것이 정확히 상한이 막으려던 상태다.
    //
    // 이제 기억할 수 없으면 보고도 하지 않는다. 새 문제 하나가 늦게 묻히는
    // 것은 로그가 프레임마다 수백 줄로 넘치는 것보다 낫고, 억제는 아래 한 줄로
    // 보인다. 은퇴한 월드의 사실은 OnWorldReleased가 거둬 가므로 이 상한이
    // 죽은 월드로 영구히 포화되지는 않는다.
    bool NotePayloadFact(const std::string& key) {
        if (std::find(reportedPayloadFacts.begin(), reportedPayloadFacts.end(),
                      key) != reportedPayloadFacts.end()) {
            return false;
        }
        if (reportedPayloadFacts.size() >= kMaxRememberedPayloadFacts) {
            if (!payloadFactBudgetAnnounced) {
                payloadFactBudgetAnnounced = true;
                Log::Warn("UILayout",
                          "this UI surface reached its " +
                              std::to_string(kMaxRememberedPayloadFacts) +
                              " distinct payload-diagnostic limit; further "
                              "distinct payload facts are suppressed rather "
                              "than repeated every frame");
            }
            return false;
        }
        reportedPayloadFacts.push_back(key);
        return true;
    }

    // Step 4f: 용량을 유지하는 스크래치. 변경된 프레임에서만 resize/덮어쓰기를
    // 하고, 완성된 키는 미스일 때만 캐시 소유로 복사한다.
    std::vector<std::uint64_t> canvasScratch;
    std::vector<std::uint64_t> hierarchyScratch;
    std::vector<std::uint64_t> rectScratch;
    std::vector<std::uint64_t> intrinsicScratch;
    std::vector<UIVisualCacheIdentity> visualScratch;
    std::vector<UIInteractionCacheIdentity> interactionScratch;

    void ReportInvalid(molga::text::TextDiagnosticSink& sink,
                       const std::string& message,
                       const std::string& remediation) {
        molga::text::TextDiagnostic diagnostic;
        diagnostic.code = molga::text::TextDiagnosticCode::LayoutInvalid;
        diagnostic.severity = molga::text::TextSeverity::Error;
        diagnostic.subsystem = "ui.layout";
        diagnostic.message = message;
        diagnostic.remediation = remediation;
        sink.Report(std::move(diagnostic));
    }
};

UILayoutSystem::UILayoutSystem() : impl_(std::make_unique<Impl>()) {}
UILayoutSystem::~UILayoutSystem() = default;

std::uint64_t UILayoutSystem::GeometryBuildCount() const noexcept {
    return impl_->geometryBuilds;
}
std::size_t UILayoutSystem::LastVisualKeyEntryCount() const noexcept {
    return impl_->lastVisualKeyEntryCount;
}

std::uint64_t UILayoutSystem::SnapshotKeyBuildCount() const noexcept {
    return impl_->keyBuilds;
}
std::uint64_t UILayoutSystem::SnapshotKeyAllocationCount() const noexcept {
    return impl_->keyAllocations;
}
std::size_t UILayoutSystem::GeometryCacheEntryCountForWorld(
    std::uint64_t worldGeneration) const noexcept {
    const auto found = impl_->geometry.find(worldGeneration);
    return found == impl_->geometry.end() ? 0 : found->second.size();
}
std::size_t UILayoutSystem::FullSnapshotCacheEntryCountForWorldDevice(
    UISnapshotWorldDeviceSlotKey slot) const noexcept {
    std::size_t total = 0;
    for (const auto& entry : impl_->fullSlots) {
        if (entry.slot == slot) ++total;
    }
    return total;
}
bool UILayoutSystem::GeometryCacheContains(
    const UILayoutGeometryCacheKey& key) const {
    const auto found = impl_->geometry.find(key.worldGeneration);
    if (found == impl_->geometry.end()) return false;
    // 프로덕션 조회와 정확히 같은 함수다. 규칙을 여기 다시 쓰면 프로덕션 쪽만
    // 해시를 믿도록 망가져도 이 접근자를 쓰는 시험은 전부 통과한다.
    return Impl::MatchGeometry(found->second,
                               HashUILayoutGeometryCacheKey(key),
                               key) != found->second.end();
}
std::optional<UISnapshotCacheKey> UILayoutSystem::LastSnapshotKey() const {
    return impl_->lastSnapshotKey;
}

std::optional<UILayoutGeometryCacheKey> UILayoutSystem::LastGeometryKey() const {
    return impl_->lastGeometryKey;
}

void UILayoutSystem::OnDeviceGenerationChanged(std::uint64_t oldGeneration,
                                              std::uint64_t newGeneration) {
    // 0은 "장치 없음"이고 같은 값은 변화가 아니다. 둘 다 아무것도 버리지
    // 않는다 — 여기서 버리면 정상적인 프레임이 캐시를 통째로 잃는다.
    if (newGeneration == 0U || newGeneration == oldGeneration) return;
    impl_->EraseFullSlots([newGeneration](const Impl::FullSlot& slot) {
        return slot.slot.deviceGeneration != newGeneration;
    });
    // 빠른 경로 포인터는 세대와 무관하게 놓는다. 그 포인터가 가리키는
    // 스냅샷은 옛 장치의 네이티브 핸들을 담고 있을 수 있고, 도장 비교만으로는
    // 그것을 알 수 없다.
    impl_->hasStamp = false;
    impl_->lastStamp = UILayoutFastPathStamp{};
    impl_->lastSnapshot.reset();
    if (impl_->lastSnapshotKey &&
        impl_->lastSnapshotKey->deviceGeneration != newGeneration) {
        impl_->lastSnapshotKey.reset();
    }
    impl_->lastDeviceGeneration = newGeneration;
    // 기하 LRU와 정규 스크래치는 손대지 않는다.
}

void UILayoutSystem::ClearFullSnapshotBindingCache(
    std::uint64_t deviceGeneration) {
    if (deviceGeneration == 0U) return;
    impl_->EraseFullSlots([deviceGeneration](const Impl::FullSlot& slot) {
        return slot.slot.deviceGeneration == deviceGeneration;
    });
    if (impl_->hasStamp &&
        impl_->lastStamp.deviceGeneration == deviceGeneration) {
        impl_->hasStamp = false;
        impl_->lastStamp = UILayoutFastPathStamp{};
        impl_->lastSnapshot.reset();
    }
    if (impl_->lastSnapshotKey &&
        impl_->lastSnapshotKey->deviceGeneration == deviceGeneration) {
        impl_->lastSnapshotKey.reset();
    }
    // 이 세대는 은퇴했다. 이름을 그대로 두면 배치 시스템이 teardown 뒤에도
    // 죽은 세대를 "지난번 장치"로 부르고, 다음 Build의 지연 재탐지가 그
    // 이름과 새 세대를 비교한다 — 두 값이 우연히 같아지는 순간(세대 축이
    // 되감기거나 같은 값이 다시 발행되면) 그 재탐지는 아무 일도 하지 않는다.
    if (impl_->lastDeviceGeneration == deviceGeneration) {
        impl_->lastDeviceGeneration = 0U;
    }
}

void UILayoutSystem::OnWorldReleased(std::uint64_t worldGeneration) {
    impl_->geometry.erase(worldGeneration);
    impl_->fullSlots.erase(
        std::remove_if(impl_->fullSlots.begin(), impl_->fullSlots.end(),
                       [worldGeneration](const Impl::FullSlot& slot) {
                           return slot.slot.worldGeneration == worldGeneration;
                       }),
        impl_->fullSlots.end());
    if (impl_->hasStamp && impl_->lastStamp.worldGeneration == worldGeneration) {
        impl_->hasStamp = false;
        impl_->lastSnapshot.reset();
    }
    if (impl_->lastGeometryKey &&
        impl_->lastGeometryKey->worldGeneration == worldGeneration) {
        impl_->lastGeometryKey.reset();
    }
    if (impl_->lastSnapshotKey &&
        impl_->lastSnapshotKey->geometry.worldGeneration == worldGeneration) {
        // lastGeometryKey의 형제다. 은퇴한 월드의 키를 계속 돌려주면 그 안의
        // runtimeBindings(텍스처/샘플러 핸들)와 inputVisualStates가 죽은 월드의
        // 것인 채로 관찰된다.
        impl_->lastSnapshotKey.reset();
    }
    // ── M35 rate limiter의 수명 ────────────────────────────────────────────
    // 기억된 사실은 그 월드의 것이다. 은퇴한 월드의 사실을 계속 들고 있으면
    // 256칸 상한이 죽은 월드들로 영구히 포화되고, 그 뒤로는 어떤 진단도 이
    // 프로세스에서 다시 나오지 않는다.
    impl_->reportedPayloadFacts.clear();
    impl_->reportedCycles.clear();
    UIIntrinsicLayoutRegistry::Get().ReleaseWorld(worldGeneration);
}


UISnapshotPtr UILayoutSystem::Build(
    World& world, molga::WindowId surfaceWindowId,
    molga::FixedSize logicalViewport,
    const UITextInputVisualStateProvider& inputVisualStates,
    molga::text::TextLayoutService& textLayout,
    molga::text::TextDiagnosticSink& sink) {
    Impl& impl = *impl_;
    // Step 4a: 표면과 뷰포트를 키 구성 전에 거절한다. 0인 창이나 비어 있는
    // 뷰포트로 만든 빈 스냅샷은 "UI가 하나도 없는 프레임"과 구분되지 않는다.
    if (surfaceWindowId == 0) {
        impl.ReportInvalid(sink, "UI layout needs a nonzero surface window",
                           "pass the exact UI surface window id");
        return nullptr;
    }
    if (logicalViewport.width.Raw() <= 0 || logicalViewport.height.Raw() <= 0) {
        impl.ReportInvalid(sink, "UI layout needs a positive logical viewport",
                           "pass a viewport with positive 26.6 extents");
        return nullptr;
    }

    const std::uint64_t viewportGeneration =
        impl.GenerationForViewport(logicalViewport);

    // const가 아니다: 아래에서 라벨 고유 크기를 게시한 뒤 그 결과를 이 프레임이
    // 그대로 소비해야 하므로 한 번 다시 읽는다.
    auto clock = UIRuntimeInvalidationClock::Current();
    // 장치가 새로 만들어졌는데 아무도 알려 주지 않았으면 여기서 알아챈다.
    // 규칙은 OnDeviceGenerationChanged 한 벌뿐이다 — 두 벌이면 명시 경로만
    // 고치고 이 경로를 잊는 회귀가 조용히 통과한다.
    OnDeviceGenerationChanged(impl.lastDeviceGeneration, clock.deviceGeneration);
    impl.lastDeviceGeneration = clock.deviceGeneration;

    UILayoutFastPathStamp stamp;
    stamp.surfaceWindowId = surfaceWindowId;
    stamp.worldGeneration = world.Generation();
    stamp.logicalViewport = logicalViewport;
    stamp.viewportGeneration = viewportGeneration;
    stamp.semanticDirtyGeneration = clock.semanticDirtyGeneration;
    stamp.scrollDisplacementGeneration = clock.scrollDisplacementGeneration;
    stamp.textureBindingGeneration = clock.textureBindingGeneration;
    stamp.deviceGeneration = clock.deviceGeneration;

    bool clocksCacheable = clock.cacheable && impl.viewportGenerationCacheable;
    if (clocksCacheable && impl.hasStamp && impl.lastSnapshot &&
        stamp == impl.lastStamp) {
        return impl.lastSnapshot;
    }

    // ── Step 5a: 활성 Canvas 트리를 모은다 ──────────────────────────────────
    LayoutBuilder builder;
    builder.world = &world;
    builder.sink = &sink;
    builder.reportedCycles = &impl.reportedCycles;

    std::vector<std::size_t> canvasRoots;
    std::vector<RawRect> canvasRects;
    // 캔버스 뿌리마다 그 서브트리를 표면 논리 단위로 되돌리는 정확한 유리수.
    // {numeratorX, denominatorX, numeratorY, denominatorY}.
    std::vector<std::array<Raw, 4>> canvasScales;
    std::uint32_t rootOrdinal = 0;
    for (const auto& object : world.Objects()) {
        if (!object || !IsCanvasRoot(*object) || !IsHierarchyActive(object.get())) {
            continue;
        }
        auto* canvas = object->GetComponent<UICanvas>();
        // 캔버스 배율만이 저작된 float 정책을 그대로 쓰는 유일한 자리다.
        // geometric interpolation은 유리수로 표현되지 않으므로, 여기서 한 번
        // 논리 크기를 확정한 뒤 그 아래의 모든 측정/배치는 raw 정수로만 한다.
        const Vector2 viewportPixels{logicalViewport.width.ToFloat(),
                                     logicalViewport.height.ToFloat()};
        const Vector2 logicalSize = canvas->LogicalSize(viewportPixels);
        Raw logicalWidth = 0;
        Raw logicalHeight = 0;
        if (!AuthoredToRaw(logicalSize.x, logicalWidth) ||
            !AuthoredToRaw(logicalSize.y, logicalHeight) || logicalWidth <= 0 ||
            logicalHeight <= 0) {
            builder.Fail("UI canvas logical size is not a finite 26.6 quantity");
            break;
        }
        const std::size_t first = builder.nodes.size();
        GatherSubtree(builder, object.get(), kNoNode, {rootOrdinal},
                      canvas->GetSortingOrder(), true);
        ++rootOrdinal;
        if (builder.nodes.size() > first) {
            canvasRoots.push_back(first);
            canvasRects.push_back(RawRect{0, 0, logicalWidth, logicalHeight});
            canvasScales.push_back(
                {static_cast<Raw>(logicalViewport.width.Raw()), logicalWidth,
                 static_cast<Raw>(logicalViewport.height.Raw()), logicalHeight});
        }
    }
    if (builder.failed) return nullptr;

    // ── 고유 크기와 취소 불가능한 revision ──────────────────────────────────
    bool uncacheable = !clocksCacheable;
    std::string uncacheableKey;
    // 소진된 시계도 이름이 있어야 한다. 빈 이름을 쓰면 lastUncacheableKey의
    // 초기값과 같아 첫 소진 프레임이 진단 하나 없이 지나가고, 캐시가 통째로
    // 꺼진 사실이 성능으로만 드러난다.
    if (!clocksCacheable) uncacheableKey = "clock;";
    auto noteComponent = [&](const UIComponent* component, unsigned int objectId,
                             const char* typeName) {
        if (!component || component->RevisionCacheable()) return;
        uncacheable = true;
        uncacheableKey += typeName;
        uncacheableKey += ':';
        uncacheableKey += std::to_string(objectId);
        uncacheableKey += ';';
    };

    for (auto& node : builder.nodes) {
        GameObject& object = *node.object;
        const unsigned int objectId = object.GetID();
        noteComponent(node.rect, objectId, "RectTransform");
        noteComponent(node.canvas, objectId, "UICanvas");
        noteComponent(node.group, objectId, "UILayoutGroup");
        noteComponent(node.element, objectId, "UILayoutElement");
        noteComponent(node.fitter, objectId, "UIContentSizeFitter");
        noteComponent(node.mask, objectId, "UIMask");
        noteComponent(object.GetComponent<UIImage>(), objectId, "UIImage");
        noteComponent(object.GetComponent<UILabel>(), objectId, "UILabel");
        noteComponent(object.GetComponent<UIButton>(), objectId, "UIButton");
        noteComponent(object.GetComponent<UITextInput>(), objectId, "UITextInput");
        noteComponent(object.GetComponent<UISelectable>(), objectId,
                      "UISelectable");
        noteComponent(object.GetComponent<UIScrollView>(), objectId,
                      "UIScrollView");
        noteComponent(object.GetComponent<UIAccessibility>(), objectId,
                      "UIAccessibility");
    }

    // ── Step 4d: 입력창의 라벨 소유권을 게시 전에 원자적으로 확정한다 ───────
    // 순서가 계약이다: 먼저 모든 주장을 안정된 Canvas DFS 순서로 모으고, 모든
    // 충돌을 찾은 다음에야 표를 만든다. 발견하는 대로 적용하면 두 입력창이
    // 같은 라벨을 요구할 때 먼저 온 쪽이 조용히 이기고, 그 승부는 형제 순서를
    // 바꾸기만 해도 뒤집힌다.
    std::vector<unsigned int> reachableObjectIds;
    reachableObjectIds.reserve(builder.nodes.size());
    for (const auto& node : builder.nodes) {
        reachableObjectIds.push_back(node.object->GetID());
    }

    std::vector<InputLabelClaim> inputClaims;
    for (const auto& node : builder.nodes) {
        GameObject& object = *node.object;
        UITextInput* input = EnabledComponent<UITextInput>(object);
        // 비활성/비활성화된 입력창은 아무 주장도 하지 않는다. 그래서 그 라벨은
        // 평범한 UILabel 가시성을 따른다.
        if (!input) continue;
        InputLabelClaim claim;
        claim.object = &object;
        claim.input = input;
        claim.renderedRefAuthored = input->RenderedLabel().IsSet();
        claim.placeholderRefAuthored = input->PlaceholderLabel().IsSet();
        claim.rendered = ResolveClaimedLabel(world, input->RenderedLabel(),
                                             reachableObjectIds);
        claim.placeholder = ResolveClaimedLabel(
            world, input->PlaceholderLabel(), reachableObjectIds);
        // 한 라벨을 두 역할로 쓰는 것은 충돌이다. 같은 글자 위에 본문과
        // placeholder가 동시에 그려지고, 어느 쪽이 이기는지는 정의되지 않는다.
        if (claim.rendered && claim.rendered == claim.placeholder) {
            claim.conflicted = true;
        }
        inputClaims.push_back(claim);
    }
    // 다중 소유자 검출. 역할과 무관하게 같은 라벨을 두 번 이상 요구하면 관련된
    // 입력창이 전부 실패한다 — 하나를 살려 두면 어느 쪽이 사는지가 순회 순서에
    // 달린다.
    for (std::size_t i = 0; i < inputClaims.size(); ++i) {
        for (std::size_t j = i + 1; j < inputClaims.size(); ++j) {
            const auto shares = [](UILabel* a, UILabel* b) {
                return a != nullptr && a == b;
            };
            if (shares(inputClaims[i].rendered, inputClaims[j].rendered) ||
                shares(inputClaims[i].rendered, inputClaims[j].placeholder) ||
                shares(inputClaims[i].placeholder, inputClaims[j].rendered) ||
                shares(inputClaims[i].placeholder,
                       inputClaims[j].placeholder)) {
                inputClaims[i].conflicted = true;
                inputClaims[j].conflicted = true;
            }
        }
    }
    // 충돌한 라벨도 평범한 출력에서 사라진다(fail-closed). 남겨 두면 소유권을
    // 잃은 라벨이 자기 저작 내용을 그대로 그려, 입력창이 비어 있는데 화면에는
    // 옛 글이 남는다.
    std::vector<unsigned int> suppressedLabelObjectIds;
    std::vector<std::string> ownershipFailures;
    for (const auto& claim : inputClaims) {
        const unsigned int objectId = claim.object->GetID();
        if (claim.conflicted) {
            if (claim.rendered) {
                suppressedLabelObjectIds.push_back(
                    claim.rendered->GetGameObject()->GetID());
            }
            if (claim.placeholder) {
                suppressedLabelObjectIds.push_back(
                    claim.placeholder->GetGameObject()->GetID());
            }
            ownershipFailures.push_back(
                "UITextInput on scene object " + std::to_string(objectId) +
                " shares a UILabel with another owner or role");
            continue;
        }
        if (!claim.rendered) {
            // 본문 역할이 무너져도 placeholder에 대한 소유 주장은 그대로다.
            // 여기서 놓아 주면(fail-open) 그 라벨이 자기 저작 문구를 평범한
            // UILabel로 영원히 그린다 — 참조 하나를 잘못 적은 저작자에게
            // 남는 것은 죽은 입력창 껍데기와 지워지지 않는 유령 문구다. 이
            // 표의 나머지 행은 전부 fail-closed이고 이 행만 예외일 이유가 없다.
            const bool suppressedPlaceholder = claim.placeholder != nullptr;
            if (suppressedPlaceholder) {
                suppressedLabelObjectIds.push_back(
                    claim.placeholder->GetGameObject()->GetID());
            }
            // 진단은 저작된 참조가 실제로 어긋났을 때만 낸다. 참조를 아예 적지
            // 않은 것은 오류가 아니라 미완성이고, 그것까지 보고하면 만드는
            // 중인 씬이 매 프레임 로그를 채운다. 억제 사실은 같은 진단 하나에
            // 함께 적는다 — 두 번째 진단을 추가하면 같은 오작성 하나가 두 줄이
            // 된다.
            if (claim.renderedRefAuthored) {
                ownershipFailures.push_back(
                    "UITextInput on scene object " + std::to_string(objectId) +
                    " references a rendered UILabel that is missing, of the "
                    "wrong type, out of this Canvas tree, or disabled" +
                    (suppressedPlaceholder
                         ? "; its placeholder UILabel is suppressed with it"
                         : ""));
            }
            continue;
        }
        suppressedLabelObjectIds.push_back(
            claim.rendered->GetGameObject()->GetID());
        if (claim.placeholder) {
            suppressedLabelObjectIds.push_back(
                claim.placeholder->GetGameObject()->GetID());
        } else if (claim.placeholderRefAuthored) {
            ownershipFailures.push_back(
                "UITextInput on scene object " + std::to_string(objectId) +
                " references a placeholder UILabel that is missing, of the "
                "wrong type, out of this Canvas tree, or disabled");
        }
    }
    for (const auto& message : ownershipFailures) {
        molga::text::TextDiagnostic diagnostic;
        diagnostic.code = molga::text::TextDiagnosticCode::ReferenceInvalid;
        diagnostic.severity = molga::text::TextSeverity::Error;
        diagnostic.subsystem = "ui.layout";
        diagnostic.message = message;
        diagnostic.remediation =
            "give each UITextInput its own enabled rendered and placeholder "
            "UILabel inside the same active Canvas tree";
        diagnostic.componentType = "UITextInput";
        sink.Report(std::move(diagnostic));
    }

    // ── Step 5b의 고유 크기 생산자 ──────────────────────────────────────────
    // Task 10.2는 이것을 UISystem::CollectRender에 두었고, 그 근거는 하나였다:
    // 그때 Build에는 TextLayoutService가 없었다(헤더가 계약을 그렇게 적어
    // 두었다). Step 3i가 서비스를 인자로 넣었으므로 그 근거는 사라졌다.
    // 스냅샷만으로 그리는 표면 — Task 11.2가 만드는 바로 그것 — 은
    // CollectRender를 돌지 않으므로, 생산자가 거기에만 있으면 모든 UILabel이
    // 다시 0으로 측정되고 fitter/그룹/SCC가 전부 0 위에서 돈다.
    //
    // 제약을 벗긴 요청으로 측정한다. 렌더 요청의 intrinsicSize를 그대로
    // 게시하면 fitter가 rect를 바꾸고 그 rect가 다음 프레임의 제약이 되어 값이
    // 영원히 흔들린다(Task 10.2가 비싸게 배운 규칙이다). 벗긴 측정은 글과
    // 스타일에만 의존하므로 첫 게시 뒤로 Publish가 거짓을 돌려준다.
    //
    // 이 자리가 기하 키보다 *앞*인 것도 계약이다. 뒤에 두면 이 프레임이 자기
    // 게시를 읽지 못해 다음 프레임이 반드시 미스가 되고, "변경 없는 프레임은
    // 같은 스냅샷"이 프레임마다 한 번씩 깨진다.
    {
        DiscardingPayloadSink discard;
        for (const auto& node : builder.nodes) {
            GameObject& object = *node.object;
            UILabel* label = EnabledComponent<UILabel>(object);
            if (!label || label->GetText().empty()) continue;
            const unsigned int labelObjectId = object.GetID();
            // 입력창이 소유한 라벨은 평범한 라벨이 아니다. 그 글의 권한은
            // 입력창의 유효 요청이고, 그 측정은 Task 14가 소유한다.
            if (std::find(suppressedLabelObjectIds.begin(),
                          suppressedLabelObjectIds.end(),
                          labelObjectId) != suppressedLabelObjectIds.end()) {
                continue;
            }
            const UIRuntimeTargetIdentity target = CaptureTarget(world, *label);
            if (!target) continue;
            molga::text::TextLayoutRequest measure =
                BuildLabelRequestFixed(*label, labelObjectId, molga::FixedRect{});
            measure.constraints = molga::text::LayoutConstraints{};
            const auto measured = textLayout.Layout(measure, discard);
            if (!measured || *measured == nullptr) continue;
            UIIntrinsicLayoutRegistry::Get().Publish(
                target, UILabelIntrinsicContentIdentity(measure),
                (*measured)->intrinsicSize);
        }
    }

    // 게시가 의미 세대를 움직였을 수 있다. 그 값을 여기서 다시 읽어야 이
    // 프레임이 자기 게시를 그대로 소비하고, 다음 프레임이 같은 도장으로 빠른
    // 경로에 든다 — 낡은 값을 도장에 남기면 매 프레임 한 번씩 헛도는 빌드가
    // 생긴다.
    clock = UIRuntimeInvalidationClock::Current();
    stamp.semanticDirtyGeneration = clock.semanticDirtyGeneration;
    clocksCacheable = clock.cacheable && impl.viewportGenerationCacheable;
    if (!clocksCacheable && uncacheableKey.empty()) uncacheableKey = "clock;";
    uncacheable = uncacheable || !clocksCacheable;

    // 고유 크기는 확정된 불변 배치를 가진 쪽이 게시한 것을 읽기만 한다.
    for (auto& node : builder.nodes) {
        GameObject& object = *node.object;
        std::optional<UIIntrinsicLayoutRecord> record;
        if (auto* label = EnabledComponent<UILabel>(object)) {
            record = UIIntrinsicLayoutRegistry::Get().Find(
                CaptureTarget(world, *label));
        }
        if (!record) {
            if (auto* input = EnabledComponent<UITextInput>(object)) {
                record = UIIntrinsicLayoutRegistry::Get().Find(
                    CaptureTarget(world, *input));
            }
        }
        if (!record) {
            if (auto* image = EnabledComponent<UIImage>(object)) {
                record = UIIntrinsicLayoutRegistry::Get().Find(
                    CaptureTarget(world, *image));
            }
        }
        if (record) {
            node.intrinsicWidth = record->intrinsicSize.width.Raw();
            node.intrinsicHeight = record->intrinsicSize.height.Raw();
            node.intrinsicGeneration = record->generation;
            node.intrinsicIdentity = record->contentIdentity;
        }
    }

    // ── Step 1i: 제공자 조회는 스칼라 빠른 경로가 빗나간 뒤에 한 번씩 ───────
    // 유효한 활성 입력창마다 정확히 한 번, (surfaceWindowId, inputIdentity)
    // 쌍으로 묻는다. 돌려받은 값은 복사되며, 제공자가 나중에 자기 저장소를
    // 고쳐도 게시된 스냅샷은 달라지지 않는다.
    std::vector<UITextInputVisualState> gatheredInputStates;
    std::vector<UITextInputGeometryCacheIdentity> gatheredInputGeometry;
    for (const auto& claim : inputClaims) {
        if (claim.conflicted || !claim.rendered) continue;
        const UIRuntimeTargetIdentity identity =
            CaptureTarget(world, *claim.input);
        if (!identity) continue;
        UITextInputVisualState state;
        state.surfaceWindowId = surfaceWindowId;
        state.input = identity;
        // 제공자가 아직 이 입력창을 모르면 저작된 초기값이 보이는 글이다.
        state.committedUtf8 = claim.input->InitialText();
        if (const auto published =
                inputVisualStates.GetVisualState(surfaceWindowId, identity)) {
            state = *published;
        }
        UITextInputGeometryCacheIdentity geometryIdentity;
        geometryIdentity.input = identity;
        geometryIdentity.effectiveRequest.utf8 =
            state.committedUtf8 + state.compositionUtf8;
        geometryIdentity.effectiveRequest.style =
            EffectiveInputParagraphStyle(*claim.input);
        // 얼어붙은 뷰포트 제약. 확정된 라벨 사각형이 아니라 논리 뷰포트를 쓰는
        // 이유는 순환 때문이다: 기하 키는 배치 앞에 있고 라벨 사각형은 배치의
        // 결과다. 라벨 사각형의 변화는 rectAndLayoutRevisions가 이미 덮는다.
        geometryIdentity.effectiveRequest.constraints.width =
            logicalViewport.width;
        geometryIdentity.effectiveRequest.constraints.height =
            logicalViewport.height;
        geometryIdentity.effectiveRequest.diagnosticContext.componentType =
            "UILabel";
        geometryIdentity.effectiveRequest.diagnosticContext.sceneObjectId =
            claim.rendered->GetGameObject()->GetID();
        gatheredInputStates.push_back(std::move(state));
        gatheredInputGeometry.push_back(std::move(geometryIdentity));
    }

    if (uncacheable) {
        // Step 8b: 소진된 revision은 캐시 정체성이 상태를 구분하지 못한다는
        // 뜻이다. 조회도 삽입도 하지 않고 매번 새로 만든다. 진단은 같은 집합이
        // 계속 소진되어 있는 동안 한 번만 낸다 — 프레임마다 내면 로그가 그
        // 하나로 가득 찬다.
        if (impl.lastUncacheableKey != uncacheableKey) {
            impl.lastUncacheableKey = uncacheableKey;
            if (clocksCacheable) {
                impl.ReportInvalid(
                    sink,
                    "a reachable UI component exhausted its authored revision; "
                    "this surface rebuilds without caching",
                    "restart the editor session to reset runtime revisions");
            } else {
                // 집계 세대가 소진되면 옛 세대가 새 상태를 가리키지 않도록
                // 조회도 삽입도 하지 않는다. 이것은 성능 문제가 아니라
                // 차단 사유다.
                molga::text::TextDiagnostic diagnostic;
                diagnostic.code =
                    molga::text::TextDiagnosticCode::LayoutInvalid;
                diagnostic.severity = molga::text::TextSeverity::Blocker;
                diagnostic.subsystem = "ui.layout";
                diagnostic.message =
                    "a UI aggregate generation is exhausted; this surface "
                    "rebuilds with both snapshot caches bypassed and publishes "
                    "no new runtime binding";
                diagnostic.remediation =
                    "restart the process to reset the runtime generation "
                    "clocks";
                sink.Report(std::move(diagnostic));
            }
        }
        impl.hasStamp = false;
        impl.lastSnapshot.reset();
    }

    // ── Step 4c: 기하 키 ────────────────────────────────────────────────────
    ++impl.keyBuilds;
    ScratchResize(impl.canvasScratch, canvasRoots.size(), impl.keyAllocations);
    for (std::size_t i = 0; i < canvasRoots.size(); ++i) {
        const LayoutNode& root = builder.nodes[canvasRoots[i]];
        impl.canvasScratch[i] =
            root.canvas ? root.canvas->AuthoredRevision() : 0;
    }
    ScratchResize(impl.hierarchyScratch, builder.nodes.size(),
                  impl.keyAllocations);
    ScratchResize(impl.rectScratch, builder.nodes.size() * 6,
                  impl.keyAllocations);
    ScratchResize(impl.intrinsicScratch, builder.nodes.size(),
                  impl.keyAllocations);
    for (std::size_t i = 0; i < builder.nodes.size(); ++i) {
        const LayoutNode& node = builder.nodes[i];
        impl.hierarchyScratch[i] =
            (static_cast<std::uint64_t>(node.object->GetID()) << 32) |
            static_cast<std::uint32_t>(node.object->GetSiblingIndex());
        impl.rectScratch[i * 6 + 0] = node.rect->AuthoredRevision();
        impl.rectScratch[i * 6 + 1] =
            node.element ? node.element->AuthoredRevision() : 0;
        impl.rectScratch[i * 6 + 2] =
            node.group ? node.group->AuthoredRevision() : 0;
        impl.rectScratch[i * 6 + 3] =
            node.fitter ? node.fitter->AuthoredRevision() : 0;
        impl.rectScratch[i * 6 + 4] =
            node.mask ? node.mask->AuthoredRevision() : 0;
        // 라벨이 기하에 닿는 통로는 정확히 둘이다. 하나는 고유 크기이고 그것은
        // 이미 intrinsicGenerations가 덮는다. 다른 하나가 줄바꿈 모드다 —
        // HasSizeDependentIntrinsicWidth가 그것을 읽어 SCC 자기 간선을 세우므로,
        // 줄바꿈을 껐다 켜면 같은 키로 다른 기하가 나온다.
        //
        // revision 전체를 넣으면 안 된다. 색이나 정렬 같은 페이로드 편집까지
        // 기하를 무효화해 "페이로드만 바뀐 편집은 기하를 재사용한다"는 계약이
        // 깨진다. 그래서 모드 값 자체를 넣는다(라벨이 없으면 0).
        const auto* label = node.object->GetComponent<UILabel>();
        impl.rectScratch[i * 6 + 5] =
            label ? static_cast<std::uint64_t>(label->GetWrapMode()) + 1U : 0U;
        impl.intrinsicScratch[i] = node.intrinsicGeneration;
    }

    // ── Step 6b: 이 표면의 사각형을 실제로 움직이는 스크롤 변위만 ───────────
    // 목록은 이미 완전한 식별자 순서로 정렬되어 도착한다(UIScrollSystem이
    // 정렬한다). 여기서 다시 정렬하면 규칙이 두 벌이 되고, 두 벌은 언젠가
    // 갈린다. content 서브트리가 이 트리에 없는 변위는 아무 사각형도 옮기지
    // 않으므로 키에서도 뺀다 — 넣으면 다른 Canvas의 스크롤이 이 표면의 기하를
    // 무효화한다.
    //
    // 대상은 **완전한 런타임 식별자**로만 고른다. objectId 하나만 맞춰 보면
    // 두 가지가 조용히 깨진다: 참조가 끊긴 스크롤 뷰가 남기는 빈 식별자
    // (objectId 0)가 씬이 그대로 발급할 수 있는 id 0 오브젝트와 맞아 무관한
    // 서브트리를 살아 있는 오프셋만큼 밀어내고, 같은 오브젝트에서 교체된
    // RectTransform이 죽은 컴포넌트의 변위를 물려받는다.
    const auto namesNode = [&world](const LayoutNode& node,
                                    const UIRuntimeTargetIdentity& content) {
        // 숫자 id 비교는 값싼 선행 필터일 뿐이다. 통과 여부는 네 필드가 정한다.
        return node.object->GetID() == content.objectId &&
               CaptureTarget(world, *node.rect) == content;
    };
    std::vector<UIScrollDisplacementCacheIdentity> scrollDisplacements;
    for (auto& displacement :
         UIScrollSystem::Get().DisplacementsForWorld(world.Generation())) {
        // 빈 식별자는 아무것도 이름하지 않는다. 어떤 사각형도 움직여서는 안
        // 되고, 기하 키에도 들어가서는 안 된다.
        if (!displacement.content) continue;
        const bool present =
            std::any_of(builder.nodes.begin(), builder.nodes.end(),
                        [&](const LayoutNode& node) {
                            return namesNode(node, displacement.content);
                        });
        if (!present) continue;
        scrollDisplacements.push_back(std::move(displacement));
    }

    UILayoutGeometryCacheKey geometryKey;
    geometryKey.worldGeneration = world.Generation();
    geometryKey.viewport = logicalViewport;
    geometryKey.viewportGeneration = viewportGeneration;
    geometryKey.canvasScaleRevisions = impl.canvasScratch;
    geometryKey.hierarchyAndSiblingRevisions = impl.hierarchyScratch;
    geometryKey.rectAndLayoutRevisions = impl.rectScratch;
    geometryKey.intrinsicGenerations = impl.intrinsicScratch;
    geometryKey.inputGeometry = gatheredInputGeometry;
    geometryKey.scrollDisplacements = scrollDisplacements;
    const std::size_t geometryHash = HashUILayoutGeometryCacheKey(geometryKey);

    std::vector<UIDrawOrderKey> orderedKeys;
    std::vector<UILayoutNodeSnapshot> orderedNodes;
    auto& lru = impl.geometry[geometryKey.worldGeneration];
    bool geometryHit = false;
    if (!uncacheable) {
        // 조회 규칙은 Impl::MatchGeometry 한 벌뿐이다. 여기서 다시 쓰면
        // GeometryCacheContains와 갈릴 수 있고, 그 어긋남은 관찰되지 않는다.
        const auto it = Impl::MatchGeometry(lru, geometryHash, geometryKey);
        if (it != lru.end()) {
            lru.splice(lru.begin(), lru, it);
            orderedKeys = lru.front().order;
            orderedNodes = lru.front().nodes;
            const auto& cachedDropped = lru.front().dropped;
            if (cachedDropped.size() == builder.nodes.size()) {
                for (std::size_t n = 0; n < builder.nodes.size(); ++n) {
                    builder.nodes[n].dropped = cachedDropped[n] != 0;
                }
                geometryHit = true;
            }
            // 크기가 어긋나면 이 항목은 지금 트리를 말하지 못한다. 조용히
            // 절반만 복원하느니 다시 짓는다.
        }
    }

    if (!geometryHit) {
        ++impl.geometryBuilds;
        ++impl.layoutRevision;
        for (std::size_t i = builder.nodes.size(); i-- > 0;) {
            MeasureNode(builder, i);
        }
        if (builder.failed) return nullptr;
        MarkLayoutCycles(builder);
        for (std::size_t i = 0; i < canvasRoots.size(); ++i) {
            builder.nodes[canvasRoots[i]].resolved = canvasRects[i];
            ArrangeSubtree(builder, canvasRoots[i]);
        }
        if (builder.failed) return nullptr;
        // 확정이 끝난 뒤에 정확히 한 번. 확정 *전에* 캔버스 뿌리를 뷰포트
        // 크기로 두면 저작된 sizeDelta(절대 픽셀)가 배율을 받지 못해 배율이
        // 앵커에만 걸린다.
        for (std::size_t i = 0; i < canvasRoots.size(); ++i) {
            const auto& scale = canvasScales[i];
            if (scale[0] == scale[1] && scale[2] == scale[3]) continue;
            ScaleSubtree(builder, canvasRoots[i], scale[0], scale[1], scale[2],
                         scale[3]);
        }
        if (builder.failed) return nullptr;
        // ── Step 6a: 배율 뒤, 클립 앞 ───────────────────────────────────────
        // 배율 뒤인 것이 계약이다. 스크롤 시스템이 읽는 오프셋은 게시된
        // 스냅샷의 표면 논리 단위이므로(그 스냅샷이 배율을 이미 받았다),
        // 배율 앞에서 더하면 같은 수가 두 공간을 오간다.
        //
        // 클립 앞인 것도 계약이다. 뷰포트 마스크는 제자리에 남고 내용만
        // 움직여야 잘려 나가야 할 부분이 실제로 잘린다 — 뒤에 두면 내용과
        // 함께 클립도 움직여 스크롤해도 아무것도 잘리지 않는다.
        for (const auto& displacement : scrollDisplacements) {
            if (displacement.offsetXRaw == 0 && displacement.offsetYRaw == 0) {
                continue;
            }
            for (std::size_t i = 0; i < builder.nodes.size(); ++i) {
                // 위의 존재 필터와 **같은 술어 하나**를 쓴다. 두 벌이면 키에
                // 들어간 변위와 실제로 옮겨진 서브트리가 갈릴 수 있고, 그
                // 어긋남은 캐시가 잘못된 기하를 정당한 항목으로 저장하는
                // 방식으로만 드러난다.
                if (!namesNode(builder.nodes[i], displacement.content)) {
                    continue;
                }
                TranslateSubtree(builder, i, displacement.offsetXRaw,
                                 displacement.offsetYRaw);
                break;
            }
        }
        if (builder.failed) return nullptr;
        for (const auto root : canvasRoots) {
            ComputeClips(builder, root, std::nullopt, false);
        }
        if (builder.failed) return nullptr;

        std::vector<std::pair<UIDrawOrderKey, UILayoutNodeSnapshot>> emitted;
        emitted.reserve(builder.nodes.size());
        for (const auto& node : builder.nodes) {
            if (node.dropped) continue;
            UILayoutNodeSnapshot record;
            record.rectTransform = CaptureTarget(world, *node.rect);
            // 게시되는 값은 전부 검증된 26.6이다. 여기서 bare
            // static_cast<int32_t>로 좁히면 범위를 벗어난 사각형이 잘린 채로
            // 정상처럼 나온다.
            const auto rectFixed = ToFixedRect(node.resolved);
            if (!rectFixed || !InRawRange(node.prefWidth) ||
                !InRawRange(node.prefHeight)) {
                builder.Fail("UI layout value left the 26.6 range");
                return nullptr;
            }
            record.logicalRect = *rectFixed;
            record.intrinsicSize =
                FixedSize{ToFixed(node.prefWidth), ToFixed(node.prefHeight)};
            record.logicalClip = node.logicalClip;
            record.layoutRevision = impl.layoutRevision;
            emitted.emplace_back(node.drawOrder, std::move(record));
        }
        // Step 8a: 안정된 draw order로 한 번만 정렬한다. 살아 있는 포인터는
        // 하나도 남기지 않는다.
        std::sort(emitted.begin(), emitted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        orderedKeys.reserve(emitted.size());
        orderedNodes.reserve(emitted.size());
        for (auto& entry : emitted) {
            orderedKeys.push_back(std::move(entry.first));
            orderedNodes.push_back(std::move(entry.second));
        }

        if (!uncacheable) {
            // Step 4g: 용량에 도달하면 정확히 그 월드의 LRU 하나를 버린다.
            if (lru.size() >= kUILayoutGeometryEntriesPerWorld) lru.pop_back();
            Impl::GeometryEntry entry;
            entry.hash = geometryHash;
            entry.key = geometryKey;
            entry.order = orderedKeys;
            entry.nodes = orderedNodes;
            entry.dropped.reserve(builder.nodes.size());
            for (const auto& node : builder.nodes) {
                entry.dropped.push_back(node.dropped ? 1U : 0U);
            }
            lru.push_front(std::move(entry));
        }
    }
    impl.lastGeometryKey = geometryKey;

    // 상호작용 자격은 기하가 아니다. 캐시된 기하를 재사용해도 이 플래그는
    // 살아 있는 저작 상태에서 매번 다시 읽는다 — 그러지 않으면 선택 불가로
    // 바꾼 버튼이 캐시가 살아 있는 동안 계속 눌린다.
    for (std::size_t i = 0; i < orderedNodes.size(); ++i) {
        GameObject* object = world.FindById(orderedNodes[i].rectTransform.objectId);
        orderedNodes[i].interactionEligible =
            object && ObjectIsInteractionEligible(*object);
    }

    // ── Step 4d/4e: 시각/상호작용 정체성 ────────────────────────────────────
    impl.visualScratch.clear();
    impl.interactionScratch.clear();
    auto pushVisual = [&](const UIComponent& component, std::uint32_t schema,
                          unsigned int objectId, const std::string& intrinsicId,
                          const std::string& textureGuid) {
        impl.visualScratch.emplace_back();
        UIVisualCacheIdentity& identity = impl.visualScratch.back();
        identity.sceneObjectId = objectId;
        ScratchAssign(identity.componentTypeName, component.GetTypeName(),
                      impl.keyAllocations);
        identity.componentSchemaVersion = schema;
        identity.authoredRevision = component.AuthoredRevision();
        ScratchAssign(identity.canonicalAuthoredPayload,
                      CanonicalPayload(component), impl.keyAllocations);
        ScratchAssign(identity.immutableTextLayoutIdentity, intrinsicId,
                      impl.keyAllocations);
        ScratchAssign(identity.textureGuid, textureGuid, impl.keyAllocations);
        if (!textureGuid.empty()) {
            if (const auto content =
                    UITextureContentRegistry::Get().Find(textureGuid)) {
                ScratchAssign(identity.textureContentSha256,
                              content->contentSha256, impl.keyAllocations);
                identity.textureContentStableId = content->contentStableId;
            }
        }
    };

    const auto visualEntriesBefore = impl.visualScratch.size();
    for (const auto& node : builder.nodes) {
        if (node.dropped) continue;
        GameObject& object = *node.object;
        const unsigned int objectId = object.GetID();
        if (auto* image = EnabledComponent<UIImage>(object)) {
            pushVisual(*image, UIImage::CurrentSchemaVersion, objectId,
                       node.intrinsicIdentity, image->GetTextureGuid());
        }
        if (auto* label = EnabledComponent<UILabel>(object)) {
            pushVisual(*label, UILabel::CurrentSchemaVersion, objectId,
                       node.intrinsicIdentity, std::string());
        }
        if (auto* button = EnabledComponent<UIButton>(object)) {
            pushVisual(*button, UIButton::CurrentSchemaVersion, objectId,
                       std::string(), std::string());
        }
        if (auto* input = EnabledComponent<UITextInput>(object)) {
            pushVisual(*input, UITextInput::CurrentSchemaVersion, objectId,
                       node.intrinsicIdentity, std::string());
        }
        if (auto* accessibility = EnabledComponent<UIAccessibility>(object)) {
            pushVisual(*accessibility, UIAccessibility::CurrentSchemaVersion,
                       objectId, std::string(), std::string());
        }

        if (auto* selectable = EnabledComponent<UISelectable>(object)) {
            impl.interactionScratch.emplace_back();
            UIInteractionCacheIdentity& identity = impl.interactionScratch.back();
            identity.sceneObjectId = objectId;
            ScratchAssign(identity.componentTypeName, selectable->GetTypeName(),
                          impl.keyAllocations);
            identity.componentSchemaVersion = UISelectable::CurrentSchemaVersion;
            identity.authoredRevision = selectable->AuthoredRevision();
            identity.active = object.IsActive();
            identity.interactable = selectable->Interactable();
            identity.focusable = selectable->Interactable();
            identity.navigationMode = selectable->NavigationMode();
            identity.explicitNavigation = {
                selectable->NavigateUp(), selectable->NavigateDown(),
                selectable->NavigateLeft(), selectable->NavigateRight()};
        }
        if (auto* button = EnabledComponent<UIButton>(object)) {
            impl.interactionScratch.emplace_back();
            UIInteractionCacheIdentity& identity = impl.interactionScratch.back();
            identity.sceneObjectId = objectId;
            ScratchAssign(identity.componentTypeName, button->GetTypeName(),
                          impl.keyAllocations);
            identity.componentSchemaVersion = UIButton::CurrentSchemaVersion;
            identity.authoredRevision = button->AuthoredRevision();
            identity.active = object.IsActive();
            identity.interactable = button->IsInteractable();
            identity.focusable = button->IsInteractable();
        }
        if (auto* input = EnabledComponent<UITextInput>(object)) {
            impl.interactionScratch.emplace_back();
            UIInteractionCacheIdentity& identity = impl.interactionScratch.back();
            identity.sceneObjectId = objectId;
            ScratchAssign(identity.componentTypeName, input->GetTypeName(),
                          impl.keyAllocations);
            identity.componentSchemaVersion = UITextInput::CurrentSchemaVersion;
            identity.authoredRevision = input->AuthoredRevision();
            identity.active = object.IsActive();
            identity.acceptsTextInput = !input->ReadOnly();
        }
        if (node.mask) {
            impl.interactionScratch.emplace_back();
            UIInteractionCacheIdentity& identity = impl.interactionScratch.back();
            identity.sceneObjectId = objectId;
            ScratchAssign(identity.componentTypeName, node.mask->GetTypeName(),
                          impl.keyAllocations);
            identity.componentSchemaVersion = UIMask::CurrentSchemaVersion;
            identity.authoredRevision = node.mask->AuthoredRevision();
            identity.active = object.IsActive();
            identity.maskEnabled = node.mask->ClipsDescendants();
        }
    }
    impl.lastVisualKeyEntryCount =
        impl.visualScratch.size() - visualEntriesBefore;

    // ── Step 3b/5d: 런타임 바인딩은 전체 키에서만 충돌 비교된다 ─────────────
    // 넓은 textureBindingGeneration 스칼라는 할당 없는 빠른 경로만 끈다. 같은
    // 내용을 다시 올렸을 때 기하는 재사용하면서 낡은 GPU 바인딩은 절대
    // 재사용하지 않게 하는 구분이 이 벡터에 있다.
    std::vector<UIRuntimeBindingCacheIdentity> runtimeBindings;
    for (const auto& node : builder.nodes) {
        if (node.dropped) continue;
        GameObject& object = *node.object;
        auto* image = EnabledComponent<UIImage>(object);
        if (!image || image->GetTextureGuid().empty()) continue;
        const auto bound =
            UITextureBindingRegistry::Get().Find(image->GetTextureGuid());
        if (!bound) continue;
        UIRuntimeBindingCacheIdentity identity;
        identity.sceneObjectId = object.GetID();
        identity.componentTypeName = image->GetTypeName();
        identity.componentSchemaVersion = UIImage::CurrentSchemaVersion;
        identity.binding = bound->binding;
        runtimeBindings.push_back(std::move(identity));
    }


    UISnapshotCacheKey completeKey;
    completeKey.surfaceWindowId = surfaceWindowId;
    completeKey.geometry = geometryKey;
    completeKey.semanticDirtyGeneration = clock.semanticDirtyGeneration;
    completeKey.scrollDisplacementGeneration = clock.scrollDisplacementGeneration;
    completeKey.textureBindingGeneration = clock.textureBindingGeneration;
    completeKey.deviceGeneration = clock.deviceGeneration;
    completeKey.visualContent = impl.visualScratch;
    completeKey.interaction = impl.interactionScratch;
    completeKey.runtimeBindings = runtimeBindings;
    completeKey.inputVisualStates = gatheredInputStates;
    // 관찰 seam. 슬롯으로 옮겨지기 전에 한 벌 남긴다 — 옮긴 뒤에 읽으면
    // 미스로 새로 만든 키와 적중으로 재사용된 키를 구분할 수 없다.
    impl.lastSnapshotKey = completeKey;

    UISnapshotWorldDeviceSlotKey slotKey;
    slotKey.worldGeneration = world.Generation();
    slotKey.deviceGeneration = clock.deviceGeneration;

    if (!uncacheable) {
        for (auto& slot : impl.fullSlots) {
            if (!(slot.slot == slotKey)) continue;
            if (slot.key == completeKey && slot.snapshot) {
                impl.lastStamp = stamp;
                impl.hasStamp = true;
                impl.lastSnapshot = slot.snapshot;
                return slot.snapshot;
            }
            break;
        }
    }

    // ── Step 4c/4e/5d/5e/5f: 구체 렌더/hit 페이로드 ─────────────────────────
    // 여기서 나오는 값은 전부 자기 완결적이다. 소비자가 이 레코드를 그리거나
    // 라우팅하기 위해 살아 있는 컴포넌트를 다시 들여다볼 일이 없어야 한다 —
    // 그 순간 게시된 스냅샷과 화면이 갈린다.
    std::vector<UIRenderItemSnapshot> renderItems;
    std::vector<UIHitTargetSnapshot> hitTargets;
    std::vector<UITextInputLabelSnapshot> textInputLabels;
    std::vector<UITextInputImeGeometrySnapshot> textInputImeGeometry;
    std::uint64_t nextSubmission = 0;
    bool submissionOverflowed = false;

    const auto isSuppressedLabelObject = [&suppressedLabelObjectIds](
                                             unsigned int objectId) {
        return std::find(suppressedLabelObjectIds.begin(),
                         suppressedLabelObjectIds.end(),
                         objectId) != suppressedLabelObjectIds.end();
    };

    for (std::size_t i = 0; i < orderedNodes.size(); ++i) {
        const UILayoutNodeSnapshot& node = orderedNodes[i];
        const UIDrawOrderKey& nodeKey = orderedKeys[i];
        GameObject* object = world.FindById(node.rectTransform.objectId);
        if (!object) continue;
        const unsigned int objectId = object->GetID();
        const molga::FixedRect rect = node.logicalRect;
        const std::optional<molga::FixedRect> clip = node.logicalClip;
        const std::uint64_t groupBaseSubmission = nextSubmission;

        auto makeOrder = [&nodeKey](std::int32_t componentSortingOrder) {
            UIDrawOrderKey order;
            order.canvasSortingOrder = nodeKey.canvasSortingOrder;
            order.siblingPath = nodeKey.siblingPath;
            order.componentSortingOrder = componentSortingOrder;
            return order;
        };

        // 이 오브젝트가 낼 레코드를 먼저 모은다. 정렬 키의 처음 두 항이 이
        // 오브젝트 안에서 전부 같으므로, 여기서 componentSortingOrder로
        // 안정 정렬한 뒤 순서대로 번호를 매기면 전체 벡터가 이미 정렬되어
        // 있다(형제 경로가 컴포넌트 정렬 순서보다 앞선다).
        std::vector<UIRenderItemSnapshot> local;
        const Component* actionComponent = nullptr;
        std::int32_t actionSortingOrder = 0;
        bool actionEmitsRender = false;

        if (auto* image = EnabledComponent<UIImage>(*object)) {
            UISpriteSnapshot sprite;
            sprite.tint = image->GetTint();
            sprite.textureGuid = image->GetTextureGuid();
            if (!sprite.textureGuid.empty()) {
                const auto content =
                    UITextureContentRegistry::Get().Find(sprite.textureGuid);
                const auto bound =
                    UITextureBindingRegistry::Get().Find(sprite.textureGuid);
                // 해석은 전부 성립하거나 전부 실패한다. 절반만 채운 변형은
                // 소비자가 "GUID는 있는데 SHA가 없다"를 각자 다르게 해석하게
                // 만든다.
                if (content && bound && bound->lifetime &&
                    bound->lifetime->Identity() == bound->binding) {
                    sprite.textureContentSha256 = content->contentSha256;
                    sprite.textureContentStableId = content->contentStableId;
                    sprite.binding = bound->binding;
                    sprite.resourceLifetime = bound->lifetime;
                } else if (impl.NotePayloadFact(
                               "texture:" + std::to_string(world.Generation()) +
                               ":" + sprite.textureGuid + ":" +
                               std::to_string(objectId))) {
                    molga::text::TextDiagnostic diagnostic;
                    diagnostic.code =
                        molga::text::TextDiagnosticCode::ReferenceInvalid;
                    diagnostic.severity = molga::text::TextSeverity::Warning;
                    diagnostic.subsystem = "ui.layout";
                    diagnostic.message =
                        "UIImage texture '" + sprite.textureGuid +
                        "' has no validated content identity or runtime "
                        "binding; the approved missing-texture payload is "
                        "published instead";
                    diagnostic.remediation =
                        "import the texture and let it publish its content "
                        "SHA and runtime binding before rendering";
                    diagnostic.assetGuid = sprite.textureGuid;
                    diagnostic.sceneObjectId = objectId;
                    diagnostic.componentType = "UIImage";
                    sink.Report(std::move(diagnostic));
                    sprite.textureGuid.clear();
                } else {
                    sprite.textureGuid.clear();
                }
            }
            UIRenderItemSnapshot item;
            item.source = CaptureTarget(world, *image);
            item.canonicalSource =
                MakeStableKey(objectId, *image, UIImage::CurrentSchemaVersion);
            item.order = makeOrder(image->GetSortingOrder());
            item.logicalRect = rect;
            item.logicalClip = clip;
            item.reservedCommandSpan = 1;
            item.payload = std::move(sprite);
            local.push_back(std::move(item));
        }
        if (auto* button = EnabledComponent<UIButton>(*object)) {
            UISolidRectSnapshot solid;
            solid.color = button->CurrentColor();
            UIRenderItemSnapshot item;
            item.source = CaptureTarget(world, *button);
            item.canonicalSource = MakeStableKey(objectId, *button,
                                                 UIButton::CurrentSchemaVersion);
            item.order = makeOrder(button->GetSortingOrder());
            item.logicalRect = rect;
            item.logicalClip = clip;
            item.reservedCommandSpan = 1;
            item.payload = std::move(solid);
            local.push_back(std::move(item));
        }
        UILabel* label = EnabledComponent<UILabel>(*object);
        if (label && !isSuppressedLabelObject(objectId) &&
            !label->GetText().empty()) {
            const molga::text::TextLayoutRequest request =
                BuildLabelRequestFixed(*label, objectId, rect);
            // Step 5e: 확정된 불변 배치 하나를 그대로 든다. 없는 glyph는 그
            // 유효한 배치 안의 절차적 tofu로 남는다 — 실패가 아니다.
            RateLimitedPayloadSink labelSink(sink, impl.reportedPayloadFacts,
                                             world.Generation(), objectId);
            const auto layout = textLayout.Layout(request, labelSink);
            if (layout && *layout) {
                UITextSnapshot text;
                text.layout = *layout;
                text.origin = molga::FixedPoint{rect.x, rect.y};
                text.color = label->GetColor();
                UIRenderItemSnapshot item;
                item.source = CaptureTarget(world, *label);
                item.canonicalSource = MakeStableKey(
                    objectId, *label, UILabel::CurrentSchemaVersion);
                item.order = makeOrder(label->GetSortingOrder());
                item.logicalRect = rect;
                item.logicalClip = clip;
                item.reservedCommandSpan =
                    molga::text::TextRenderCommandSpan(**layout);
                item.payload = std::move(text);
                local.push_back(std::move(item));
            }
        }

        // ── Step 3h: 동작 대상 우선순위 ─────────────────────────────────────
        // 앞의 셋은 원래의 시각 동작 경로를 그대로 지키고, 뒤의 둘이 장식 없는
        // selectable/input 껍데기를 상호작용 가능하게 만든다.
        UISelectable* selectable = EnabledComponent<UISelectable>(*object);
        UITextInput* input = EnabledComponent<UITextInput>(*object);
        UIButton* button = EnabledComponent<UIButton>(*object);
        UIImage* image = EnabledComponent<UIImage>(*object);
        std::uint32_t actionSchema = 0;
        if (button && button->IsInteractable()) {
            actionComponent = button;
            actionSortingOrder = button->GetSortingOrder();
            actionSchema = UIButton::CurrentSchemaVersion;
            actionEmitsRender = true;
        } else if (image) {
            actionComponent = image;
            actionSortingOrder = image->GetSortingOrder();
            actionSchema = UIImage::CurrentSchemaVersion;
            actionEmitsRender = true;
        } else if (label && !isSuppressedLabelObject(objectId)) {
            // 입력창이 소유한 라벨은 동작 대상이 아니다. 남겨 두면 그 라벨이
            // 입력창보다 위에 있으므로 텍스트를 누를 때 hit-test가 입력창이
            // 아니라 라벨을 고르고, 포커스와 IME가 그 자리에서 사라진다.
            actionComponent = label;
            actionSortingOrder = label->GetSortingOrder();
            actionSchema = UILabel::CurrentSchemaVersion;
            actionEmitsRender = !label->GetText().empty();
        } else if (selectable && selectable->Interactable()) {
            actionComponent = selectable;
            actionSortingOrder = 0;
            actionSchema = UISelectable::CurrentSchemaVersion;
            actionEmitsRender = false;
        } else if (input && !input->ReadOnly()) {
            actionComponent = input;
            actionSortingOrder = 0;
            actionSchema = UITextInput::CurrentSchemaVersion;
            actionEmitsRender = false;
        }

        // Step 4e: 앞선 레코드의 예약 구간을 검증된 덧셈으로 더한 뒤에만 다음
        // 번호가 배정된다. 넘치면 이 소스 묶음을 통째로 빼고 진단을 낸다 —
        // 일부만 정렬된 묶음은 화면에서 순서가 섞인 채로 나온다.
        std::stable_sort(local.begin(), local.end(),
                         [](const UIRenderItemSnapshot& a,
                            const UIRenderItemSnapshot& b) {
                             return a.order.componentSortingOrder <
                                    b.order.componentSortingOrder;
                         });
        std::uint64_t cursor = nextSubmission;
        bool groupOverflowed = false;
        for (auto& item : local) {
            item.order.stableSubmissionIndex = cursor;
            // ── 인계받은 결함 4 (Task 11.1 -> 11.2) ─────────────────────────
            // 예약 구간이 0인 레코드는 자기 번호를 소비하지 않았고, 그래서
            // **다음 레코드가 같은 번호를 받았다**. 두 다른 레코드가 같은
            // 서수를 주장하면 UIDrawOrderKey는 그 둘을 구별하지 못하고,
            // 화면과 hit-test의 순서가 그 자리에서 갈릴 수 있다. 위치 기록이
            // 하나도 없는 텍스트(전부 기본 무시 문자인 문자열이 그렇다)가
            // 정확히 그 모양이다.
            //
            // reservedCommandSpan 자체는 진짜 기록 수로 남는다 — 수집기가
            // 그 값을 배치의 기록 수와 대조하기 때문이다. 바뀌는 것은
            // 커서가 나아가는 양뿐이다.
            const std::uint64_t span =
                std::max<std::uint64_t>(item.reservedCommandSpan, 1U);
            if (span > std::numeric_limits<std::uint64_t>::max() - cursor) {
                groupOverflowed = true;
                break;
            }
            cursor += span;
        }

        // ── 입력창 시각 묶음의 예약 구간 ────────────────────────────────────
        // 이 오브젝트의 평범한 레코드가 번호를 받은 다음, 입력창의 텍스트
        // 단계가 자기 위치 기록 전부를 예약한 뒤에야 다음 항목이 번호를 받는다
        // (Global Constraint의 두 반쪽 중 나머지 하나).
        //
        // 예약하지 않으면 baseOrder가 이 오브젝트의 배경 스프라이트 키와
        // 네 필드 전부 같아진다 — 같은 캔버스 순서, 같은 형제 경로, 같은
        // componentSortingOrder 0, 같은 stableSubmissionIndex. UIDrawOrderKey는
        // 그 둘을 구분하지 못하고, Task 14의 선택 사각형/글자/캐럿은 자기가
        // 올라앉아야 할 배경과 같은 자리에서 시작한다.
        std::uint64_t inputGroupBase = cursor;
        std::uint64_t inputGroupSpan = 0;
        // 이 오브젝트가 실제로 입력창 시각 묶음을 낼 때만 그 한 자리를
        // 예약한다. 입력창이 없는 오브젝트까지 한 칸씩 밀면 번호가 비어
        // 있는 자리로 가득 찬다.
        bool inputPresent = false;
        std::optional<molga::text::TextLayoutRequest> effectiveInputRequest;
        const UITextInputVisualState* inputState = nullptr;
        if (input && !groupOverflowed) {
            for (const auto& claim : inputClaims) {
                if (claim.input != input) continue;
                if (claim.conflicted || !claim.rendered) break;
                const UIRuntimeTargetIdentity inputIdentity =
                    CaptureTarget(world, *input);
                if (!inputIdentity) break;
                for (const auto& gathered : gatheredInputStates) {
                    if (gathered.input == inputIdentity) {
                        inputState = &gathered;
                        break;
                    }
                }
                effectiveInputRequest = BuildEffectiveInputRequest(
                    *input,
                    inputState ? inputState->committedUtf8 +
                                     inputState->compositionUtf8
                               : input->InitialText(),
                    logicalViewport, claim.rendered->GetGameObject()->GetID());
                inputPresent = true;
                if (!effectiveInputRequest->utf8.empty()) {
                    DiscardingPayloadSink discard;
                    const auto inputLayout =
                        textLayout.Layout(*effectiveInputRequest, discard);
                    if (inputLayout && *inputLayout) {
                        inputGroupSpan =
                            molga::text::TextRenderCommandSpan(**inputLayout);
                    }
                }
                break;
            }
        }
        if (!groupOverflowed) {
            // 결함 4의 나머지 절반. 비어 있는 입력창의 텍스트 단계는 예약
            // 구간이 0이므로, 그대로 두면 baseOrder가 다음 오브젝트의 첫
            // 레코드와 같은 번호를 갖는다. baseOrder는 언제나 자기 한 자리를
            // 소유한다.
            const std::uint64_t inputGroupAdvance =
                inputPresent ? std::max<std::uint64_t>(inputGroupSpan, 1U)
                             : inputGroupSpan;
            if (inputGroupAdvance >
                std::numeric_limits<std::uint64_t>::max() - cursor) {
                groupOverflowed = true;
            } else {
                cursor += inputGroupAdvance;
            }
        }
        if (groupOverflowed) {
            submissionOverflowed = true;
            continue;
        }
        // ── 인계받은 결함 5의 나머지 절반 ──────────────────────────────────
        // 렌더 레코드를 내지 않는 동작 대상(장식 없는 selectable/입력창
        // 껍데기)은 groupBaseSubmission을 그대로 썼다. 그 번호는 이 묶음의
        // 첫 레코드의 것이거나, 이 묶음이 아무 레코드도 내지 않았다면
        // **다음 오브젝트의 첫 레코드**의 것이다 — 어느 쪽이든 그 hit
        // 레코드가 아무도 차지하지 않은(또는 남의) 서수를 이름으로 갖는다.
        // 자기 한 자리를 뒤에 예약한다.
        //
        // ── 그 절반의 나머지 절반 ──────────────────────────────────────────
        // actionEmitsRender는 "렌더 레코드를 낼 **것이다**"라는 예측이지
        // "냈다"는 사실이 아니다. 비어 있지 않은 라벨은 그 값을 참으로
        // 만들지만, 그 라벨의 레코드는 TextLayoutService::Layout이 성공했을
        // 때만 만들어진다 — family 해석 실패, 분석/측정 실패 등 여러 경로가
        // nullopt를 돌려준다. 그때 예전 코드는 예약도 하지 않고 자기 번호도
        // 찾지 못해 groupBaseSubmission을 그대로 들었고, 그 번호는 같은
        // 오브젝트의 다른 레코드(상호작용 불가능한 UIButton의 솔리드가 그
        // 모양이다)나 다음 오브젝트의 첫 레코드의 것이었다.
        //
        // 그래서 예약 여부를 예측이 아니라 **실제로 찾았는가**로 정한다.
        std::uint64_t actionSubmission = groupBaseSubmission;
        bool actionOrdinalClaimed = false;
        if (actionComponent && actionEmitsRender) {
            const UIRuntimeTargetIdentity actionTarget =
                CaptureTarget(world, *actionComponent);
            for (const auto& item : local) {
                if (item.source != actionTarget) continue;
                actionSubmission = item.order.stableSubmissionIndex;
                actionOrdinalClaimed = true;
                break;
            }
        }
        if (actionComponent && !actionOrdinalClaimed) {
            if (cursor == std::numeric_limits<std::uint64_t>::max()) {
                submissionOverflowed = true;
                continue;
            }
            actionSubmission = cursor;
            ++cursor;
        }
        for (const auto& item : local) {
            renderItems.push_back(item);
        }
        nextSubmission = cursor;

        if (actionComponent) {
            UIHitTargetSnapshot hit;
            hit.target = CaptureTarget(world, *actionComponent);
            hit.canonicalTarget =
                MakeStableKey(objectId, *actionComponent, actionSchema);
            hit.order = makeOrder(actionEmitsRender ? actionSortingOrder : 0);
            hit.order.stableSubmissionIndex = actionSubmission;
            hit.logicalRect = rect;
            // 렌더와 hit이 같은 클립을 든다. 두 번 계산하면 언젠가 갈리고,
            // 그때 보이지 않는 버튼이 생긴다.
            hit.logicalClip = clip;
            // ── 인계받은 결함 5 (Task 11.1 -> 11.2) ─────────────────────
            // 이 값은 무조건 참이었다. 진짜 답은 몇 줄 위에서 이미 계산되어
            // 이 노드에 실려 있다(ObjectIsInteractionEligible). 무조건 참인
            // 필드는 "상호작용할 수 있는 대상"과 "그저 hit 대상"을 구별하지
            // 못하므로, 그 값을 읽는 첫 라우팅이 선택 불가 버튼을 누른다.
            hit.interactable = node.interactionEligible;
            if (selectable && selectable->Interactable()) {
                hit.focusTarget = MakeFrozenTarget(
                    world, objectId, *selectable,
                    UISelectable::CurrentSchemaVersion);
            }
            // Step 1h 표만이 텍스트 대상을 끄는 자리다. 유효한 라벨 주장이
            // 라벨 렌더를 억누르는 것과 이 껍데기의 동작 레코드를 지우는 것은
            // 다른 일이다.
            if (input && !input->ReadOnly()) {
                bool inputUsable = false;
                for (const auto& claim : inputClaims) {
                    if (claim.input != input) continue;
                    inputUsable = !claim.conflicted && claim.rendered != nullptr;
                    break;
                }
                if (inputUsable) {
                    hit.textInputTarget = MakeFrozenTarget(
                        world, objectId, *input,
                        UITextInput::CurrentSchemaVersion);
                }
            }
            hit.focusable = hit.focusTarget.has_value();
            hit.acceptsTextInput = hit.textInputTarget.has_value();
            // 안쪽에서 바깥쪽으로. 순서 자체가 계약이다 — 스크롤 입력은
            // 가장 안쪽부터 변위를 소비한다.
            for (GameObject* ancestor = object->GetParent(); ancestor;
                 ancestor = ancestor->GetParent()) {
                auto* scroll = EnabledComponent<UIScrollView>(*ancestor);
                if (!scroll) continue;
                UIFrozenTarget frozen =
                    MakeFrozenTarget(world, ancestor->GetID(), *scroll,
                                     UIScrollView::CurrentSchemaVersion);
                // 붙들 수 없는 대상은 뺀다. 같은 objectId를 가진 다른
                // 컴포넌트로 대체하지 않는다.
                if (!frozen) continue;
                hit.scrollTargets.push_back(std::move(frozen));
            }
            // Step 5f: 저작된 방향 정책을 얼린다. 명시 참조는 여기서 한 번만
            // 해석되고, 나중 포커스 투사는 이 필드만 소비한다.
            if (selectable) {
                hit.navigation.mode = selectable->NavigationMode();
                const std::array<SceneObjectRef, 4> refs = {
                    selectable->NavigateUp(), selectable->NavigateDown(),
                    selectable->NavigateLeft(), selectable->NavigateRight()};
                for (std::size_t axis = 0; axis < refs.size(); ++axis) {
                    if (!refs[axis].IsSet()) continue;
                    GameObject* target = world.FindById(refs[axis].ObjectId());
                    UISelectable* targetSelectable =
                        target ? target->GetComponent<UISelectable>() : nullptr;
                    if (!targetSelectable || !targetSelectable->IsEnabled()) {
                        molga::text::TextDiagnostic diagnostic;
                        diagnostic.code =
                            molga::text::TextDiagnosticCode::ReferenceInvalid;
                        diagnostic.severity = molga::text::TextSeverity::Warning;
                        diagnostic.subsystem = "ui.layout";
                        diagnostic.message =
                            "UISelectable explicit navigation reference does "
                            "not resolve to an enabled UISelectable";
                        diagnostic.remediation =
                            "point explicit navigation at an enabled "
                            "UISelectable in this scene";
                        diagnostic.sceneObjectId = objectId;
                        diagnostic.componentType = "UISelectable";
                        sink.Report(std::move(diagnostic));
                        continue;
                    }
                    hit.navigation.explicitTargets[axis] =
                        CaptureTarget(world, *targetSelectable);
                    hit.navigation.canonicalTargets[axis] = MakeStableKey(
                        target->GetID(), *targetSelectable,
                        UISelectable::CurrentSchemaVersion);
                }
            }
            hitTargets.push_back(std::move(hit));
        }

        // ── Step 3f: 입력창이 소유한 라벨과 IME 기하 ────────────────────────
        if (input) {
            for (const auto& claim : inputClaims) {
                if (claim.input != input) continue;
                if (claim.conflicted || !claim.rendered) break;
                const auto frozenInput = MakeFrozenTarget(
                    world, objectId, *input, UITextInput::CurrentSchemaVersion);
                if (!frozenInput) break;
                const unsigned int renderedId =
                    claim.rendered->GetGameObject()->GetID();

                UITextInputLabelSnapshot owned;
                owned.input = frozenInput;
                owned.renderedLabel.label =
                    MakeFrozenTarget(world, renderedId, *claim.rendered,
                                     UILabel::CurrentSchemaVersion);
                owned.renderedLabel.requestTemplate = BuildLabelRequestFixed(
                    *claim.rendered, renderedId, rect);
                // 렌더 라벨의 폰트/스타일은 출처일 뿐 입력 텍스트의 권한이
                // 아니다. 색만 그대로 입력 텍스트 색으로 쓴다.
                owned.renderedLabel.color = claim.rendered->GetColor();
                owned.renderedLabel.enabledAndVisible = true;

                const UITextInputVisualState* state = inputState;
                const bool focused = state && state->focused;
                const bool emptyText = !state || state->committedUtf8.empty();
                if (claim.placeholder) {
                    const unsigned int placeholderId =
                        claim.placeholder->GetGameObject()->GetID();
                    UIFrozenInputLabelVisual visual;
                    visual.label = MakeFrozenTarget(
                        world, placeholderId, *claim.placeholder,
                        UILabel::CurrentSchemaVersion);
                    visual.requestTemplate = BuildLabelRequestFixed(
                        *claim.placeholder, placeholderId, rect);
                    visual.color = claim.placeholder->GetColor();
                    // 비어 있고 포커스가 없을 때만 보인다. 포커스만으로
                    // 감추면 빈 입력창이 포커스를 잃은 뒤에도 안내 문구가
                    // 돌아오지 않는다.
                    visual.enabledAndVisible = emptyText && !focused;
                    owned.placeholderLabel = std::move(visual);
                }

                // 예약을 셀 때 쓴 그 요청 그대로다. 여기서 다시 만들면 예약된
                // 칸 수와 게시된 요청이 조용히 갈릴 수 있다.
                owned.effectiveInputRequestTemplate =
                    effectiveInputRequest
                        ? *effectiveInputRequest
                        : BuildEffectiveInputRequest(
                              *input,
                              state ? state->committedUtf8 +
                                          state->compositionUtf8
                                    : input->InitialText(),
                              logicalViewport, renderedId);
                owned.logicalViewport = rect;
                owned.logicalClip = clip;
                owned.baseOrder = makeOrder(0);
                // 이 오브젝트가 낸 평범한 레코드 *뒤*에서 시작한다. 배경
                // 스프라이트의 키와 같은 값을 게시하면 그 둘의 순서가 정의되지
                // 않는다.
                owned.baseOrder.stableSubmissionIndex = inputGroupBase;
                owned.reservedCommandSpan = inputGroupSpan;
                textInputLabels.push_back(std::move(owned));

                UITextInputImeGeometrySnapshot ime;
                ime.input = frozenInput;
                ime.logicalInputArea = rect;
                ime.logicalCursor = molga::FixedPoint{rect.x, rect.y};
                ime.logicalClip = clip;
                // caret이 지금 그려지는가와 무관하다. caretVisible에 묶으면
                // 깜빡임이 꺼진 프레임에 OS 후보창이 화면 구석으로 튄다.
                ime.focused = focused;
                textInputImeGeometry.push_back(std::move(ime));
                break;
            }
        }
    }
    if (submissionOverflowed) {
        impl.ReportInvalid(
            sink,
            "a UI render group exhausted the stable submission index; that "
            "complete source group was omitted",
            "reduce the number of glyphs drawn on this UI surface");
    }
    // Step 4c: 두 벡터를 같은 키로 한 번씩 정렬한다.
    std::sort(renderItems.begin(), renderItems.end(),
              [](const UIRenderItemSnapshot& a, const UIRenderItemSnapshot& b) {
                  return a.order < b.order;
              });
    std::sort(hitTargets.begin(), hitTargets.end(),
              [](const UIHitTargetSnapshot& a, const UIHitTargetSnapshot& b) {
                  return a.order < b.order;
              });

    auto snapshot = std::make_shared<UISnapshot>();
    snapshot->surfaceWindowId = surfaceWindowId;
    snapshot->worldGeneration = world.Generation();
    snapshot->logicalViewport = logicalViewport;
    snapshot->nodes = std::move(orderedNodes);
    snapshot->renderItems = std::move(renderItems);
    snapshot->hitTargets = std::move(hitTargets);
    snapshot->textInputLabels = std::move(textInputLabels);
    snapshot->textInputImeGeometry = std::move(textInputImeGeometry);
    UISnapshotPtr published = snapshot;

    if (!uncacheable) {
        bool replaced = false;
        for (auto& slot : impl.fullSlots) {
            if (!(slot.slot == slotKey)) continue;
            // 전체 미스는 그 슬롯을 교체한다. 포커스/스크롤/깜빡임/업로드 키의
            // 역사를 쌓으면 슬롯 수가 상한 없이 늘어난다.
            slot.key = completeKey;
            slot.snapshot = published;
            replaced = true;
            break;
        }
        if (!replaced) {
            Impl::FullSlot slot;
            slot.slot = slotKey;
            slot.key = std::move(completeKey);
            slot.snapshot = published;
            impl.fullSlots.push_back(std::move(slot));
        }
        impl.lastStamp = stamp;
        impl.hasStamp = true;
        impl.lastSnapshot = published;
    }
    return published;
}

} // namespace molga::ui
