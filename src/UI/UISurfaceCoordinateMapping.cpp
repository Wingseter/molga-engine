#include "UI/UISurfaceCoordinateMapping.h"

#include "Systems/Input.h"
#include "Text/TextDiagnostic.h"
#include "UI/UIInputRouter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace molga::ui {

namespace {

constexpr const char* kSubsystem = "ui.frame";

void Report(molga::text::TextDiagnosticSink& sink,
            molga::text::TextDiagnosticCode code, std::string message,
            std::string remediation) {
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = molga::text::TextSeverity::Error;
    diagnostic.subsystem = kSubsystem;
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    sink.Report(std::move(diagnostic));
}

bool PositiveFinite(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

// 논리 해상도 -> 캔버스 FixedRect / 물리(출력) 사각형. 넘치면 nullopt.
std::optional<UIPhysicalTransform> MakeCanvasToOutput(
    const molga::OutputPresentationLayout& presentation,
    std::uint64_t deviceGeneration) noexcept {
    if (!presentation.IsValid()) return std::nullopt;
    const std::int64_t width = presentation.logicalSize.width;
    const std::int64_t height = presentation.logicalSize.height;
    constexpr std::int64_t kMaxRaw = std::numeric_limits<std::int32_t>::max();
    if (width <= 0 || height <= 0 || width * 64 > kMaxRaw ||
        height * 64 > kMaxRaw) {
        return std::nullopt;
    }
    UIPhysicalTransform transform;
    transform.logicalViewport.width =
        molga::Fixed26_6::FromRaw(static_cast<std::int32_t>(width * 64));
    transform.logicalViewport.height =
        molga::Fixed26_6::FromRaw(static_cast<std::int32_t>(height * 64));
    transform.physicalViewport = molga::PixelRectU32{
        0, 0, static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height)};
    transform.deviceGeneration = deviceGeneration;
    return transform;
}

}  // namespace

std::optional<molga::FixedPoint> UISurfaceCoordinateMapping::WindowPixelToCanvas(
    double windowPixelX, double windowPixelY) const noexcept {
    if (!std::isfinite(windowPixelX) || !std::isfinite(windowPixelY)) {
        return std::nullopt;
    }
    if (!PositiveFinite(windowPixelsPerOutputPixelX) ||
        !PositiveFinite(windowPixelsPerOutputPixelY) ||
        !std::isfinite(outputOriginWindowPixelX) ||
        !std::isfinite(outputOriginWindowPixelY)) {
        return std::nullopt;
    }
    if (visibleWindowPixelsValid &&
        (windowPixelX < visibleWindowPixelMinX ||
         windowPixelX >= visibleWindowPixelMaxX ||
         windowPixelY < visibleWindowPixelMinY ||
         windowPixelY >= visibleWindowPixelMaxY)) {
        return std::nullopt;
    }
    const double outputX =
        (windowPixelX - outputOriginWindowPixelX) / windowPixelsPerOutputPixelX;
    const double outputY =
        (windowPixelY - outputOriginWindowPixelY) / windowPixelsPerOutputPixelY;
    return canvasToOutput.ToLogicalPoint(outputX, outputY);
}

void UISurfaceCoordinateMapping::ClipVisibleWindowPixels(
    double minX, double minY, double maxX, double maxY) noexcept {
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) ||
        !std::isfinite(maxY)) {
        // 빈 영역: 반열린 [0,0)에는 점이 없다.
        visibleWindowPixelsValid = true;
        visibleWindowPixelMinX = visibleWindowPixelMinY = 0.0;
        visibleWindowPixelMaxX = visibleWindowPixelMaxY = 0.0;
        return;
    }
    if (!visibleWindowPixelsValid) {
        visibleWindowPixelsValid = true;
        visibleWindowPixelMinX = minX;
        visibleWindowPixelMinY = minY;
        visibleWindowPixelMaxX = maxX;
        visibleWindowPixelMaxY = maxY;
        return;
    }
    visibleWindowPixelMinX = std::max(visibleWindowPixelMinX, minX);
    visibleWindowPixelMinY = std::max(visibleWindowPixelMinY, minY);
    visibleWindowPixelMaxX = std::min(visibleWindowPixelMaxX, maxX);
    visibleWindowPixelMaxY = std::min(visibleWindowPixelMaxY, maxY);
}

std::optional<UISurfaceCoordinateMapping> MakeEmbeddedSurfaceMapping(
    const molga::OutputPresentationLayout& presentation,
    double imageOriginWindowPointX, double imageOriginWindowPointY,
    double windowPixelsPerPointX, double windowPixelsPerPointY,
    double physicalPixelsPerTexel, std::uint64_t deviceGeneration) noexcept {
    if (!std::isfinite(imageOriginWindowPointX) ||
        !std::isfinite(imageOriginWindowPointY) ||
        !PositiveFinite(windowPixelsPerPointX) ||
        !PositiveFinite(windowPixelsPerPointY) ||
        !PositiveFinite(physicalPixelsPerTexel)) {
        return std::nullopt;
    }
    const auto transform = MakeCanvasToOutput(presentation, deviceGeneration);
    if (!transform) return std::nullopt;

    // 이미지 원점의 창 물리 픽셀. 텍셀 하나 = physicalPixelsPerTexel 창 픽셀.
    const double imageOriginX = imageOriginWindowPointX * windowPixelsPerPointX;
    const double imageOriginY = imageOriginWindowPointY * windowPixelsPerPointY;
    UISurfaceCoordinateMapping mapping;
    mapping.canvasToOutput = *transform;
    mapping.outputOriginWindowPixelX =
        imageOriginX + presentation.contentRect.x * physicalPixelsPerTexel;
    mapping.outputOriginWindowPixelY =
        imageOriginY + presentation.contentRect.y * physicalPixelsPerTexel;
    // 논리 픽셀 하나 = presentation.scale 텍셀.
    mapping.windowPixelsPerOutputPixelX =
        physicalPixelsPerTexel * static_cast<double>(presentation.scale);
    mapping.windowPixelsPerOutputPixelY = mapping.windowPixelsPerOutputPixelX;
    mapping.visibleWindowPixelsValid = true;
    mapping.visibleWindowPixelMinX = imageOriginX;
    mapping.visibleWindowPixelMinY = imageOriginY;
    mapping.visibleWindowPixelMaxX =
        imageOriginX + presentation.framebufferSize.width * physicalPixelsPerTexel;
    mapping.visibleWindowPixelMaxY =
        imageOriginY + presentation.framebufferSize.height * physicalPixelsPerTexel;
    if (!std::isfinite(mapping.outputOriginWindowPixelX) ||
        !std::isfinite(mapping.outputOriginWindowPixelY) ||
        !std::isfinite(mapping.visibleWindowPixelMaxX) ||
        !std::isfinite(mapping.visibleWindowPixelMaxY)) {
        return std::nullopt;
    }
    return mapping;
}

std::optional<UISurfaceCoordinateMapping> MakeStandaloneSurfaceMapping(
    const molga::OutputPresentationLayout& presentation,
    std::uint64_t deviceGeneration) noexcept {
    return MakeEmbeddedSurfaceMapping(presentation, 0.0, 0.0, 1.0, 1.0, 1.0,
                                      deviceGeneration);
}

void DropUnfocusedPanelInput(UIFrameInput& input) noexcept {
    input.orderedEvents.clear();
    input.pointerAtBatchStart = {};
    input.pointerAtBatchStartValid = false;
    input.nativeWindowFocusedAtBatchStart = false;
}

bool KeepFocusingPressSuffix(UIFrameInput& input, molga::WindowId nativeWindow) noexcept {
    const auto left = static_cast<std::uint32_t>(Input::MouseButton::Left);
    const auto first = std::find_if(
        input.orderedEvents.begin(), input.orderedEvents.end(),
        [&](const UIInputEvent& event) {
            return event.kind == UIInputEventKind::PointerButton &&
                   event.control == left && event.active &&
                   event.windowId == nativeWindow && event.logicalPointValid;
        });
    if (first == input.orderedEvents.end()) {
        DropUnfocusedPanelInput(input);
        return false;
    }
    input.orderedEvents.erase(input.orderedEvents.begin(), first);
    return true;
}

bool PointerDepartureNeededOnSurfaceChange(
    molga::WindowId previousWindow, std::uint64_t previousWorldGeneration,
    molga::WindowId nextWindow, std::uint64_t nextWorldGeneration) noexcept {
    if (previousWindow == 0 || previousWorldGeneration == 0) return false;
    return previousWindow != nextWindow ||
           previousWorldGeneration != nextWorldGeneration;
}

UIInputEvent MakeSyntheticPrimaryButtonEvent(std::uint64_t sequence,
                                             molga::WindowId windowId,
                                             molga::FixedPoint canvasPoint,
                                             bool pressed) noexcept {
    UIInputEvent event;
    event.sequence = sequence;
    event.traceOrdinal = 0;
    event.windowId = windowId;
    event.kind = UIInputEventKind::PointerButton;
    event.control = static_cast<std::uint32_t>(Input::MouseButton::Left);
    event.active = pressed;
    event.logicalPoint = canvasPoint;
    event.logicalPointValid = true;
    return event;
}

std::optional<molga::FixedPoint> SmokeClickPointForObject(
    const UISnapshot& snapshot, unsigned int objectId) noexcept {
    for (const UIHitTargetSnapshot& hit : snapshot.hitTargets) {
        if (hit.target.objectId != objectId || !hit.interactable) continue;
        std::int64_t minX = hit.logicalRect.x.Raw();
        std::int64_t minY = hit.logicalRect.y.Raw();
        std::int64_t maxX = minX + hit.logicalRect.width.Raw();
        std::int64_t maxY = minY + hit.logicalRect.height.Raw();
        if (hit.logicalClip) {
            const std::int64_t clipMinX = hit.logicalClip->x.Raw();
            const std::int64_t clipMinY = hit.logicalClip->y.Raw();
            minX = std::max(minX, clipMinX);
            minY = std::max(minY, clipMinY);
            maxX = std::min(maxX, clipMinX + hit.logicalClip->width.Raw());
            maxY = std::min(maxY, clipMinY + hit.logicalClip->height.Raw());
        }
        if (maxX <= minX || maxY <= minY) continue;
        // 반열린 구간 [min, max)의 중심. 정수 raw 중간값은 항상 구간 안이다.
        const std::int64_t centerX = minX + (maxX - minX) / 2;
        const std::int64_t centerY = minY + (maxY - minY) / 2;
        constexpr std::int64_t kRawMin = std::numeric_limits<std::int32_t>::min();
        constexpr std::int64_t kRawMax = std::numeric_limits<std::int32_t>::max();
        if (centerX < kRawMin || centerX > kRawMax || centerY < kRawMin ||
            centerY > kRawMax) {
            continue;
        }
        const molga::FixedPoint point{
            molga::Fixed26_6::FromRaw(static_cast<std::int32_t>(centerX)),
            molga::Fixed26_6::FromRaw(static_cast<std::int32_t>(centerY))};
        const UIHitTargetSnapshot* top = TopmostHitRecordAt(snapshot, point);
        if (top != nullptr && top->target.objectId == objectId) return point;
    }
    return std::nullopt;
}

std::optional<UIFrameInput> MapNativeInputBatch(
    const molga::platform::NativeInputBatch& batch,
    molga::WindowId surfaceWindowId, const UISurfaceCoordinateMapping& mapping,
    double windowPixelsPerPointX, double windowPixelsPerPointY,
    molga::text::TextDiagnosticSink& sink) {
    const molga::platform::NativeSurfaceBatchStart* seed =
        batch.FindSurface(surfaceWindowId);
    if (seed == nullptr) {
        Report(sink, molga::text::TextDiagnosticCode::ReferenceInvalid,
               "the native input batch has no unique batch-start seed for "
               "this surface window",
               "take the batch from the same host poll that covers the "
               "surface window");
        return std::nullopt;
    }

    UIFrameInput input;
    input.windowId = surfaceWindowId;
    input.nativeWindowFocusedAtBatchStart = seed->nativeWindowFocused;
    // 점 -> 픽셀 배율이 유한한 양수가 아니면 포인터 좌표를 믿을 수 없다. 진단 한 번,
    // 씨앗과 모든 이벤트의 포인터를 무효로 두고 순서·시퀀스·비포인터 필드는 그대로 옮긴다.
    const bool scaleUsable = PositiveFinite(windowPixelsPerPointX) &&
                             PositiveFinite(windowPixelsPerPointY);
    if (!scaleUsable) {
        Report(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
               "the window pixels-per-point scale is not a finite positive "
               "number",
               "pass the window's pixel density; every pointer in this batch "
               "was left invalid");
    }
    // 씨앗은 이미 창 물리 픽셀이다. 없으면 (0,0)을 지어내지 않는다.
    if (scaleUsable && seed->pointerWindowPixelValid) {
        const auto point = mapping.WindowPixelToCanvas(
            static_cast<double>(seed->pointerWindowPixelX),
            static_cast<double>(seed->pointerWindowPixelY));
        if (point) {
            input.pointerAtBatchStart = *point;
            input.pointerAtBatchStartValid = true;
        }
    }

    bool nonFiniteReported = false;
    const auto reportNonFinite = [&]() {
        if (nonFiniteReported) return;
        nonFiniteReported = true;
        Report(sink, molga::text::TextDiagnosticCode::LayoutInvalid,
               "a native input event carried a non-finite or out-of-range "
               "pointer, scroll or axis value",
               "the value was ignored and its validity cleared; the sequence "
               "audit is unchanged");
    };
    const auto quantize = [&](float value) {
        const auto fixed = molga::Fixed26_6::FromFloat(value);
        if (!fixed) {
            reportNonFinite();
            return molga::Fixed26_6::FromRaw(0);
        }
        return *fixed;
    };

    // UIInputEventKind는 NativeInputEventKind와 열거 순서가 같다.
    using NK = molga::platform::NativeInputEventKind;
    static_assert(static_cast<int>(NK::Other) == static_cast<int>(UIInputEventKind::Other) &&
                  static_cast<int>(NK::WindowFocus) == static_cast<int>(UIInputEventKind::WindowFocus) &&
                  static_cast<int>(NK::PointerMotion) == static_cast<int>(UIInputEventKind::PointerMotion) &&
                  static_cast<int>(NK::PointerButton) == static_cast<int>(UIInputEventKind::PointerButton) &&
                  static_cast<int>(NK::Scroll) == static_cast<int>(UIInputEventKind::Scroll) &&
                  static_cast<int>(NK::Key) == static_cast<int>(UIInputEventKind::Key) &&
                  static_cast<int>(NK::GamepadButton) == static_cast<int>(UIInputEventKind::GamepadButton) &&
                  static_cast<int>(NK::GamepadAxis) == static_cast<int>(UIInputEventKind::GamepadAxis) &&
                  static_cast<int>(NK::TextEditing) == static_cast<int>(UIInputEventKind::TextEditing) &&
                  static_cast<int>(NK::TextCommit) == static_cast<int>(UIInputEventKind::TextCommit),
                  "native and UI event kinds must stay in the same order");
    input.orderedEvents.reserve(batch.events.size());
    for (const molga::platform::NativeInputEvent& native : batch.events) {
        using Kind = molga::platform::NativeInputEventKind;
        UIInputEvent event;
        event.sequence = native.sequence;
        event.traceOrdinal = 0;
        event.windowId = native.windowId;
        event.kind = static_cast<UIInputEventKind>(native.kind);
        event.nativeType = native.nativeType;
        event.control = native.control;
        event.keyModifiers = native.keyModifiers;
        event.active = native.active;

        const bool pointerKind = native.kind == Kind::PointerMotion ||
                                 native.kind == Kind::PointerButton ||
                                 native.kind == Kind::Scroll;
        if (scaleUsable && pointerKind && native.windowId == surfaceWindowId) {
            const double pixelX =
                static_cast<double>(native.x) * windowPixelsPerPointX;
            const double pixelY =
                static_cast<double>(native.y) * windowPixelsPerPointY;
            if (!std::isfinite(pixelX) || !std::isfinite(pixelY)) {
                reportNonFinite();
            } else if (const auto point = mapping.WindowPixelToCanvas(pixelX, pixelY)) {
                event.logicalPoint = *point;
                event.logicalPointValid = true;
            }
            // 유한하지만 content 밖이면 무효(이탈)이고 진단은 없다.
        }
        if (native.kind == Kind::Scroll) {
            // 의미상 SDL 휠 값이다: 한 번만 양자화하고 배율을 곱하지 않는다.
            event.logicalDelta.x = quantize(native.deltaX);
            event.logicalDelta.y = quantize(native.deltaY);
        } else if (native.kind == Kind::GamepadAxis) {
            event.axisValue = quantize(native.axisValue);
        }
        input.orderedEvents.push_back(std::move(event));
    }
    return input;
}

}  // namespace molga::ui
