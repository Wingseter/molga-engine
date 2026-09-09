#pragma once

#include "ECS/GameObject.h"

namespace molga::ui {

// 이 오브젝트와 그 조상이 **모두** 활성인가. UI 표면이 "이 서브트리는 지금
// 화면에 존재하는가"를 묻는 유일한 술어다.
//
// 사본은 하나다. 이 규칙은 세 자리에서 필요하고(배치 수집, 입력/포커스 순회,
// 스크롤 해석) 세 벌로 두면 언젠가 갈린다 — 그리고 그 어긋남은 "숨긴 패널이
// 깨진 참조로 보고된다" 같은 조용한 결함으로만 드러난다.
inline bool IsHierarchyActive(const GameObject* object) {
    for (const GameObject* node = object; node; node = node->GetParent()) {
        if (!node->IsActive()) return false;
    }
    return true;
}

} // namespace molga::ui
