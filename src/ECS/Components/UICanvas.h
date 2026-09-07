#pragma once

#include "ECS/Components/UIComponent.h"
#include "Common/Types.h"

#include <cstdint>

// 캔버스가 논리 크기를 정하는 방식. ScaleWithViewport는 8.2 이전부터 있던
// 유일한 동작이므로 기본값이자 레거시 문서의 값이고, ConstantPixelSize는 이
// 스키마에서 새로 저작할 수 있게 된 값이다.
enum class UICanvasScaleMode : std::uint8_t { ConstantPixelSize, ScaleWithViewport };

std::string_view ToCanonicalString(UICanvasScaleMode value) noexcept;
std::optional<UICanvasScaleMode> ParseUICanvasScaleMode(const std::string&);

// Screen-space overlay canvas using Unity-style "Scale With Screen Size".
class UICanvas : public UIComponent {
public:
    COMPONENT_TYPE(UICanvas)

    static constexpr std::uint32_t CurrentSchemaVersion = 2;

    UICanvasScaleMode GetScaleMode() const { return scaleMode_; }
    void SetScaleMode(UICanvasScaleMode value);

    const Vector2& GetReferenceResolution() const { return referenceResolution_; }
    void SetReferenceResolution(const Vector2& value);

    float GetMatchWidthOrHeight() const { return matchWidthOrHeight_; }
    void SetMatchWidthOrHeight(float value);

    int GetSortingOrder() const { return sortingOrder_; }
    void SetSortingOrder(int value);

    // 이 컴포넌트가 어느 형식에서 읽혔는가. 런타임 전용 표식이라 직렬화되지
    // 않으며, 저장 모양만 결정한다.
    LoadedSchema GetLoadedSchema() const { return loadedSchema_; }

    float ScaleFactor(const Vector2& viewportSize) const;
    Vector2 LogicalSize(const Vector2& viewportSize) const;

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    UICanvasScaleMode scaleMode_ = UICanvasScaleMode::ScaleWithViewport;
    Vector2 referenceResolution_{800.0f, 600.0f};
    float matchWidthOrHeight_ = 0.5f;
    int sortingOrder_ = 0;
    LoadedSchema loadedSchema_ = LoadedSchema::Current;
};
