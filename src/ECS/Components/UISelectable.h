#pragma once

#include "ECS/Components/UIComponent.h"
#include "UI/UINavigationTypes.h"

#include <cstdint>
#include <unordered_map>

// 포커스를 받을 수 있는 요소의 저작 상태. hover/press/focus 같은 런타임 상태는
// 여기 없다 — 그것들은 스냅샷과 런타임 표의 것이고 직렬화되지 않는다.
class UISelectable : public UIComponent {
public:
    COMPONENT_TYPE(UISelectable)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    bool Interactable() const noexcept { return interactable_; }
    UINavigationMode NavigationMode() const noexcept { return navigationMode_; }
    SceneObjectRef NavigateUp() const noexcept { return navigateUp_; }
    SceneObjectRef NavigateDown() const noexcept { return navigateDown_; }
    SceneObjectRef NavigateLeft() const noexcept { return navigateLeft_; }
    SceneObjectRef NavigateRight() const noexcept { return navigateRight_; }

    void SetInteractable(bool value);
    void SetNavigationMode(UINavigationMode value);
    void SetNavigateUp(SceneObjectRef value);
    void SetNavigateDown(SceneObjectRef value);
    void SetNavigateLeft(SceneObjectRef value);
    void SetNavigateRight(SceneObjectRef value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void RemapReferences(
        const std::unordered_map<unsigned int, unsigned int>& idRemap) override;

private:
    bool interactable_ = true;
    UINavigationMode navigationMode_ = UINavigationMode::Auto;
    SceneObjectRef navigateUp_;
    SceneObjectRef navigateDown_;
    SceneObjectRef navigateLeft_;
    SceneObjectRef navigateRight_;
};
