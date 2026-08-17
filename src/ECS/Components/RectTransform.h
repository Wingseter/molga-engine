#pragma once

#include "ECS/Component.h"
#include "Common/Types.h"

class UICanvas;

class RectTransform : public Component {
public:
    COMPONENT_TYPE(RectTransform)

    const Vector2& GetAnchorMin() const { return anchorMin_; }
    const Vector2& GetAnchorMax() const { return anchorMax_; }
    const Vector2& GetPivot() const { return pivot_; }
    const Vector2& GetAnchoredPosition() const { return anchoredPosition_; }
    const Vector2& GetSizeDelta() const { return sizeDelta_; }

    void SetAnchorMin(const Vector2& value);
    void SetAnchorMax(const Vector2& value);
    void SetAnchors(const Vector2& minimum, const Vector2& maximum);
    void SetPivot(const Vector2& value);
    void SetAnchoredPosition(const Vector2& value) { anchoredPosition_ = value; }
    void SetSizeDelta(const Vector2& value) { sizeDelta_ = value; }

    // Resolves a logical rectangle from a parent logical rectangle.
    AABB ResolveIn(const AABB& parentRect) const;
    // Resolves to framebuffer pixel coordinates using the nearest Canvas.
    AABB GetScreenRect(const Vector2& viewportSize) const;
    const UICanvas* FindCanvas() const;

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    AABB ResolveLogical(const Vector2& viewportSize) const;

    Vector2 anchorMin_{0.5f, 0.5f};
    Vector2 anchorMax_{0.5f, 0.5f};
    Vector2 pivot_{0.5f, 0.5f};
    Vector2 anchoredPosition_{0.0f, 0.0f};
    Vector2 sizeDelta_{100.0f, 100.0f};
};
