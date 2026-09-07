#pragma once

#include "ECS/Components/UIComponent.h"
#include "Common/Types.h"

#include <cstdint>
#include <string>

class Texture;

class UIImage : public UIComponent {
public:
    COMPONENT_TYPE(UIImage)

    // RectTransform과 같은 이유로 표식이 없다: 저작 모양이 바뀐 적이 없어
    // schema 1이 레거시 문서의 모양과 같다.
    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    const std::string& GetTextureGuid() const { return textureGuid_; }
    void SetTextureGuid(std::string value);
    const Color& GetTint() const { return tint_; }
    void SetTint(const Color& value);
    int GetSortingOrder() const { return sortingOrder_; }
    void SetSortingOrder(int value);
    Texture* GetTexture() const { return texture_; }

    void ResolveAssets() override;
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    std::string textureGuid_;
    Color tint_ = Color::White();
    int sortingOrder_ = 0;
    Texture* texture_ = nullptr;
};
