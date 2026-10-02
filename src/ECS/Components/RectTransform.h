#pragma once

#include "ECS/Components/UIComponent.h"
#include "Common/Types.h"

#include <cstdint>

class UICanvas;

class RectTransform : public UIComponent {
public:
    COMPONENT_TYPE(RectTransform)

    // 이 컴포넌트의 저작 모양은 한 번도 바뀐 적이 없다. schema 1이 곧 레거시
    // 문서의 모양이므로, UILabel/UICanvas가 쓰는 LoadedSchema 표식이 여기서는
    // 필요 없다 — 키가 없는 문서의 기본값 1이 이미 현재 형식이다. 표식을 두면
    // 디스크의 모든 문서가 영원히 legacy로 읽혀 키를 끝내 쓰지 못한다.
    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    const Vector2& GetAnchorMin() const { return anchorMin_; }
    const Vector2& GetAnchorMax() const { return anchorMax_; }
    const Vector2& GetPivot() const { return pivot_; }
    const Vector2& GetAnchoredPosition() const { return anchoredPosition_; }
    const Vector2& GetSizeDelta() const { return sizeDelta_; }

    void SetAnchorMin(const Vector2& value);
    void SetAnchorMax(const Vector2& value);
    void SetAnchors(const Vector2& minimum, const Vector2& maximum);
    void SetPivot(const Vector2& value);
    void SetAnchoredPosition(const Vector2& value);
    void SetSizeDelta(const Vector2& value);

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
