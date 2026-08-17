#pragma once

#include "Common/Types.h"
#include "ECS/Component.h"

#include <cstdint>

class PointLight2D final : public Component {
public:
    COMPONENT_TYPE(PointLight2D)

    static constexpr float MinIntensity = 0.0f;
    static constexpr float MaxIntensity = 32.0f;
    static constexpr float MinRadius = 0.01f;
    static constexpr float MaxRadius = 1'000'000.0f;
    static constexpr float MinFalloff = 0.1f;
    static constexpr float MaxFalloff = 8.0f;

    const Color& GetColor() const noexcept { return color_; }
    bool SetColor(const Color& color) noexcept;

    float GetIntensity() const noexcept { return intensity_; }
    bool SetIntensity(float intensity) noexcept;

    float GetRadius() const noexcept { return radius_; }
    bool SetRadius(float radius) noexcept;

    float GetHeight() const noexcept { return height_; }
    bool SetHeight(float height) noexcept;

    float GetFalloff() const noexcept { return falloff_; }
    bool SetFalloff(float falloff) noexcept;

    std::uint32_t GetAffectMask() const noexcept { return affectMask_; }
    void SetAffectMask(std::uint32_t mask) noexcept { affectMask_ = mask; }

    bool CastsShadows() const noexcept { return castsShadows_; }
    void SetCastsShadows(bool casts) noexcept { castsShadows_ = casts; }

    int GetPriority() const noexcept { return priority_; }
    void SetPriority(int priority) noexcept { priority_ = priority; }

    Vector2 GetWorldPosition() const noexcept;

    void Serialize(nlohmann::json& json) const override;
    void Deserialize(const nlohmann::json& json) override;

private:
    Color color_ = Color::White();
    float intensity_ = 1.0f;
    float radius_ = 128.0f;
    float height_ = 32.0f;
    float falloff_ = 2.0f;
    std::uint32_t affectMask_ = 0xFFFFFFFFu;
    bool castsShadows_ = false;
    int priority_ = 0;
};
