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
    }
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
    if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() >= 4) {
        tint_ = {j["tint"][0].get<float>(), j["tint"][1].get<float>(),
                 j["tint"][2].get<float>(), j["tint"][3].get<float>()};
    }
    sortingOrder_ = j.value("sortingOrder", sortingOrder_);
}
