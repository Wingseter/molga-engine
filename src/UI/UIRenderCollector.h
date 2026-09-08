#pragma once

#include "Rendering/TextRenderer.h"
#include "Text/TextDiagnostic.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutSystem.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace molga {
class RenderQueue;
}

namespace molga::ui {

// ── Task 11.2 Step 5a: 스냅샷과 렌더 큐 사이의 유일한 경계 ───────────────────
// 여기서 일어나는 일은 셋뿐이다.
//
//  1. 이미 정렬된 renderItems를 순서대로 방문한다.
//  2. logicalRect / logicalClip을 UIPhysicalTransform으로 정확히 한 번
//     물리 픽셀로 옮긴다(floor(min)/ceil(max)).
//  3. 그 결과를 명령에 싣는다.
//
// 여기서 일어나지 않는 일이 그만큼 중요하다. World::FindById도, 살아 있는
// 컴포넌트 읽기도, 두 번째 논리->물리 변환도 없다. 하나라도 있으면 게시된
// 스냅샷과 화면이 그 자리에서 갈릴 수 있고, 그 차이는 스냅샷을 다시 지어야만
// 보인다.
inline constexpr int kUISnapshotCameraPass = 1;

class UIRenderCollector {
public:
    void Collect(const UISnapshot&, const UIPhysicalTransform&,
                 molga::RenderQueue&, TextRenderer&,
                 molga::text::TextDiagnosticSink&) const;

    // ── 은퇴한 월드 세대 회수 ──────────────────────────────────────────────
    // 진단 예산은 월드 세대로 칸이 나뉜다. 세대가 은퇴하면 그 칸을 놓아야
    // 한다 — 놓지 않으면 죽은 월드가 남긴 사실들이 상한을 영구히 채우고,
    // 그 순간부터 **살아 있는 월드의 새 사실이 하나도 보고되지 않는다**.
    // UILayoutSystem::OnWorldReleased가 같은 이유로 자기 예산을 비운다.
    //
    // 프로세스 배선(molga::ui::NotifyUIWorldReleased -> 이 함수)은 이
    // 수집기의 소유자를 만드는 **Task 12.3**의 몫이다. 오늘 이 클래스는
    // 소유자가 없고, 소유자 없는 등록은 등록이 아니다.
    void OnWorldReleased(std::uint64_t worldGeneration);

    // ── 관찰 seam ──────────────────────────────────────────────────────────
    // **가장 최근 Collect 하나**가 떨어뜨리고 제출한 레코드 수다. 진단은
    // 상한이 걸려 있어(같은 사실을 프레임마다 다시 내면 로그가 그 하나로
    // 가득 찬다) 로그만으로는 "두 항목이 떨어졌다"와 "스무 항목이 떨어졌다"를
    // 구별할 수 없다. 누계로 두면 이 두 값이 "이 프레임"이 아니라 "이 수집기가
    // 지나온 역사"를 말하게 되고, 그 역사는 프레임 진단의 답이 아니다.
    std::uint64_t DroppedItemCount() const noexcept { return droppedItems_; }
    std::uint64_t SubmittedItemCount() const noexcept { return submittedItems_; }

private:
    // 이 수집기가 이미 낸 사실들. 상한에 닿으면 **새 사실을 더 내지 않는다**
    // (한 번의 요약 진단만 남긴다). 상한에 닿았을 때 그냥 기억을 멈추면
    // 기억되지 않은 사실이 프레임마다 다시 나가므로, 메모리 상한이 그 순간
    // 로그 상한을 없애 버린다 — 상한을 두는 이유가 정확히 사라지는 지점이다.
    static constexpr std::size_t kMaxRememberedFacts = 256;
    struct ReportedFact {
        std::uint64_t worldGeneration = 0;
        std::string key;
    };
    bool NoteFact(std::uint64_t worldGeneration, const std::string& key,
                  molga::text::TextDiagnosticSink&) const;

    mutable std::vector<ReportedFact> reportedFacts_;
    mutable bool factBudgetExhaustedReported_ = false;
    mutable std::uint64_t droppedItems_ = 0;
    mutable std::uint64_t submittedItems_ = 0;
};

} // namespace molga::ui
