#include "ECS/Components/UIAccessibility.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <iterator>

REGISTER_COMPONENT(UIAccessibility)

namespace {

constexpr std::size_t kRoleCount = 7;
static_assert(static_cast<std::size_t>(UIAccessibilityRole::ScrollView) + 1 ==
                  kRoleCount,
              "every UIAccessibilityRole needs a canonical spelling");

// 열거 순서와 한 줄씩 짝을 이룬다. constexpr 배열이라 첫 호출에 할당하지
// 않는다 — 함수 지역 static std::vector였다면 그 할당 실패가 noexcept 함수
// 밖으로 나가 std::terminate가 된다.
constexpr std::string_view kRoleStrings[kRoleCount] = {
    "None", "Panel", "Label", "Button", "TextInput", "Image", "ScrollView"};

} // namespace

std::string_view ToCanonicalString(UIAccessibilityRole value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < kRoleCount ? kRoleStrings[index] : kRoleStrings[0];
}

std::optional<UIAccessibilityRole> ParseUIAccessibilityRole(
    const std::string& token) {
    for (std::size_t index = 0; index < kRoleCount; ++index) {
        if (kRoleStrings[index] == token) {
            return static_cast<UIAccessibilityRole>(index);
        }
    }
    return std::nullopt;
}

const std::vector<std::string>& AllAccessibilityRoleStrings() {
    // 에디터 드롭다운이 std::string 목록을 그대로 쓴다. 같은 표에서 한 번만
    // 만들어 두어, 철자가 두 곳에 따로 적히는 일이 없게 한다.
    static const std::vector<std::string> names(std::begin(kRoleStrings),
                                                std::end(kRoleStrings));
    return names;
}

void UIAccessibility::SetRole(UIAccessibilityRole value) {
    if (role_ == value) return;
    role_ = value;
    // 의미 정보는 기하를 바꾸지 않는다. 배치/고유 크기 비트를 세우면 역할
    // 하나를 고칠 때마다 레이아웃 전체가 다시 계산된다.
    Invalidate(UIInvalidation::Interaction);
}

void UIAccessibility::SetName(std::string value) {
    if (name_ == value) return;
    name_ = std::move(value);
    Invalidate(UIInvalidation::Interaction);
}

void UIAccessibility::SetDescription(std::string value) {
    if (description_ == value) return;
    description_ = std::move(value);
    Invalidate(UIInvalidation::Interaction);
}

void UIAccessibility::SetHidden(bool value) {
    if (hidden_ == value) return;
    hidden_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UIAccessibility::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["role"] = ToCanonicalString(role_);
    j["name"] = name_;
    j["description"] = description_;
    j["hidden"] = hidden_;
}

void UIAccessibility::Deserialize(const nlohmann::json& j) {
    // 먼저 전부 읽고, 하나도 거부되지 않았을 때에만 저장한다. 읽는 도중에
    // 저장하면 세 번째 키에서 거부됐을 때 앞의 둘만 바뀐 반쪽 상태가 남는다 —
    // 실행 취소와 prefab override는 살아 있는 컴포넌트에 이 함수를 부른다.
    //
    // role 키가 없는 승인된 레거시 payload는 None이 된다. 형제 컴포넌트를 보고
    // 역할을 짐작하지 않는다 — 짐작한 역할은 저작자가 검토한 적이 없다.
    const auto role = ReadCanonicalEnum(j, "role", UIAccessibilityRole::None,
                                        ParseUIAccessibilityRole);
    auto name = ReadUIString(j, "name", std::string{});
    auto description = ReadUIString(j, "description", std::string{});
    const bool hidden = ReadUIBool(j, "hidden", false);

    SetRole(role);
    SetName(std::move(name));
    SetDescription(std::move(description));
    SetHidden(hidden);
}
