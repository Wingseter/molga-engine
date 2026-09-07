#pragma once

#include <cstdint>
#include <vector>

namespace molga::ui {

// 하나의 UI 표면 안에서 draw order를 유일하게 결정하는 키. 렌더 수집과
// hit-test가 같은 키를 쓰고, hit-test는 이 순서의 정확한 역순을 본다.
//
// 필드 우선순위는 Canvas sort -> hierarchy sibling path -> component sorting
// order -> stable submission index 순이며, 이 순서 자체가 계약이다. 낮은
// 우선순위 필드가 반대 방향을 가리키는 경우에 비교가 갈리므로, 우선순위를
// 뒤바꾼 구현은 그런 쌍에서만 드러난다.
//
// siblingPath는 루트부터의 형제 인덱스 열이라 사전식으로 비교한다. 접두사인
// 경로가 먼저 오므로 조상이 자손보다 앞선다.
//
// 비교를 memcmp로 하면 안 된다: 이 구조체에는 패딩이 있고 패딩 바이트의 값은
// 정해져 있지 않아 같은 키가 다르게 비교될 수 있다. 그래서 필드별로 비교한다.
// C++17 타깃이므로 사용하는 ==/!=/< 는 전부 명시적으로 선언한다(rewritten
// comparison도 defaulted operator<=>도 없다).
//
// operator< 와 operator== 의 정의는 UILayoutSnapshot.cpp에 있다.
struct UIDrawOrderKey {
    std::int32_t canvasSortingOrder = 0;
    std::vector<std::uint32_t> siblingPath;
    std::int32_t componentSortingOrder = 0;
    std::uint64_t stableSubmissionIndex = 0;
    bool operator<(const UIDrawOrderKey&) const;
    bool operator==(const UIDrawOrderKey&) const;
    bool operator!=(const UIDrawOrderKey& other) const {
        return !(*this == other);
    }
};

} // namespace molga::ui
