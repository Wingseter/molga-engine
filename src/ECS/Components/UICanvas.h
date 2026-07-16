#pragma once

#include "ECS/Component.h"
#include "Common/Types.h"

// Screen-space overlay canvas using Unity-style "Scale With Screen Size".
class UICanvas : public Component {
public:
    COMPONENT_TYPE(UICanvas)

    const Vector2& GetReferenceResolution() const { return referenceResolution_; }
    void SetReferenceResolution(const Vector2& value);

    float GetMatchWidthOrHeight() const { return matchWidthOrHeight_; }
    void SetMatchWidthOrHeight(float value);

    int GetSortingOrder() const { return sortingOrder_; }
    void SetSortingOrder(int value) { sortingOrder_ = value; }

    float ScaleFactor(const Vector2& viewportSize) const;
    Vector2 LogicalSize(const Vector2& viewportSize) const;

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    Vector2 referenceResolution_{800.0f, 600.0f};
    float matchWidthOrHeight_ = 0.5f;
    int sortingOrder_ = 0;
};
