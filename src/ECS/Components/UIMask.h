#pragma once

#include "ECS/Components/UIComponent.h"

#include <cstdint>

// 자손을 자기 사각형으로 자를지 여부. 잘린 영역은 렌더에서만 사라지는 것이
// 아니라 히트 대상에서도 사라지므로, 이 값은 상호작용까지 무효화한다.
// 확정된 clip 사각형은 저작 상태가 아니라 스냅샷의 것이다.
class UIMask : public UIComponent {
public:
    COMPONENT_TYPE(UIMask)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    bool ClipsDescendants() const noexcept { return clipsDescendants_; }
    void SetClipsDescendants(bool value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    bool clipsDescendants_ = true;
};
