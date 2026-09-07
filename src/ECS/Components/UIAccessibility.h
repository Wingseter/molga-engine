#pragma once

#include "ECS/Components/UIComponent.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// 접근성 트리가 이 요소를 무엇으로 노출하는가. 닫힌 열거이고, 알 수 없는
// 값은 암묵 대체가 아니라 typed 실패다 — 대체하면 보조 기술이 저작자가
// 지정하지 않은 역할로 요소를 읽는다.
enum class UIAccessibilityRole : std::uint8_t {
    None, Panel, Label, Button, TextInput, Image, ScrollView
};

std::string_view ToCanonicalString(UIAccessibilityRole value) noexcept;
std::optional<UIAccessibilityRole> ParseUIAccessibilityRole(const std::string&);
// 열거 순서 그대로의 정규 철자 목록. 에디터 드롭다운과 시험이 같은 표를 본다.
const std::vector<std::string>& AllAccessibilityRoleStrings();

// 요소의 의미 정보만 담는다. 계산된 역할이나 현재 포커스 여부는 여기 없다 —
// 그것들은 스냅샷과 런타임 표의 것이다.
class UIAccessibility : public UIComponent {
public:
    COMPONENT_TYPE(UIAccessibility)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    UIAccessibilityRole Role() const noexcept { return role_; }
    const std::string& Name() const noexcept { return name_; }
    const std::string& Description() const noexcept { return description_; }
    bool Hidden() const noexcept { return hidden_; }

    void SetRole(UIAccessibilityRole value);
    void SetName(std::string value);
    void SetDescription(std::string value);
    void SetHidden(bool value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    UIAccessibilityRole role_ = UIAccessibilityRole::None;
    std::string name_;
    std::string description_;
    bool hidden_ = false;
};
