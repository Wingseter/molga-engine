#pragma once

#include "Common/Fixed26_6.h"
#include "Platform/NativeInputEvent.h"
#include "Platform/Window.h"
#include "Rendering/OutputPresentationLayout.h"
#include "UI/UIInputEvent.h"
#include "UI/UILayoutSystem.h"

#include <cstdint>
#include <optional>

namespace molga::text {
class TextDiagnosticSink;
}

namespace molga::ui {

// 창 물리 픽셀 -> 캔버스 논리점. 렌더링이 쓰는 변환과 같은 값을 얼려 쓴다.
//
// 값이다. 포인터 hit-test와 이후 IME 사각형/커서 매핑(Task 14.3)이 **같은
// 값**을 쓴다 — 배율/원점을 따로 다시 계산하면 둘이 한 픽셀씩 어긋난다.
//
// "출력 픽셀"은 논리 프레임버퍼 픽셀이다: 한 출력 픽셀 = 창 물리 픽셀
// windowPixelsPerOutputPixel개. canvasToOutput의 물리 뷰포트는 {0,0,L.w,L.h}
// (논리 해상도)이고 논리 뷰포트는 캔버스 전체다.
struct UISurfaceCoordinateMapping {
    UIPhysicalTransform canvasToOutput;
    // 논리 프레임버퍼 원점이 놓인 창 물리 픽셀(content 원점 포함, 잘림이면 음수 content).
    double outputOriginWindowPixelX = 0.0;
    double outputOriginWindowPixelY = 0.0;
    double windowPixelsPerOutputPixelX = 1.0;
    double windowPixelsPerOutputPixelY = 1.0;
    // 표면이 실제로 보이는 창 물리 픽셀 영역(반열린). 잘린 출력의 content가
    // 보이는 영역 밖에서 다시 나타나지 않게 한다. 유효하지 않으면 제한 없음.
    bool visibleWindowPixelsValid = false;
    double visibleWindowPixelMinX = 0.0;
    double visibleWindowPixelMinY = 0.0;
    double visibleWindowPixelMaxX = 0.0;
    double visibleWindowPixelMaxY = 0.0;

    // 창 물리 픽셀 -> 캔버스 논리점. 반열린 content 밖/보이는 영역 밖/비유한/넘침은
    // nullopt다(클램프 없음).
    std::optional<molga::FixedPoint> WindowPixelToCanvas(
        double windowPixelX, double windowPixelY) const noexcept;

    // 보이는 영역을 주어진 창 물리 픽셀 사각형(반열린)과 교집합한다. 스크롤바가 있는
    // 100% Game View처럼 이미지가 자식 창의 보이는 영역 밖으로 삐져나올 때, 가려진
    // 부분이 포인터를 받지 않게 한다. 비유한 값이거나 교집합이 비면 아무 점도 보이지
    // 않는다(fail-closed).
    void ClipVisibleWindowPixels(double minX, double minY, double maxX,
                                 double maxY) noexcept;
};

// 에디터 Game View 패널(내장 또는 분리 창). 이미지 원점은 창 안 점 좌표이고,
// 창 점 -> 창 물리 픽셀 배율은 Game View 레이아웃의 framebufferScale 하나다.
// physicalPixelsPerTexel은 그 레이아웃의 같은 값이다. 입력이 유효하지 않으면 nullopt.
std::optional<UISurfaceCoordinateMapping> MakeEmbeddedSurfaceMapping(
    const molga::OutputPresentationLayout& presentation,
    double imageOriginWindowPointX, double imageOriginWindowPointY,
    double windowPixelsPerPointX, double windowPixelsPerPointY,
    double physicalPixelsPerTexel, std::uint64_t deviceGeneration) noexcept;

// 독립 실행 런타임: 프레임버퍼가 곧 창 물리 픽셀 격자다.
std::optional<UISurfaceCoordinateMapping> MakeStandaloneSurfaceMapping(
    const molga::OutputPresentationLayout& presentation,
    std::uint64_t deviceGeneration) noexcept;

// 입력 포커스가 없는 패널의 프레임 입력: 이벤트와 포인터 씨앗과 창 포커스 씨앗을 모두
// 버린다(포커스 없는 패널의 이벤트는 UI에 닿지 않는다). frameIndex, windowId,
// logicalViewport, uiTicks는 그대로다 — tick은 계속 흘러 관성이 가라앉는다.
void DropUnfocusedPanelInput(UIFrameInput& input) noexcept;

// 호스트 배치 -> 이 표면의 순서 있는 프레임 입력. 씨앗이 없거나 중복이면
// nullopt + ReferenceInvalid 한 번. frameIndex, logicalViewport, uiTicks는
// 부르는 쪽이 채운다.
//
// 이벤트 점(창 점)에는 창 점 -> 물리 픽셀 배율을 정확히 한 번 곱한다. 씨앗은
// 호스트가 이미 물리 픽셀로 바꾼 값이므로 곱하지 않는다. 게임패드 이벤트의
// 창은 호스트가 도장 찍은 값 그대로다.
std::optional<UIFrameInput> MapNativeInputBatch(
    const molga::platform::NativeInputBatch& batch,
    molga::WindowId surfaceWindowId, const UISurfaceCoordinateMapping& mapping,
    double windowPixelsPerPointX, double windowPixelsPerPointY,
    molga::text::TextDiagnosticSink& sink);

}  // namespace molga::ui
