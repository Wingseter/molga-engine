#include "UI/UILayoutSnapshot.h"

#include "UI/UILayoutTypes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
        if (node.logicalClip) {
            entry["logicalClip"] = RectJson(*node.logicalClip);
        } else {
            entry["logicalClip"] = nullptr;
        }
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

    return document.dump();
}

} // namespace molga::ui
