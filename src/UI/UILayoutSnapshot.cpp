#include "UI/UILayoutSnapshot.h"

#include "Rendering/TextureBindingRegistry.h"
#include "UI/UILayoutTypes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace molga::ui {
namespace {

// nlohmann::json(std::map)은 키를 알파벳순으로 재배열한다. 정규 JSON은 바이트
// 단위로 비교되므로 필드 순서도 계약의 일부다. ordered_json은 삽입 순서를
// 유지하므로 x, y, width, height 처럼 읽기 좋은 순서를 그대로 고정할 수 있다.
nlohmann::ordered_json RectJson(const molga::FixedRect& rect) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["x"] = rect.x.Raw();
    out["y"] = rect.y.Raw();
    out["width"] = rect.width.Raw();
    out["height"] = rect.height.Raw();
    return out;
}

nlohmann::ordered_json SizeJson(const molga::FixedSize& size) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["width"] = size.width.Raw();
    out["height"] = size.height.Raw();
    return out;
}

nlohmann::ordered_json PointJson(const molga::FixedPoint& point) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["x"] = point.x.Raw();
    out["y"] = point.y.Raw();
    return out;
}

// ── Task 11.2: 정규 JSON의 색은 한 규칙으로만 나간다 ────────────────────────
// 이 파일의 다른 모든 값은 검증된 26.6 raw 정수다. 색만 날 float으로 나가고
// 있었고, 그래서 이 하위 시스템의 두 전역 규칙 — 부호 있는 0을 양의 0으로
// 정규화한다, 유한하지 않은 값을 거절한다 — 이 색에서만 성립하지 않았다.
//
//  - -0.0f는 0.0f와 같은 값이지만 다른 바이트("-0.0")로 인쇄된다. 그러면 같은
//    저작 색을 가진 두 빌드가 서로 다른 정규 바이트를 내고, "cold 빌드와 warm
//    빌드가 바이트 동일한 스냅샷을 낸다"는 Exit Contract가 그 자리에서 깨진다.
//  - NaN/Inf는 JSON에 수로 인쇄될 수 없다(nlohmann은 null을 쓴다). 조용히
//    null이 되면 서로 다른 두 깨진 색이 같은 바이트를 갖고, 그 사실을 아무도
//    모른다. 그래서 거절된 성분은 문자열 "non-finite"로 나간다 — null은 이
//    문서에서 이미 "값이 없다"는 뜻이므로(OptionalRectJson) 그것을 쓰면
//    거절이 부재와 별칭이 된다. 아래 kNonFiniteChannel의 주석이 그 이유다.
//
// 네 곳에 흩어져 있던 같은 배열 리터럴을 이 함수 하나로 접는다.
// 거절된 성분의 표기. null이 **아니어야 한다** — 이 문서에서 null은
// "값이 없다"는 뜻이고(OptionalRectJson이 그렇게 쓴다), nlohmann은 유한하지
// 않은 float도 아무 말 없이 null로 인쇄한다. 그러면 "색이 없다"와 "색이
// NaN이다"가 바이트로 구별되지 않고, 거절이 있는 구현과 없는 구현이 같은
// 정규 바이트를 낸다 — 거절을 통째로 지워도 아무 시험이 움직이지 않는다.
constexpr const char* kNonFiniteChannel = "non-finite";

nlohmann::ordered_json ColorChannelJson(float value) {
    if (!std::isfinite(value)) {
        return nlohmann::ordered_json(std::string(kNonFiniteChannel));
    }
    // 부호 있는 0을 양의 0으로. value == 0.0f 는 -0.0f 에도 참이다.
    return nlohmann::ordered_json(value == 0.0f ? 0.0f : value);
}

nlohmann::ordered_json ColorJson(const Color& color) {
    return nlohmann::ordered_json::array(
        {ColorChannelJson(color.r), ColorChannelJson(color.g),
         ColorChannelJson(color.b), ColorChannelJson(color.a)});
}

nlohmann::ordered_json OptionalRectJson(
    const std::optional<molga::FixedRect>& rect) {
    if (!rect) return nlohmann::ordered_json(nullptr);
    return RectJson(*rect);
}

// 정규 키 하나. 런타임 정체성 네 필드는 여기 나타나지 않는다 — 그것들은
// 이 프로세스의 할당 순번이다.
nlohmann::ordered_json StableKeyJson(const UIStableComponentKey& key) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["sceneObjectId"] = key.sceneObjectId;
    out["componentTypeName"] = key.componentTypeName;
    out["componentSchemaVersion"] = key.componentSchemaVersion;
    return out;
}

nlohmann::ordered_json FrozenTargetJson(const UIFrozenTarget& target) {
    // runtimeTarget은 통째로 빠진다. 정규 바이트에 들어가는 것은 정규 키뿐이다.
    return StableKeyJson(target.canonicalTarget);
}

nlohmann::ordered_json OptionalFrozenTargetJson(
    const std::optional<UIFrozenTarget>& target) {
    if (!target) return nlohmann::ordered_json(nullptr);
    return FrozenTargetJson(*target);
}

nlohmann::ordered_json DrawOrderJson(const UIDrawOrderKey& order) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["canvasSortingOrder"] = order.canvasSortingOrder;
    out["siblingPath"] = order.siblingPath;
    out["componentSortingOrder"] = order.componentSortingOrder;
    out["stableSubmissionIndex"] = order.stableSubmissionIndex;
    return out;
}

// 불변 배치의 의미 구조. atlas page/UV/glyph bitmap 경계는 하나도 들어가지
// 않는다 — 그 셋은 같은 내용이라도 업로드 순서와 장치에 따라 달라진다.
nlohmann::ordered_json TextLayoutJson(const molga::text::TextLayout& layout) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["intrinsicSize"] = SizeJson(layout.intrinsicSize);
    out["clipped"] = layout.clipped;
    out["ellipsized"] = layout.ellipsized;
    nlohmann::ordered_json lines = nlohmann::ordered_json::array();
    for (const auto& line : layout.lines) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["sourceBytes"] = nlohmann::ordered_json{
            {"begin", line.sourceBytes.begin}, {"end", line.sourceBytes.end}};
        entry["graphemes"] = nlohmann::ordered_json{
            {"begin", line.graphemes.begin}, {"end", line.graphemes.end}};
        entry["baseline"] = line.baseline.Raw();
        entry["advance"] = line.advance.Raw();
        entry["ascent"] = line.ascent.Raw();
        entry["descent"] = line.descent.Raw();
        entry["lineGap"] = line.lineGap.Raw();
        entry["top"] = line.top.Raw();
        entry["bottom"] = line.bottom.Raw();
        std::uint64_t glyphs = 0;
        for (const auto& run : line.visualRuns) glyphs += run.glyphs.size();
        entry["glyphCount"] = glyphs;
        lines.push_back(std::move(entry));
    }
    out["lines"] = std::move(lines);
    return out;
}

nlohmann::ordered_json PayloadJson(const UIRenderPayload& payload) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    if (const auto* sprite = std::get_if<UISpriteSnapshot>(&payload)) {
        out["kind"] = "sprite";
        // 저작된 GUID와 검증된 내용 SHA, 그리고 그 둘에서 유도된 안정 ID까지가
        // 정규 정체성이다. TextureRuntimeBindingIdentity는 통째로 빠진다 —
        // 같은 내용을 다시 올리거나 장치를 다시 만들어도 바이트가 같아야 한다.
        out["textureGuid"] = sprite->textureGuid;
        out["textureContentSha256"] = sprite->textureContentSha256;
        out["textureContentStableId"] = sprite->textureContentStableId;
        out["tint"] = ColorJson(sprite->tint);
        return out;
    }
    if (const auto* text = std::get_if<UITextSnapshot>(&payload)) {
        out["kind"] = "text";
        out["origin"] = PointJson(text->origin);
        out["color"] = ColorJson(text->color);
        out["layout"] = text->layout ? TextLayoutJson(*text->layout)
                                     : nlohmann::ordered_json(nullptr);
        return out;
    }
    const auto& solid = std::get<UISolidRectSnapshot>(payload);
    out["kind"] = "solid";
    out["color"] = ColorJson(solid.color);
    return out;
}

nlohmann::ordered_json NavigationJson(const UINavigationSnapshot& navigation) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["mode"] = std::string(ToCanonicalString(navigation.mode));
    nlohmann::ordered_json targets = nlohmann::ordered_json::array();
    for (const auto& target : navigation.canonicalTargets) {
        targets.push_back(target ? StableKeyJson(*target)
                                 : nlohmann::ordered_json(nullptr));
    }
    out["explicitTargets"] = std::move(targets);
    return out;
}

nlohmann::ordered_json InputLabelVisualJson(
    const UIFrozenInputLabelVisual& visual) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    out["label"] = FrozenTargetJson(visual.label);
    out["color"] = ColorJson(visual.color);
    // enabledAndVisible은 여기 없다. 그 값은 focused와 "커밋된 글이 비어
    // 있는가"를 그대로 담은 런타임 편집 상태이고, Step 7이 정규 JSON에서
    // 빼라고 명시한 부류다. 같은 저작 씬에서 사용자가 입력창을 누르기만 해도
    // 바이트가 달라지면 편집 이력이 다른 두 빌드의 parity 비교가 무너진다 —
    // UISnapshot::textInputImeGeometry를 통째로 뺀 것과 같은 이유다.
    return out;
}

} // namespace

bool UIDrawOrderKey::operator<(const UIDrawOrderKey& other) const {
    if (canvasSortingOrder != other.canvasSortingOrder) {
        return canvasSortingOrder < other.canvasSortingOrder;
    }
    if (siblingPath != other.siblingPath) {
        // 사전식 비교라 접두사인 경로가 먼저 온다(조상 -> 자손).
        return std::lexicographical_compare(
            siblingPath.begin(), siblingPath.end(), other.siblingPath.begin(),
            other.siblingPath.end());
    }
    if (componentSortingOrder != other.componentSortingOrder) {
        return componentSortingOrder < other.componentSortingOrder;
    }
    return stableSubmissionIndex < other.stableSubmissionIndex;
}

bool UIDrawOrderKey::operator==(const UIDrawOrderKey& other) const {
    return canvasSortingOrder == other.canvasSortingOrder &&
           siblingPath == other.siblingPath &&
           componentSortingOrder == other.componentSortingOrder &&
           stableSubmissionIndex == other.stableSubmissionIndex;
}

// ── Step 1j: C++17 값 동등성 ────────────────────────────────────────────────
// 전부 필드별이다. 오브젝트 표현 비교(memcmp)는 패딩 바이트를 함께 보므로
// 같은 값이 다르게 비교될 수 있고, C++20의 rewritten comparison은 이 타깃에
// 없다.
bool UIStableComponentKey::operator==(
    const UIStableComponentKey& other) const noexcept {
    return sceneObjectId == other.sceneObjectId &&
           componentTypeName == other.componentTypeName &&
           componentSchemaVersion == other.componentSchemaVersion;
}

bool UIFrozenTarget::operator==(const UIFrozenTarget& other) const noexcept {
    return runtimeTarget == other.runtimeTarget &&
           canonicalTarget == other.canonicalTarget;
}

bool TextureRuntimeBindingIdentity::operator==(
    const TextureRuntimeBindingIdentity& other) const noexcept {
    // 핸들 비교는 resource index와 handle generation을 모두 본다
    // (ResourceHandle::operator==). 해시만 맞춰 보는 비교는 파괴 후 재사용된
    // 슬롯을 같은 핸들로 읽는다.
    return deviceGeneration == other.deviceGeneration &&
           uploadGeneration == other.uploadGeneration &&
           texture == other.texture && sampler == other.sampler &&
           lifetimeIdentity == other.lifetimeIdentity;
}

bool UIRuntimeBindingCacheIdentity::operator==(
    const UIRuntimeBindingCacheIdentity& other) const noexcept {
    return sceneObjectId == other.sceneObjectId &&
           componentTypeName == other.componentTypeName &&
           componentSchemaVersion == other.componentSchemaVersion &&
           binding == other.binding;
}

bool UIScrollDisplacementCacheIdentity::operator==(
    const UIScrollDisplacementCacheIdentity& other) const noexcept {
    return scrollTarget == other.scrollTarget &&
           offsetXRaw == other.offsetXRaw && offsetYRaw == other.offsetYRaw &&
           viewport == other.viewport && content == other.content;
}

bool UITextInputImeGeometrySnapshot::operator==(
    const UITextInputImeGeometrySnapshot& other) const noexcept {
    return input == other.input &&
           logicalInputArea == other.logicalInputArea &&
           logicalCursor.x == other.logicalCursor.x &&
           logicalCursor.y == other.logicalCursor.y &&
           logicalClip == other.logicalClip && focused == other.focused;
}

std::string StableLayoutSnapshotJson(const UISnapshot& snapshot) {
    nlohmann::ordered_json document = nlohmann::ordered_json::object();
    document["logicalViewport"] = SizeJson(snapshot.logicalViewport);

    nlohmann::ordered_json nodes = nlohmann::ordered_json::array();
    for (const UILayoutNodeSnapshot& node : snapshot.nodes) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        // 런타임 식별자 네 필드 중 씬에 저장되는 것은 objectId 하나뿐이다.
        entry["objectId"] = node.rectTransform.objectId;
        entry["logicalRect"] = RectJson(node.logicalRect);
        entry["intrinsicSize"] = SizeJson(node.intrinsicSize);
        // 클립 없음과 클립 있음은 서로 다른 상태다. 키를 아예 빼는 대신
        // 명시적 null을 남기면 노드의 키 집합이 항상 같아서, 두 스냅샷의
        // 정규 바이트 차이가 언제나 "같은 필드의 값 차이"로 읽힌다.
        entry["logicalClip"] = OptionalRectJson(node.logicalClip);
        // layoutRevision은 여기에 들어가지 않는다. 이름 그대로 재계산/편집
        // 횟수에 달린 프로세스 지역 순번이라, 넣으면 같은 authored 입력이
        // cold 빌드와 편집을 거친 warm 빌드에서 다른 바이트를 낸다. 07 subplan
        // 의 canonical 금지 키 목록(frameIndex, worldGeneration,
        // componentRuntimeTypeId, componentInstanceId, layoutRevision,
        // timestamp, physicalPixel ...)이 이를 명시한다. 런타임 레코드에는
        // 남아 있고 정규 바이트에서만 빠진다.
        entry["interactionEligible"] = node.interactionEligible;
        nodes.push_back(std::move(entry));
    }
    document["nodes"] = std::move(nodes);

    // ── Step 7: 의미 페이로드 필드만 ────────────────────────────────────────
    nlohmann::ordered_json renderItems = nlohmann::ordered_json::array();
    for (const UIRenderItemSnapshot& item : snapshot.renderItems) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["canonicalSource"] = StableKeyJson(item.canonicalSource);
        entry["order"] = DrawOrderJson(item.order);
        entry["logicalRect"] = RectJson(item.logicalRect);
        entry["logicalClip"] = OptionalRectJson(item.logicalClip);
        entry["reservedCommandSpan"] = item.reservedCommandSpan;
        entry["payload"] = PayloadJson(item.payload);
        renderItems.push_back(std::move(entry));
    }
    document["renderItems"] = std::move(renderItems);

    nlohmann::ordered_json hitTargets = nlohmann::ordered_json::array();
    for (const UIHitTargetSnapshot& hit : snapshot.hitTargets) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["canonicalTarget"] = StableKeyJson(hit.canonicalTarget);
        entry["focusTarget"] = OptionalFrozenTargetJson(hit.focusTarget);
        entry["textInputTarget"] = OptionalFrozenTargetJson(hit.textInputTarget);
        nlohmann::ordered_json scrolls = nlohmann::ordered_json::array();
        // 안쪽에서 바깥쪽 순서 그대로다. 정렬하지 않는다 — 순서 자체가 계약이다.
        for (const auto& scroll : hit.scrollTargets) {
            scrolls.push_back(FrozenTargetJson(scroll));
        }
        entry["scrollTargets"] = std::move(scrolls);
        entry["order"] = DrawOrderJson(hit.order);
        entry["logicalRect"] = RectJson(hit.logicalRect);
        entry["logicalClip"] = OptionalRectJson(hit.logicalClip);
        entry["interactable"] = hit.interactable;
        // scrollTargets와 같은 규칙이다: 정규 키만, 런타임 정체성은 없다.
        entry["pointerOwner"] = OptionalFrozenTargetJson(hit.pointerOwner);
        entry["focusable"] = hit.focusable;
        entry["acceptsTextInput"] = hit.acceptsTextInput;
        entry["navigation"] = NavigationJson(hit.navigation);
        hitTargets.push_back(std::move(entry));
    }
    document["hitTargets"] = std::move(hitTargets);

    nlohmann::ordered_json inputLabels = nlohmann::ordered_json::array();
    for (const UITextInputLabelSnapshot& owned : snapshot.textInputLabels) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["input"] = FrozenTargetJson(owned.input);
        entry["renderedLabel"] = InputLabelVisualJson(owned.renderedLabel);
        entry["placeholderLabel"] =
            owned.placeholderLabel
                ? InputLabelVisualJson(*owned.placeholderLabel)
                : nlohmann::ordered_json(nullptr);
        entry["logicalViewport"] = RectJson(owned.logicalViewport);
        entry["logicalClip"] = OptionalRectJson(owned.logicalClip);
        entry["baseOrder"] = DrawOrderJson(owned.baseOrder);
        entry["reservedCommandSpan"] = owned.reservedCommandSpan;
        inputLabels.push_back(std::move(entry));
    }
    document["textInputLabels"] = std::move(inputLabels);

    return document.dump();
}

} // namespace molga::ui
