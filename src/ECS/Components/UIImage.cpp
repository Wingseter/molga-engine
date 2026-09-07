#include "ECS/Components/UIImage.h"

#include "Common/Log.h"
#include "Core/AssetDatabase.h"
#include "Core/TextureManager.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

REGISTER_COMPONENT(UIImage)

void UIImage::SetTextureGuid(std::string value) {
    if (textureGuid_ != value) {
        textureGuid_ = std::move(value);
        texture_ = nullptr;
        Invalidate(UIInvalidation::Visual);
    }
}

void UIImage::SetTint(const Color& value) {
    if (tint_ == value) return;
    tint_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UIImage::SetSortingOrder(int value) {
    if (sortingOrder_ == value) return;
    sortingOrder_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UIImage::ResolveAssets() {
    if (texture_ || textureGuid_.empty()) return;
    const auto path = molga::AssetDatabase::Get().AbsoluteSourcePath(textureGuid_);
    if (!path.empty()) texture_ = TextureManager::Get().Load(path.string());
    if (!texture_) {
        Log::Warn("UIImage", "Missing texture for guid '" + textureGuid_ + "'");
    }
}

void UIImage::Serialize(nlohmann::json& j) const {
    j["textureGuid"] = textureGuid_;
    j["tint"] = {tint_.r, tint_.g, tint_.b, tint_.a};
    j["sortingOrder"] = sortingOrder_;
}

void UIImage::Deserialize(const nlohmann::json& j) {
    SetTextureGuid(j.value("textureGuid", textureGuid_));
    // 세터를 거쳐야 한다. 되돌리기와 prefab override는 살아 있는 컴포넌트에
    // 다시 Deserialize하므로, 직접 대입하면 값만 바뀌고 Visual 무효화가 일어나지
    // 않는다. 위의 textureGuid는 이미 세터를 쓰고 있었다.
    if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() >= 4) {
        SetTint(Color{j["tint"][0].get<float>(), j["tint"][1].get<float>(),
                      j["tint"][2].get<float>(), j["tint"][3].get<float>()});
    }
    SetSortingOrder(j.value("sortingOrder", sortingOrder_));
}
