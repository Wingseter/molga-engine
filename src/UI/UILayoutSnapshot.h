#pragma once

#include "Common/Fixed26_6.h"
#include "Platform/Window.h"
#include "UI/UIRuntimeIdentity.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace molga::ui {

// 배치 결과 한 노드. 좌표는 전부 검증된 26.6 논리 단위이며 비교는 raw 정수로만
// 한다. frameIndex/타임스탬프/물리 픽셀 사각형/날 포인터/GPU 핸들은 여기에
// 들어오지 않는다 — 그런 필드가 하나라도 있으면 프레임마다 값이 달라져
// "변경 없는 프레임은 같은 스냅샷을 재사용한다"는 계약이 절대 성립하지 않는다.
// logicalClip은 이미 조상들과 교집합을 취한 결과이고, 비어 있는 교집합은 노드
// 자체를 제거하므로 여기에는 나타나지 않는다.
struct UILayoutNodeSnapshot {
    UIRuntimeTargetIdentity rectTransform;
    molga::FixedRect logicalRect;
    molga::FixedSize intrinsicSize;
    std::optional<molga::FixedRect> logicalClip;
    std::uint64_t layoutRevision = 0;
    bool interactionEligible = false;
};

// 한 UI 표면의 의미(semantic) 스냅샷. 게시된 뒤에는 불변이며, 같은 프레임의
// 렌더와 hit-test가 같은 인스턴스를 공유한다.
//
// 게시 후 변경 금지. 불변성을 강제하는 것은 아래 UISnapshotPtr의 const뿐이고
// 구조체 자체는 평범한 집합체이므로, 게시하기 전에 shared_ptr<const>로 넘겨
// 어떤 소유자도 나중에 수정할 수 없게 해야 한다. 게시된 스냅샷을 고쳐 쓰면
// 이미 그 인스턴스를 들고 있는 이번 프레임의 렌더/hit-test가 서로 다른 값을
// 보게 된다.
//
// nodes는 게시하는 쪽이 UIDrawOrderKey 오름차순으로 정렬해서 넣는다.
// UILayoutNodeSnapshot이 draw order key를 담지 않는 것은 의도된 설계라서
// (키는 렌더/hit 페이로드가 따로 들고 다닌다) 직렬화기는 스스로 그 순서를
// 만들 수 없고 주어진 순서를 그대로 내보낸다.
struct UISnapshot {
    molga::WindowId surfaceWindowId = 0;
    std::uint64_t worldGeneration = 0;
    molga::FixedSize logicalViewport;
    std::vector<UILayoutNodeSnapshot> nodes;
};

using UISnapshotPtr = std::shared_ptr<const UISnapshot>;

// 진단/parity 비교용 정규 JSON. raw 26.6 정수만 내보내며(부동소수 표기는
// 반올림 때문에 바이트 동일성을 깨뜨린다) 프로세스나 편집 이력에 따라 달라지는
// 값은 전부 뺀다: surfaceWindowId는 에디터/런타임의 창 할당이고,
// worldGeneration과 UIRuntimeTargetIdentity의 componentInstanceId/
// componentRuntimeTypeId는 이 프로세스의 할당 순번이며, layoutRevision은
// 재계산/편집 횟수에 달린 순번이다. 하나라도 넣으면 같은 authored 입력이
// 실행마다, 또는 cold 빌드와 편집을 거친 warm 빌드 사이에서 다른 바이트를
// 내서 parity 비교가 무너진다. 노드에서 내보내는 식별자는 씬에 저장되는
// objectId 하나뿐이다.
//
// 사전조건: snapshot.nodes는 이미 UIDrawOrderKey 오름차순으로 정렬되어
// 있어야 한다. 노드가 draw order key를 담지 않으므로 이 함수는 그 순서를
// 만들 수도, 검사할 수도 없다 — 정렬되지 않은 목록을 넘기면 진단 없이 다른
// 바이트가 나온다. 정렬은 스냅샷을 게시하는 쪽의 의무다.
std::string StableLayoutSnapshotJson(const UISnapshot&);

} // namespace molga::ui
