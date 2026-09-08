#include "UI/UILayoutSystem.h"

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
#include "UI/UILayoutTypes.h"
#include "UI/UIRuntimeInvalidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

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
    bool hasClip = false;
    RawRect clip;
    bool dropped = false;
};

bool IsHierarchyActive(const GameObject* object) {
    for (const GameObject* node = object; node; node = node->GetParent()) {
        if (!node->IsActive()) return false;
    }
    return true;
}

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

bool UILayoutGeometryCacheKey::operator==(
    const UILayoutGeometryCacheKey& other) const {
    return worldGeneration == other.worldGeneration &&
           viewport == other.viewport &&
           viewportGeneration == other.viewportGeneration &&
           canvasScaleRevisions == other.canvasScaleRevisions &&
           hierarchyAndSiblingRevisions == other.hierarchyAndSiblingRevisions &&
           rectAndLayoutRevisions == other.rectAndLayoutRevisions &&
           intrinsicGenerations == other.intrinsicGenerations;
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
           interaction == other.interaction;
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
    return hash;
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

bool IntersectRects(const RawRect& a, const RawRect& b, RawRect& out) {
    const Raw x0 = std::max(a.x, b.x);
    const Raw y0 = std::max(a.y, b.y);
    const Raw x1 = std::min(a.x + a.width, b.x + b.width);
    const Raw y1 = std::min(a.y + a.height, b.y + b.height);
    if (x1 <= x0 || y1 <= y0) return false;
    out = RawRect{x0, y0, x1 - x0, y1 - y0};
    return true;
}

void ComputeClips(LayoutBuilder& builder, std::size_t index, bool hasInherited,
                  const RawRect& inherited, bool droppedAncestor) {
    LayoutNode& node = builder.nodes[index];
    node.hasClip = hasInherited;
    node.clip = inherited;
    node.dropped = droppedAncestor;
    if (hasInherited && !droppedAncestor) {
        RawRect ignored;
        if (!IntersectRects(inherited, node.resolved, ignored)) node.dropped = true;
    }

    bool childHasClip = hasInherited;
    RawRect childClip = inherited;
    if (node.mask && node.mask->ClipsDescendants()) {
        if (!childHasClip) {
            childClip = node.resolved;
            childHasClip = true;
        } else if (!IntersectRects(childClip, node.resolved, childClip)) {
            // 빈 교집합은 자손 전체를 제거한다. 여기서 clip을 유지하면 자손이
            // 자기 조상보다 넓은 영역에 그려진다.
            childClip = RawRect{};
            for (const auto child : node.children) {
                ComputeClips(builder, child, true, childClip, true);
            }
            return;
        }
    }
    for (const auto child : node.children) {
        ComputeClips(builder, child, childHasClip, childClip, node.dropped);
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

    // 월드마다 최대 256개. front가 MRU다.
    std::unordered_map<std::uint64_t, std::list<GeometryEntry>> geometry;
    std::vector<FullSlot> fullSlots;

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
    const std::size_t hash = HashUILayoutGeometryCacheKey(key);
    for (const auto& entry : found->second) {
        if (entry.hash != hash) continue;
        if (entry.key == key) return true;
    }
    return false;
}
std::optional<UILayoutGeometryCacheKey> UILayoutSystem::LastGeometryKey() const {
    return impl_->lastGeometryKey;
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
    UIIntrinsicLayoutRegistry::Get().ReleaseWorld(worldGeneration);
}

UISnapshotPtr UILayoutSystem::Build(World& world,
                                    molga::WindowId surfaceWindowId,
                                    molga::FixedSize logicalViewport,
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

    const auto clock = UIRuntimeInvalidationClock::Current();
    if (impl.lastDeviceGeneration != 0 &&
        impl.lastDeviceGeneration != clock.deviceGeneration) {
        // 장치가 새로 만들어지면 옛 장치에 묶인 전체 스냅샷은 더 이상 쓸 수
        // 없다. 기하는 장치와 무관하므로 그대로 둔다.
        impl.fullSlots.erase(
            std::remove_if(impl.fullSlots.begin(), impl.fullSlots.end(),
                           [&clock](const Impl::FullSlot& slot) {
                               return slot.slot.deviceGeneration !=
                                      clock.deviceGeneration;
                           }),
            impl.fullSlots.end());
        impl.hasStamp = false;
        impl.lastSnapshot.reset();
    }
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

    const bool clocksCacheable = clock.cacheable && impl.viewportGenerationCacheable;
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
        }
    }
    if (builder.failed) return nullptr;

    // ── 고유 크기와 취소 불가능한 revision ──────────────────────────────────
    bool uncacheable = !clocksCacheable;
    std::string uncacheableKey;
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

        // 고유 크기는 확정된 불변 배치를 가진 쪽이 게시한 것을 읽기만 한다.
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

    if (uncacheable) {
        // Step 8b: 소진된 revision은 캐시 정체성이 상태를 구분하지 못한다는
        // 뜻이다. 조회도 삽입도 하지 않고 매번 새로 만든다. 진단은 같은 집합이
        // 계속 소진되어 있는 동안 한 번만 낸다 — 프레임마다 내면 로그가 그
        // 하나로 가득 찬다.
        if (impl.lastUncacheableKey != uncacheableKey) {
            impl.lastUncacheableKey = uncacheableKey;
            impl.ReportInvalid(
                sink,
                "a reachable UI component exhausted its authored revision; "
                "this surface rebuilds without caching",
                "restart the editor session to reset runtime revisions");
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

    UILayoutGeometryCacheKey geometryKey;
    geometryKey.worldGeneration = world.Generation();
    geometryKey.viewport = logicalViewport;
    geometryKey.viewportGeneration = viewportGeneration;
    geometryKey.canvasScaleRevisions = impl.canvasScratch;
    geometryKey.hierarchyAndSiblingRevisions = impl.hierarchyScratch;
    geometryKey.rectAndLayoutRevisions = impl.rectScratch;
    geometryKey.intrinsicGenerations = impl.intrinsicScratch;
    const std::size_t geometryHash = HashUILayoutGeometryCacheKey(geometryKey);

    std::vector<UIDrawOrderKey> orderedKeys;
    std::vector<UILayoutNodeSnapshot> orderedNodes;
    auto& lru = impl.geometry[geometryKey.worldGeneration];
    bool geometryHit = false;
    if (!uncacheable) {
        for (auto it = lru.begin(); it != lru.end(); ++it) {
            // 해시는 후보를 좁히기만 한다. 값 비교를 건너뛰면 서로 다른 두
            // 씬이 한 항목을 공유할 수 있다.
            if (it->hash != geometryHash || !(it->key == geometryKey)) continue;
            lru.splice(lru.begin(), lru, it);
            orderedKeys = lru.front().order;
            orderedNodes = lru.front().nodes;
            const auto& cachedDropped = lru.front().dropped;
            if (cachedDropped.size() == builder.nodes.size()) {
                for (std::size_t n = 0; n < builder.nodes.size(); ++n) {
                    builder.nodes[n].dropped = cachedDropped[n] != 0;
                }
                geometryHit = true;
                break;
            }
            // 크기가 어긋나면 이 항목은 지금 트리를 말하지 못한다. 조용히
            // 절반만 복원하느니 다시 짓는다.
            break;
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
        for (const auto root : canvasRoots) {
            ComputeClips(builder, root, false, RawRect{}, false);
        }

        std::vector<std::pair<UIDrawOrderKey, UILayoutNodeSnapshot>> emitted;
        emitted.reserve(builder.nodes.size());
        for (const auto& node : builder.nodes) {
            if (node.dropped) continue;
            UILayoutNodeSnapshot record;
            record.rectTransform = CaptureTarget(world, *node.rect);
            record.logicalRect =
                FixedRect{ToFixed(node.resolved.x), ToFixed(node.resolved.y),
                          ToFixed(node.resolved.width),
                          ToFixed(node.resolved.height)};
            record.intrinsicSize =
                FixedSize{ToFixed(node.prefWidth), ToFixed(node.prefHeight)};
            if (node.hasClip) {
                record.logicalClip =
                    FixedRect{ToFixed(node.clip.x), ToFixed(node.clip.y),
                              ToFixed(node.clip.width), ToFixed(node.clip.height)};
            }
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

    UISnapshotCacheKey completeKey;
    completeKey.surfaceWindowId = surfaceWindowId;
    completeKey.geometry = geometryKey;
    completeKey.semanticDirtyGeneration = clock.semanticDirtyGeneration;
    completeKey.scrollDisplacementGeneration = clock.scrollDisplacementGeneration;
    completeKey.textureBindingGeneration = clock.textureBindingGeneration;
    completeKey.deviceGeneration = clock.deviceGeneration;
    completeKey.visualContent = impl.visualScratch;
    completeKey.interaction = impl.interactionScratch;

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

    auto snapshot = std::make_shared<UISnapshot>();
    snapshot->surfaceWindowId = surfaceWindowId;
    snapshot->worldGeneration = world.Generation();
    snapshot->logicalViewport = logicalViewport;
    snapshot->nodes = std::move(orderedNodes);
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
