#pragma once

#include "ECS/Component.h"
#include "Common/Types.h"

#include <string>

class Texture;

class UIImage : public Component {
public:
    COMPONENT_TYPE(UIImage)

    const std::string& GetTextureGuid() const { return textureGuid_; }
    void SetTextureGuid(std::string value);
    const Color& GetTint() const { return tint_; }
    void SetTint(const Color& value) { tint_ = value; }
    int GetSortingOrder() const { return sortingOrder_; }
    void SetSortingOrder(int value) { sortingOrder_ = value; }
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
