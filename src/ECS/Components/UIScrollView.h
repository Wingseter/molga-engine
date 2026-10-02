#pragma once

#include "ECS/Components/UIComponent.h"

#include <cstdint>
#include <unordered_map>

// 스크롤 영역의 저작 상태 전부. 런타임 offset과 velocity는 여기 없다 —
// 그것들은 런타임 식별자로 키를 잡는 별도 표에 살고 직렬화되지 않는다.
class UIScrollView : public UIComponent {
public:
    COMPONENT_TYPE(UIScrollView)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    SceneObjectRef Viewport() const noexcept { return viewport_; }
    SceneObjectRef Content() const noexcept { return content_; }
    bool Horizontal() const noexcept { return horizontal_; }
    bool Vertical() const noexcept { return vertical_; }
    UIScrollMovement Movement() const noexcept { return movement_; }
    float Elasticity() const noexcept { return elasticity_; }
    bool Inertia() const noexcept { return inertia_; }
    float DecelerationRate() const noexcept { return decelerationRate_; }
    float ScrollSensitivity() const noexcept { return scrollSensitivity_; }
    float InitialNormalizedX() const noexcept { return initialNormalizedX_; }
    float InitialNormalizedY() const noexcept { return initialNormalizedY_; }

    void SetViewport(SceneObjectRef value);
    void SetContent(SceneObjectRef value);
    void SetHorizontal(bool value);
    void SetVertical(bool value);
    void SetMovement(UIScrollMovement value);
    void SetElasticity(float value);
    void SetInertia(bool value);
    void SetDecelerationRate(float value);
    void SetScrollSensitivity(float value);
    void SetInitialNormalizedX(float value);
    void SetInitialNormalizedY(float value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void RemapReferences(
        const std::unordered_map<unsigned int, unsigned int>& idRemap) override;

private:
    SceneObjectRef viewport_;
    SceneObjectRef content_;
    bool horizontal_ = true;
    bool vertical_ = true;
    UIScrollMovement movement_ = UIScrollMovement::Clamped;
    // Elastic일 때 0 이하이면 되돌아오는 데 걸리는 시간이 0이나 음수가 되어
    // 탄성 계산이 발산한다. 그래서 두 setter가 함께 이 불변식을 지킨다.
    float elasticity_ = 0.125f;
    bool inertia_ = true;
    float decelerationRate_ = 0.875f;
    float scrollSensitivity_ = 1.0f;
    float initialNormalizedX_ = 0.0f;
    float initialNormalizedY_ = 0.0f;
};
