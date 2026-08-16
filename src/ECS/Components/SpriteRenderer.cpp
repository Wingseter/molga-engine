#include "SpriteRenderer.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "Core/AssetDatabase.h"
#include "Core/SpriteResolver.h"
#include "Core/TextureImportSettings.h"

REGISTER_COMPONENT(SpriteRenderer)
#include "../../Rendering/Renderer.h"
#include <glad/glad.h>
#include "../../Rendering/Shader.h"
#include "../../Rendering/Camera2D.h"
#include "../../Rendering/Sprite.h"
#include "../../Rendering/Texture.h"
#include "../../Core/TextureManager.h"
#include "../../Core/PathService.h"
#include "../../Common/Log.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/WorldRenderTraversal.h"
#ifdef MOLGA_EDITOR
#include "../../Editor/Project.h"
#endif
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

namespace {

void WarnInvalidNormalOnce(const std::string& guid,
                           const std::string& failure) {
    static std::unordered_set<std::string> warned;
    const std::string key = guid + '\n' + failure;
    if (!warned.insert(key).second) return;
    Log::Warn("SpriteRenderer",
        "Normal map '" + guid + "' is invalid (" + failure +
        "); using a flat normal");
}

} // namespace

void SpriteRenderer::SetTexture(Texture* tex) {
    texture = tex;
}

void SpriteRenderer::SetLightingMode(SpriteLightingMode2D mode) {
    lightingMode_ = mode == SpriteLightingMode2D::Lit
        ? SpriteLightingMode2D::Lit : SpriteLightingMode2D::Unlit;
    if (lightingMode_ == SpriteLightingMode2D::Lit) {
        normalResolveAttempted_ = false;
    }
}

void SpriteRenderer::SetNormalMapGuid(const std::string& guid) {
    if (normalMapGuid_ == guid) return;
    normalMapGuid_ = guid;
    InvalidateNormalResolution();
}

bool SpriteRenderer::SetNormalStrength(float strength) {
    if (!std::isfinite(strength)) return false;
    normalStrength_ = std::clamp(strength, 0.0f, 2.0f);
    return true;
}

void SpriteRenderer::InvalidateNormalResolution() {
    normalTexture_ = nullptr;
    normalResolveAttempted_ = false;
    normalWarningEmitted_ = false;
}

void SpriteRenderer::EnsureNormalResolution() {
    if (lightingMode_ != SpriteLightingMode2D::Lit) return;
    normalResolveAttempted_ = true;
    if (normalMapGuid_.empty()) {
        normalTexture_ = nullptr;
        WarnInvalidNormalOnce(
            normalMapGuid_, "missing normalMapGuid reference");
        normalWarningEmitted_ = true;
        return;
    }

    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    const molga::AssetRecord* record = database.Find(normalMapGuid_);
    std::string failure;
    if (!record) {
        failure = "missing asset";
    } else if (record->importer != "TextureImporter") {
        failure = "wrong importer";
    } else if (record->importFailed) {
        failure = "import failed";
    } else {
        const molga::TextureImportSettings settings =
            molga::DeserializeTextureImportSettings(record->settings, true);
        if (settings.usage != molga::TextureUsage::NormalMap) {
            failure = "texture usage is not NormalMap";
        } else if (record->textureWidth <= 0 || record->textureHeight <= 0) {
            failure = "invalid texture dimensions";
        } else if (!normalTexture_) {
            const std::filesystem::path source =
                database.AbsoluteSourcePath(normalMapGuid_);
            if (source.empty()) {
                failure = "missing source";
            } else {
                normalTexture_ = TextureManager::Get().LoadWithSettings(
                    source.string(), settings, "SpriteRenderer.NormalMap");
                if (!normalTexture_) failure = "texture load failed";
            }
        }
    }

    if (!failure.empty()) {
        normalTexture_ = nullptr;
        WarnInvalidNormalOnce(normalMapGuid_, failure);
        normalWarningEmitted_ = true;
    }
}

bool SpriteRenderer::HasUsableNormalTexture() {
    if (lightingMode_ != SpriteLightingMode2D::Lit) return false;
    EnsureNormalResolution();
    if (!normalTexture_) return false;

    const VisualSprite visual = GetVisualSprite();
    Texture* effectiveDiffuse =
        material.mainTexture ? material.mainTexture : visual.texture;
    if (!effectiveDiffuse ||
        effectiveDiffuse->GetWidth() != normalTexture_->GetWidth() ||
        effectiveDiffuse->GetHeight() != normalTexture_->GetHeight()) {
        WarnInvalidNormalOnce(
            normalMapGuid_, "size does not match the effective diffuse texture");
        normalWarningEmitted_ = true;
        return false;
    }
    return true;
}

Texture* SpriteRenderer::GetNormalTexture() {
    return HasUsableNormalTexture() ? normalTexture_ : nullptr;
}

void SpriteRenderer::SetTexturePath(const std::string& path) {
    if (texturePath == path) return;
    texturePath = path;
    texture = nullptr;
    if (authoredSprite.textureGuid.empty() && !texturePath.empty()) {
        const std::string guid =
            molga::AssetDatabase::Get().GuidForSource(texturePath);
        if (!guid.empty()) authoredSprite.textureGuid = guid;
    }
    InvalidateAuthoredResolution();
}

void SpriteRenderer::SetTextureGuid(const std::string& guid) {
    if (authoredSprite.textureGuid == guid && authoredSprite.sliceId.empty()) return;
    authoredSprite.textureGuid = guid;
    authoredSprite.sliceId.clear();
    texture = nullptr;
    InvalidateAuthoredResolution();
}

void SpriteRenderer::SetSpriteRef(const molga::SpriteRef& value) {
    if (authoredSprite == value) return;
    authoredSprite = value;
    texture = nullptr;
    InvalidateAuthoredResolution();
}

void SpriteRenderer::SetRuntimeSpriteOverride(const molga::SpriteRef& value) {
    if (value.Empty()) {
        ClearRuntimeSpriteOverride();
        return;
    }
    if (hasRuntimeSpriteOverride && runtimeSpriteOverride == value) return;
    runtimeSpriteOverride = value;
    hasRuntimeSpriteOverride = true;
    InvalidateRuntimeResolution();
}

void SpriteRenderer::ClearRuntimeSpriteOverride() {
    hasRuntimeSpriteOverride = false;
    runtimeSpriteOverride = {};
    InvalidateRuntimeResolution();
}

void SpriteRenderer::InvalidateAuthoredResolution() {
    authoredResolved = {};
    authoredResolveAttempted = false;
}

void SpriteRenderer::InvalidateRuntimeResolution() {
    runtimeResolved = {};
    runtimeResolveAttempted = false;
}

void SpriteRenderer::EnsureSpriteResolution() {
    if (!authoredResolveAttempted && !authoredSprite.Empty()) {
        authoredResolveAttempted = true;
        authoredResolved = molga::SpriteResolver::Resolve(authoredSprite);
        if (authoredResolved.valid) texture = authoredResolved.texture;
    }
    if (hasRuntimeSpriteOverride && !runtimeResolveAttempted &&
        !runtimeSpriteOverride.Empty()) {
        runtimeResolveAttempted = true;
        runtimeResolved = molga::SpriteResolver::Resolve(runtimeSpriteOverride);
    }
}

SpriteRenderer::VisualSprite SpriteRenderer::GetVisualSprite() {
    EnsureSpriteResolution();
    const molga::ResolvedSprite* resolved = nullptr;
    if (hasRuntimeSpriteOverride && runtimeResolved.valid) {
        resolved = &runtimeResolved;
    } else if (authoredResolved.valid) {
        resolved = &authoredResolved;
    }

    VisualSprite visual;
    if (resolved) {
        visual.texture = resolved->texture;
        visual.uv = resolved->uv;
        visual.pivot = resolved->pivot;
        visual.nativeSize = resolved->nativeSize;
        visual.resolved = true;
    } else {
        visual.texture = texture;
        visual.uv = Frame{};
        visual.pivot = {0.5f, 0.5f};
        if (texture) {
            visual.nativeSize = {
                static_cast<float>(texture->GetWidth()),
                static_cast<float>(texture->GetHeight())
            };
        }
    }
    return visual;
}

Vector2 SpriteRenderer::GetSize() const {
    if (sizeMode == SizeMode::Custom) return {width, height};
    if (hasRuntimeSpriteOverride && runtimeResolved.valid) {
        return runtimeResolved.nativeSize;
    }
    if (authoredResolved.valid) return authoredResolved.nativeSize;
    if (texture) {
        return {static_cast<float>(texture->GetWidth()),
                static_cast<float>(texture->GetHeight())};
    }
    return {width, height};
}

Vector2 SpriteRenderer::GetPivot() const {
    if (sizeMode == SizeMode::Custom) return {0.0f, 0.0f};
    if (hasRuntimeSpriteOverride && runtimeResolved.valid) return runtimeResolved.pivot;
    if (authoredResolved.valid) return authoredResolved.pivot;
    return {0.5f, 0.5f};
}

namespace {

inline void TransformVertex(const mat4x4 model, float localX, float localY,
                            float& outX, float& outY) {
    outX = model[0][0] * localX + model[1][0] * localY + model[3][0];
    outY = model[0][1] * localX + model[1][1] * localY + model[3][1];
}

Vector2 RotateVector(Vector2 value, float degrees) {
    constexpr float Pi = 3.14159265358979323846f;
    const float radians = degrees * Pi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}

void ConfigureSprite(Sprite& sprite, const Transform& transform,
                     const Vector2& localSize, const Vector2& pivot,
                     bool usePivot, const Frame& uv, bool flipX, bool flipY) {
    const Vector2 worldPosition = transform.GetWorldPosition();
    const Vector2 worldScale = transform.GetWorldScale();
    const float rotation = transform.GetWorldRotation();
    const Vector2 size{localSize.x * worldScale.x,
                       localSize.y * worldScale.y};

    Vector2 topLeft = worldPosition;
    if (usePivot) {
        const Vector2 centerOffset{
            (0.5f - pivot.x) * size.x,
            (0.5f - pivot.y) * size.y
        };
        const Vector2 center = worldPosition + RotateVector(centerOffset, rotation);
        topLeft = {center.x - size.x * 0.5f,
                   center.y - size.y * 0.5f};
    }

    sprite.SetPosition(topLeft.x, topLeft.y);
    sprite.SetSize(size.x, size.y);
    sprite.SetRotation(rotation);
    sprite.SetFrame(uv);
    if (flipX) std::swap(sprite.uv[0], sprite.uv[2]);
    if (flipY) std::swap(sprite.uv[1], sprite.uv[3]);
}

AABB CalculateSpriteBounds(Sprite& sprite) {
    mat4x4 model;
    sprite.GetModelMatrix(model);
    float x = 0.0f;
    float y = 0.0f;
    TransformVertex(model, 0.0f, 0.0f, x, y);
    float minX = x;
    float maxX = x;
    float minY = y;
    float maxY = y;
    for (const Vector2 corner : {Vector2{1.0f, 0.0f}, Vector2{1.0f, 1.0f},
                                 Vector2{0.0f, 1.0f}}) {
        TransformVertex(model, corner.x, corner.y, x, y);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    return {minX, minY, maxX - minX, maxY - minY};
}

} // namespace

std::optional<AABB> SpriteRenderer::GetWorldBounds() {
    if (!gameObject || !enabled) return std::nullopt;
    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return std::nullopt;

    const VisualSprite visual = GetVisualSprite();
    Sprite sprite;
    ConfigureSprite(sprite, *transform,
                    sizeMode == SizeMode::Native && visual.nativeSize.x > 0.0f &&
                            visual.nativeSize.y > 0.0f
                        ? visual.nativeSize : Vector2{width, height},
                    visual.pivot, sizeMode == SizeMode::Native,
                    visual.uv, flipX, flipY);
    return CalculateSpriteBounds(sprite);
}

void SpriteRenderer::RenderSprite(Renderer* renderer) {
    if (!gameObject || !enabled) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    VisualSprite visual = GetVisualSprite();
    material.Apply(renderer);
    Sprite sprite;
    ConfigureSprite(sprite, *transform,
                    sizeMode == SizeMode::Native && visual.nativeSize.x > 0.0f &&
                            visual.nativeSize.y > 0.0f
                        ? visual.nativeSize : Vector2{width, height},
                    visual.pivot, sizeMode == SizeMode::Native,
                    visual.uv, flipX, flipY);
    sprite.SetColor(color.r * material.tint.r, color.g * material.tint.g, color.b * material.tint.b, color.a * material.tint.a);

    if (material.mainTexture) {
        sprite.SetTexture(material.mainTexture);
    } else if (visual.texture) {
        sprite.SetTexture(visual.texture);
    }

    renderer->DrawSprite(&sprite);

    // Restore standard alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void SpriteRenderer::CollectRender(molga::RenderQueue& queue) {
    if (!gameObject || !enabled) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    VisualSprite visual = GetVisualSprite();
    molga::RenderCommand cmd;
    
    cmd.sortKey = molga::MakeWorldSortKey(
        GetWorldSortSettings(), transform->GetWorldPosition().y);

    // Batch key setup
    cmd.batchKey = material.GetBatchKey();
    if (!cmd.batchKey.texture) {
        cmd.batchKey.texture = visual.texture;
    }
    if (lightingMode_ == SpriteLightingMode2D::Lit) {
        if (cmd.batchKey.isBatchable) {
            cmd.batchKey.lit = true;
            cmd.batchKey.normalTexture = GetNormalTexture();
            cmd.batchKey.normalStrength = normalStrength_;
            cmd.batchKey.receiverLayer = static_cast<std::uint32_t>(
                molga::NormalizeWorldRenderLayer(gameObject->GetLayer()));
        } else if (!litCustomMaterialWarningEmitted_) {
            Log::Warn(
                "SpriteRenderer",
                "Lit mode is not supported by custom material shaders; "
                "rendering this SpriteRenderer Unlit");
            litCustomMaterialWarningEmitted_ = true;
        }
    }

    if (cmd.batchKey.isBatchable) {
        cmd.isBatchableSprite = true;

        Sprite sprite;
        ConfigureSprite(sprite, *transform,
                        sizeMode == SizeMode::Native && visual.nativeSize.x > 0.0f &&
                                visual.nativeSize.y > 0.0f
                            ? visual.nativeSize : Vector2{width, height},
                        visual.pivot, sizeMode == SizeMode::Native,
                        visual.uv, flipX, flipY);

        mat4x4 model;
        sprite.GetModelMatrix(model);

        // Combined color/tint
        float r = color.r * material.tint.r;
        float g = color.g * material.tint.g;
        float b = color.b * material.tint.b;
        float a = color.a * material.tint.a;

        // Setup vertices (TL, TR, BR, BL)
        TransformVertex(model, 0.0f, 1.0f, cmd.vertices[0].x, cmd.vertices[0].y);
        cmd.vertices[0].u = sprite.uv[0];
        cmd.vertices[0].v = sprite.uv[3];
        cmd.vertices[0].r = r; cmd.vertices[0].g = g; cmd.vertices[0].b = b; cmd.vertices[0].a = a;

        TransformVertex(model, 1.0f, 1.0f, cmd.vertices[1].x, cmd.vertices[1].y);
        cmd.vertices[1].u = sprite.uv[2];
        cmd.vertices[1].v = sprite.uv[3];
        cmd.vertices[1].r = r; cmd.vertices[1].g = g; cmd.vertices[1].b = b; cmd.vertices[1].a = a;

        TransformVertex(model, 1.0f, 0.0f, cmd.vertices[2].x, cmd.vertices[2].y);
        cmd.vertices[2].u = sprite.uv[2];
        cmd.vertices[2].v = sprite.uv[1];
        cmd.vertices[2].r = r; cmd.vertices[2].g = g; cmd.vertices[2].b = b; cmd.vertices[2].a = a;

        TransformVertex(model, 0.0f, 0.0f, cmd.vertices[3].x, cmd.vertices[3].y);
        cmd.vertices[3].u = sprite.uv[0];
        cmd.vertices[3].v = sprite.uv[1];
        cmd.vertices[3].r = r; cmd.vertices[3].g = g; cmd.vertices[3].b = b; cmd.vertices[3].a = a;

        cmd.worldBounds = CalculateSpriteBounds(sprite);
    } else {
        cmd.isBatchableSprite = false;
        cmd.fallbackRender = [this](Renderer* r) {
            this->RenderSprite(r);
        };
    }

    queue.Submit(cmd);
}

void SpriteRenderer::Serialize(nlohmann::json& j) const {
    j["color"] = { color.r, color.g, color.b, color.a };
    j["size"] = { width, height };
    j["flipX"] = flipX;
    j["flipY"] = flipY;
    molga::SerializeWorldSortSettings(j, GetWorldSortSettings());
    j["texturePath"] = texturePath;
    j["textureGuid"] = authoredSprite.textureGuid;
    j["spriteRef"] = molga::SerializeSpriteRef(authoredSprite);
    j["sizeMode"] = sizeMode == SizeMode::Native ? "Native" : "Custom";
    j["lightingMode"] = SpriteLightingMode2DName(lightingMode_);
    j["normalMapGuid"] = normalMapGuid_;
    j["normalStrength"] = normalStrength_;

    nlohmann::json matJson;
    material.Serialize(matJson);
    j["material"] = matJson;
}

nlohmann::json SpriteRenderer::CanonicalizeSerializedData(
    const nlohmann::json& serialized) {
    nlohmann::json canonical = serialized.is_object()
        ? serialized : nlohmann::json::object();
    const auto mode = canonical.find("lightingMode");
    canonical["lightingMode"] =
        mode != canonical.end() && mode->is_string() &&
                mode->get<std::string>() == "Lit"
            ? "Lit" : "Unlit";
    if (!canonical.contains("normalMapGuid") ||
        !canonical["normalMapGuid"].is_string()) {
        canonical["normalMapGuid"] = "";
    }
    float strength = 1.0f;
    const auto authoredStrength = canonical.find("normalStrength");
    if (authoredStrength != canonical.end() && authoredStrength->is_number()) {
        try {
            const float value = authoredStrength->get<float>();
            if (std::isfinite(value)) strength = std::clamp(value, 0.0f, 2.0f);
        } catch (...) {
        }
    }
    canonical["normalStrength"] = strength;
    return canonical;
}

void SpriteRenderer::Deserialize(const nlohmann::json& j) {
    const nlohmann::json canonical = CanonicalizeSerializedData(j);
    lightingMode_ = SpriteLightingMode2DFromString(
        canonical["lightingMode"].get<std::string>());
    normalMapGuid_ = canonical["normalMapGuid"].get<std::string>();
    normalStrength_ = canonical["normalStrength"].get<float>();
    InvalidateNormalResolution();
    ClearRuntimeSpriteOverride();
    const molga::WorldSortSettings2D worldSort =
        molga::DeserializeWorldSortSettings(j);
    sortingLayer = worldSort.sortingLayer;
    sortingOrder = worldSort.sortingOrder;
    sortMode = worldSort.sortMode;
    ySortOffset = worldSort.ySortOffset;
    const bool hasModernSprite = j.contains("spriteRef") && j["spriteRef"].is_object();
    const bool hasModernSizeMode = j.contains("sizeMode") && j["sizeMode"].is_string();
    if (j.contains("color") && j["color"].is_array()) {
        SetColor(j["color"][0], j["color"][1], j["color"][2], j["color"][3]);
    }
    if (j.contains("size") && j["size"].is_array()) {
        width = j["size"][0].get<float>();
        height = j["size"][1].get<float>();
    }
    if (j.contains("flipX")) {
        SetFlipX(j["flipX"]);
    }
    if (j.contains("flipY")) {
        SetFlipY(j["flipY"]);
    }
    if (hasModernSprite) {
        authoredSprite = molga::DeserializeSpriteRef(j["spriteRef"]);
    } else if (j.contains("textureGuid") && j["textureGuid"].is_string()) {
        authoredSprite.textureGuid = j["textureGuid"].get<std::string>();
        authoredSprite.sliceId.clear();
    }
    if (j.contains("texturePath")) {
        std::string p = j["texturePath"].get<std::string>();
        if (p != texturePath) {
            SetTexturePath(p);
            texture = nullptr;
        }
    }
    // 구버전 마이그레이션: guid가 없고 path만 있으면 path를 guid로 승격(메모리에서만).
    if (authoredSprite.textureGuid.empty() && !texturePath.empty()) {
        std::string g = molga::AssetDatabase::Get().GuidForSource(texturePath);
        if (!g.empty()) authoredSprite.textureGuid = g;
    }
    // Scene v1 stored a top-left position plus explicit pixel size. Only the
    // explicit v2 Native mode opts into pivot/PPU placement.
    sizeMode = hasModernSizeMode && j["sizeMode"].get<std::string>() == "Native"
        ? SizeMode::Native : SizeMode::Custom;
    texture = nullptr;
    InvalidateAuthoredResolution();
    if (j.contains("material")) {
        material.Deserialize(j["material"]);
    }
}

void SpriteRenderer::ResolveAssets() {
    authoredResolveAttempted = false;
    runtimeResolveAttempted = false;
    normalResolveAttempted_ = false;
    EnsureSpriteResolution();
    if (!texture) {
        std::filesystem::path src;
        if (!authoredSprite.textureGuid.empty() && authoredSprite.sliceId.empty()) {
            src = molga::AssetDatabase::Get().AbsoluteSourcePath(authoredSprite.textureGuid);
        }
        if (src.empty() && !texturePath.empty()) {
            src = PathService::Get().ResolveAsset(texturePath);  // guid 미해석 시 폴백
        }
        if (!src.empty()) {
            texture = TextureManager::Get().Load(src.string());
        }
        if (!texture) {
            // UX-2 Console로 경고(없으면 stdout). 시각적 placeholder는 missing_texture.
            Log::Warn("SpriteRenderer", "Missing texture for guid '" + authoredSprite.textureGuid +
                      "' (path '" + texturePath + "')");
            texture = TextureManager::Get().Load(
                molga::AssetDatabase::MissingTexturePath().string());
        }
    }
    const bool hasAuthoredMaterialTexture = material.mainTexture != nullptr ||
                                            !material.mainTextureGuid.empty() ||
                                            !material.mainTexturePath.empty();
    material.ResolveAssets();
    if (!hasAuthoredMaterialTexture) material.mainTexture = nullptr;
    EnsureNormalResolution();
}

void SpriteRenderer::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    namespace fs = std::filesystem;

    // Texture section
    ImGui::Text("Texture");
    ImGui::Separator();

    // Show current texture path
    char pathBuffer[512];
    strncpy(pathBuffer, texturePath.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';

    ImGui::SetNextItemWidth(-60);
    if (ImGui::InputText("##texpath", pathBuffer, sizeof(pathBuffer))) {
        SetTexturePath(pathBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        texturePath.clear();
        SetSpriteRef({});
        texture = nullptr;
    }

    // Drop target for texture
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID");
        if (!payload) payload = ImGui::AcceptDragDropPayload("TEXTURE_PATH");
        if (payload) {
            std::string guid;
            std::string path;
            if (std::strcmp(payload->DataType, "ASSET_GUID") == 0) {
                guid = static_cast<const char*>(payload->Data);
                path = molga::AssetDatabase::Get().AbsoluteSourcePath(guid).string();
            } else {
                path = static_cast<const char*>(payload->Data);
                guid = molga::AssetDatabase::Get().GuidForSource(
                    Project::Get().GetRelativePath(path));
            }
            SetTextureGuid(guid);
            texturePath = Project::Get().IsOpen() ? Project::Get().GetRelativePath(path) : path;

            // Load the texture
            texture = TextureManager::Get().Load(path);

        }
        ImGui::EndDragDropTarget();
    }

    // Load button
    if (!texturePath.empty() && !texture) {
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            std::string absPath = texturePath;
            if (Project::Get().IsOpen() && !fs::path(texturePath).is_absolute()) {
                absPath = Project::Get().GetAbsolutePath(texturePath);
            }
            texture = TextureManager::Get().Load(absPath);
        }
    }

    // Show texture info
    if (texture) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Loaded: %dx%d", texture->GetWidth(), texture->GetHeight());

        // Texture preview
        ImGui::Text("Preview:");
        float previewSize = 64.0f;
        float aspect = static_cast<float>(texture->GetWidth()) / static_cast<float>(texture->GetHeight());
        ImVec2 size = aspect > 1.0f ? ImVec2(previewSize, previewSize / aspect) : ImVec2(previewSize * aspect, previewSize);
        ImGui::Image(static_cast<ImTextureID>(texture->GetID()), size);
    } else if (!texturePath.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.3f, 1.0f), "Not loaded");
    }

    ImGui::Spacing();
    ImGui::Text("Transform");
    ImGui::Separator();

    float size[2] = { width, height };
    if (ImGui::DragFloat2("Size", size, 0.5f)) {
        SetSize(size[0], size[1]);
    }

    float colorArr[4] = { color.r, color.g, color.b, color.a };
    if (ImGui::ColorEdit4("Color", colorArr)) {
        SetColor(colorArr[0], colorArr[1], colorArr[2], colorArr[3]);
    }

    bool fx = flipX;
    bool fy = flipY;
    if (ImGui::Checkbox("Flip X", &fx)) {
        SetFlipX(fx);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Flip Y", &fy)) {
        SetFlipY(fy);
    }

    int order = sortingOrder;
    if (ImGui::InputInt("Sorting Order", &order)) {
        SetSortingOrder(order);
    }

    ImGui::Spacing();
    ImGui::Text("Material");
    ImGui::Separator();

    // Shader Name
    char shaderNameBuffer[128];
    strncpy(shaderNameBuffer, material.shaderName.c_str(), sizeof(shaderNameBuffer) - 1);
    shaderNameBuffer[sizeof(shaderNameBuffer)-1] = '\0';
    if (ImGui::InputText("Shader Name", shaderNameBuffer, sizeof(shaderNameBuffer))) {
        material.shaderName = shaderNameBuffer;
    }

    // Blend Mode
    const char* blendModes[] = { "Opaque", "Alpha", "Additive", "Multiply" };
    int currentBlendMode = static_cast<int>(material.blendMode);
    if (ImGui::Combo("Blend Mode", &currentBlendMode, blendModes, 4)) {
        material.blendMode = static_cast<BlendMode>(currentBlendMode);
    }

    // Tint
    float tintArr[4] = { material.tint.r, material.tint.g, material.tint.b, material.tint.a };
    if (ImGui::ColorEdit4("Tint", tintArr)) {
        material.tint = Color(tintArr[0], tintArr[1], tintArr[2], tintArr[3]);
    }

    // Main Texture Path
    char matTexPathBuffer[512];
    strncpy(matTexPathBuffer, material.mainTexturePath.c_str(), sizeof(matTexPathBuffer) - 1);
    matTexPathBuffer[sizeof(matTexPathBuffer)-1] = '\0';
    if (ImGui::InputText("Material Texture", matTexPathBuffer, sizeof(matTexPathBuffer))) {
        material.mainTexturePath = matTexPathBuffer;
        material.mainTexture = nullptr; // force reload on ResolveAssets
    }
    // Drop target for material texture
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID");
        if (!payload) payload = ImGui::AcceptDragDropPayload("TEXTURE_PATH");
        if (payload) {
            std::string guid;
            std::string path;
            if (std::strcmp(payload->DataType, "ASSET_GUID") == 0) {
                guid = static_cast<const char*>(payload->Data);
                path = molga::AssetDatabase::Get().AbsoluteSourcePath(guid).string();
            } else {
                path = static_cast<const char*>(payload->Data);
                guid = molga::AssetDatabase::Get().GuidForSource(
                    Project::Get().GetRelativePath(path));
            }
            material.mainTextureGuid = guid;
            material.mainTexturePath = Project::Get().IsOpen() ? Project::Get().GetRelativePath(path) : path;
            material.mainTexture = TextureManager::Get().Load(path);
        }
        ImGui::EndDragDropTarget();
    }
    
    // Load button for material texture
    if (!material.mainTexturePath.empty() && !material.mainTexture) {
        ImGui::SameLine();
        if (ImGui::Button("Load Mat Tex")) {
            std::string absPath = material.mainTexturePath;
            if (Project::Get().IsOpen() && !fs::path(material.mainTexturePath).is_absolute()) {
                absPath = Project::Get().GetAbsolutePath(material.mainTexturePath);
            }
            material.mainTexture = TextureManager::Get().Load(absPath);
        }
    }

    // Custom properties
    ImGui::Text("Custom Properties");
    ImGui::Indent();
    
    std::string toRemove = "";
    for (auto& [name, prop] : material.properties) {
        ImGui::PushID(name.c_str());
        ImGui::Text("%s:", name.c_str());
        ImGui::SameLine();
        
        // Show type combo or select
        const char* propTypes[] = { "Float", "Vec4", "Texture" };
        int currentType = static_cast<int>(prop.type);
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##type", &currentType, propTypes, 3)) {
            prop.type = static_cast<MaterialProperty::Type>(currentType);
        }
        ImGui::SameLine();

        if (prop.type == MaterialProperty::Type::Float) {
            ImGui::SetNextItemWidth(120);
            ImGui::DragFloat("##val", &prop.floatVal, 0.1f);
        } else if (prop.type == MaterialProperty::Type::Vec4) {
            float v[4] = { prop.vec4Val.x, prop.vec4Val.y, prop.vec4Val.z, prop.vec4Val.w };
            ImGui::SetNextItemWidth(200);
            if (ImGui::DragFloat4("##val", v, 0.1f)) {
                prop.vec4Val = Vector4(v[0], v[1], v[2], v[3]);
            }
        } else if (prop.type == MaterialProperty::Type::Texture) {
            char texPathBuf[256];
            strncpy(texPathBuf, prop.texturePath.c_str(), sizeof(texPathBuf) - 1);
            texPathBuf[sizeof(texPathBuf)-1] = '\0';
            ImGui::SetNextItemWidth(150);
            if (ImGui::InputText("##texpath", texPathBuf, sizeof(texPathBuf))) {
                prop.texturePath = texPathBuf;
                prop.texture = nullptr;
            }
            if (ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID");
                if (!payload) payload = ImGui::AcceptDragDropPayload("TEXTURE_PATH");
                if (payload) {
                    std::string path;
                    if (std::strcmp(payload->DataType, "ASSET_GUID") == 0) {
                        std::string guid = static_cast<const char*>(payload->Data);
                        path = molga::AssetDatabase::Get().AbsoluteSourcePath(guid).string();
                    } else {
                        path = static_cast<const char*>(payload->Data);
                    }
                    std::string relativePath = path;
                    if (Project::Get().IsOpen()) {
                        relativePath = Project::Get().GetRelativePath(path);
                    }
                    prop.texturePath = relativePath;
                    prop.texture = TextureManager::Get().Load(path);
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            toRemove = name;
        }
        ImGui::PopID();
    }

    if (!toRemove.empty()) {
        material.properties.erase(toRemove);
    }

    // Add new property
    static char newPropName[64] = "";
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##newpropname", newPropName, sizeof(newPropName));
    ImGui::SameLine();
    if (ImGui::Button("Add Property")) {
        std::string nameStr(newPropName);
        if (!nameStr.empty() && material.properties.find(nameStr) == material.properties.end()) {
            material.properties[nameStr] = MaterialProperty{};
            newPropName[0] = '\0';
        }
    }
    ImGui::Unindent();
#endif
}
