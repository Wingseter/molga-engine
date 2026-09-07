#include "ECS/Components/UICanvas.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>
#include <cmath>

REGISTER_COMPONENT(UICanvas)

std::string_view ToCanonicalString(UICanvasScaleMode value) noexcept {
    switch (value) {
        case UICanvasScaleMode::ConstantPixelSize: return "ConstantPixelSize";
        case UICanvasScaleMode::ScaleWithViewport: break;
    }
    return "ScaleWithViewport";
}

std::optional<UICanvasScaleMode> ParseUICanvasScaleMode(
    const std::string& token) {
    if (token == "ConstantPixelSize") return UICanvasScaleMode::ConstantPixelSize;
    if (token == "ScaleWithViewport") return UICanvasScaleMode::ScaleWithViewport;
    return std::nullopt;
}

void UICanvas::SetScaleMode(UICanvasScaleMode value) {
    // ConstantPixelSize는 레거시 형식으로 표현할 방법이 없다(그 형식에는 모드
    // 키 자체가 없었고, 그때의 동작은 언제나 뷰포트에 맞춘 배율이었다).
    // 표식을 그대로 두면 이 값은 직렬화되지 않고 다음 Deserialize가 되돌려,
    // 실행 취소도 다시 실행도 ScaleWithViewport로 떨어진다. UILabel이
    // fontFamilyGuid에서 이미 푼 것과 같은 문제다 — 저작자가 schema 2 전용
    // 값을 공급하는 것이 이관 행위다.
    if (value == UICanvasScaleMode::ConstantPixelSize) {
        loadedSchema_ = LoadedSchema::Current;
    }
    if (scaleMode_ == value) return;
    scaleMode_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UICanvas::SetReferenceResolution(const Vector2& value) {
    // 유한성은 거절하고 하한만 자른다. 레거시 문서가 이미 1보다 작은 해상도를
    // 담고 있을 수 있고, 그때의 결과(1로 올려 계산)를 바꾸면 화면이 달라진다.
    // NaN/Inf는 어떤 레거시 결과도 만들지 않으므로 그것만 typed 실패다.
    const Vector2 canonical{
        std::max(RequireFiniteUIValue(value.x, "referenceResolution.x"), 1.0f),
        std::max(RequireFiniteUIValue(value.y, "referenceResolution.y"), 1.0f)};
    if (referenceResolution_ == canonical) return;
    referenceResolution_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UICanvas::SetMatchWidthOrHeight(float value) {
    // std::clamp(-0.0f, 0.0f, 1.0f)는 -0.0f를 그대로 돌려준다. 부호 비트가
    // 남으면 논리적으로 같은 저작 상태가 "-0.0"과 "0.0" 두 바이트열로 저장돼
    // 캐시 정체성이 둘로 갈린다.
    const float canonical = NormalizeUISignedZero(std::clamp(
        RequireFiniteUIValue(value, "matchWidthOrHeight"), 0.0f, 1.0f));
    if (matchWidthOrHeight_ == canonical) return;
    matchWidthOrHeight_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UICanvas::SetSortingOrder(int value) {
    if (sortingOrder_ == value) return;
    sortingOrder_ = value;
    // 그리는 순서만 달라진다. 배치는 그대로다.
    Invalidate(UIInvalidation::Visual);
}

float UICanvas::ScaleFactor(const Vector2& viewportSize) const {
    // 고정 픽셀 모드는 뷰포트를 보지 않는다. 논리 단위가 곧 픽셀이다.
    if (scaleMode_ == UICanvasScaleMode::ConstantPixelSize) return 1.0f;
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return 1.0f;
    const float widthScale = viewportSize.x / referenceResolution_.x;
    const float heightScale = viewportSize.y / referenceResolution_.y;
    // Geometric interpolation matches Unity CanvasScaler and remains stable
    // when one dimension is much larger than the other.
    return std::pow(std::max(widthScale, 0.0001f), 1.0f - matchWidthOrHeight_) *
           std::pow(std::max(heightScale, 0.0001f), matchWidthOrHeight_);
}

Vector2 UICanvas::LogicalSize(const Vector2& viewportSize) const {
    const float scale = ScaleFactor(viewportSize);
    return scale > 0.0f ? viewportSize / scale : viewportSize;
}

void UICanvas::Serialize(nlohmann::json& j) const {
    if (loadedSchema_ == LoadedSchema::Legacy) {
        // 아직 이관되지 않은 payload는 읽은 형식 그대로 돌려준다. 여기에 새 키가
        // 하나라도 새면 migration 전 scene 파일이 조용히 다시 쓰인다.
        j["referenceResolution"] = {referenceResolution_.x, referenceResolution_.y};
        j["matchWidthOrHeight"] = matchWidthOrHeight_;
        j["sortingOrder"] = sortingOrder_;
        return;
    }

    j["schemaVersion"] = CurrentSchemaVersion;
    j["scaleMode"] = ToCanonicalString(scaleMode_);
    j["referenceResolution"] = {referenceResolution_.x, referenceResolution_.y};
    j["matchWidthOrHeight"] = matchWidthOrHeight_;
    j["sortingOrder"] = sortingOrder_;
}

void UICanvas::Deserialize(const nlohmann::json& j) {
    const std::uint32_t schemaVersion = ReadUIUInt(j, "schemaVersion", 1u);
    // 표식은 payload에서만 온다. 저작 상태에서 유도하면 같은 파일을 두 번
    // 읽었을 때 서로 다른 형식으로 저장될 수 있다.
    loadedSchema_ = schemaVersion < CurrentSchemaVersion ? LoadedSchema::Legacy
                                                         : LoadedSchema::Current;

    // 모드를 먼저 읽는다. SetScaleMode가 표식을 건드릴 수 있으므로, 표식을
    // 정한 직후에 그 값을 확정해 두어야 아래 두 분기가 서로 다른 표식을 보지
    // 않는다.
    const UICanvasScaleMode scaleMode =
        loadedSchema_ == LoadedSchema::Current
            // 레거시 문서에는 모드 키가 없었고, 그때의 동작은 언제나 뷰포트에
            // 맞춘 배율이었다. schema 2 전용 값을 남겨 두면 표식은 legacy인데
            // 배율은 저작된 모드를 따르는, 저장 형식과 다른 화면이 나온다.
            ? ReadCanonicalEnum(j, "scaleMode",
                                UICanvasScaleMode::ScaleWithViewport,
                                ParseUICanvasScaleMode)
            : UICanvasScaleMode::ScaleWithViewport;

    if (j.contains("referenceResolution") &&
        j["referenceResolution"].is_array() &&
        j["referenceResolution"].size() >= 2) {
        SetReferenceResolution({j["referenceResolution"][0].get<float>(),
                                j["referenceResolution"][1].get<float>()});
    }
    SetMatchWidthOrHeight(ReadUIFloat(j, "matchWidthOrHeight", matchWidthOrHeight_));
    SetSortingOrder(j.value("sortingOrder", sortingOrder_));
    SetScaleMode(scaleMode);
}
