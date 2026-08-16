#include "MarrowRenderer.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "Transform.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Texture.h"
#include "../../Rendering/RenderSystem2D.h"
#include "../../Core/TextureManager.h"
#include "../../Core/PathService.h"
#include "../../Common/Log.h"
#include "Rendering/RenderQueue.h"

#ifdef MOLGA_EDITOR
#include "../../Editor/Project.h"
#include "../../Editor/EditorState.h"
#include <imgui.h>
#endif

#include <cmath>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>

REGISTER_COMPONENT(MarrowRenderer)

#ifndef MOLGA_MARROW_SUPPORT

// Stub implementation when Marrow dependency is disabled
MarrowRenderer::~MarrowRenderer() {}
void MarrowRenderer::PlayAnimation(const std::string&, bool, int) {}
void MarrowRenderer::SetMix(const std::string&, const std::string&, float) {}
void MarrowRenderer::Update(float) {}
void MarrowRenderer::RenderSprite(Renderer*) {}
void MarrowRenderer::ResolveAssets(bool) {}
void MarrowRenderer::OnDestroy() {}
void MarrowRenderer::Serialize(nlohmann::json& json) const {
    molga::SerializeWorldSortSettings(json, GetWorldSortSettings());
}
void MarrowRenderer::Deserialize(const nlohmann::json& json) {
    const molga::WorldSortSettings2D settings =
        molga::DeserializeWorldSortSettings(json);
    sortingLayer = settings.sortingLayer;
    sortingOrder = settings.sortingOrder;
    sortMode = settings.sortMode;
    ySortOffset = settings.ySortOffset;
}
void MarrowRenderer::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "Marrow Support Disabled");
    ImGui::TextWrapped("The Marrow dependency was not found during CMake configure. Please place the Marrow project in the sibling directory and build it first.");
#endif
}

#else

// Full integration when Marrow is available
MarrowRenderer::~MarrowRenderer() {
}

void MarrowRenderer::PlayAnimation(const std::string& animName, bool loop, int track) {
    if (animationState) {
        std::optional<double> mixDuration = std::nullopt;
        if (auto current = animationState->get_current(track)) {
            std::string fromName = current->animation_name;
            auto key = std::make_pair(fromName, animName);
            auto it = customMixDurations.find(key);
            if (it != customMixDurations.end()) {
                mixDuration = static_cast<double>(it->second);
            }
        }
        animationState->set_animation(track, animName, loop, mixDuration);
    } else {
        Log::Warn("MarrowRenderer", "Cannot play animation: AnimationState is null.");
    }
}

void MarrowRenderer::SetMix(const std::string& from, const std::string& to, float duration) {
    customMixDurations[std::make_pair(from, to)] = duration;
}

void MarrowRenderer::Update(float dt) {
    if (!enabled || !skeleton || !animationState) return;

    // 1. Advance animation time & apply to skeleton
    animationState->update(dt);
    animationState->apply(*skeleton);

    // 2. Resolve bone hierarchy transforms
    skeleton->update_world_transforms();
}

void MarrowRenderer::RenderSprite(Renderer* renderer) {
    if (!renderer) return;
    molga::RenderQueue queue;
    CollectRender(queue);
    molga::RenderSystem2D::Get().Render(queue, renderer, nullptr);
}

void MarrowRenderer::ResolveAssets(bool forceReload) {
    // 1. Re-entry guard to prevent redundant asset loading
    if (!forceReload && texture != nullptr) {
        return;
    }

    if (skeletonPath.empty() || atlasPath.empty()) return;

    // 2. Clear old assets if force reloading
    if (forceReload) {
        skeleton.reset();
        animationState.reset();
        skeletonData.reset();
        atlasData.reset();
        texture = nullptr;
    }

    // Resolve absolute paths
    std::string absMskl = PathService::Get().ResolveAsset(skeletonPath);
    std::string absMatl = PathService::Get().ResolveAsset(atlasPath);

    // 3. Load Skeleton Data
    marrow::runtime::SkeletonDataResult msklResult = marrow::runtime::load_skeleton_data(absMskl);
    if (!msklResult) {
        Log::Error("MarrowRenderer", "Failed to load skeleton data: " + absMskl);
        return;
    }
    skeletonData = msklResult.skeleton_data;

    // 4. Load Atlas Data
    marrow::runtime::AtlasDataResult matlResult = marrow::runtime::AtlasLoader::load(absMatl);
    if (!matlResult) {
        Log::Error("MarrowRenderer", "Failed to load atlas data: " + absMatl);
        return;
    }
    atlasData = matlResult.atlas_data;

    // 5. Resolve and load texture atlas image
    std::string imgName = atlasData->info().image;
    std::filesystem::path matlDirectory = std::filesystem::path(absMatl).parent_path();
    std::string absTexturePath = (matlDirectory / imgName).string();

    texture = TextureManager::Get().Load(absTexturePath);
    if (!texture) {
        Log::Error("MarrowRenderer", "Failed to load texture for atlas: " + absTexturePath);
        return;
    }

    // 6. Instantiate runtime instances
    skeleton = std::make_unique<marrow::runtime::Skeleton>(skeletonData);
    animationState = std::make_unique<marrow::runtime::AnimationState>(skeletonData);

    // Initial pose setup
    skeleton->set_to_setup_pose();
    
    // Play default first animation if available
    if (!skeletonData->animations().empty()) {
        std::string defaultAnim = skeletonData->animations()[0].name;
        PlayAnimation(defaultAnim, true);
    }

    Log::Info("MarrowRenderer", "Marrow assets resolved successfully for: " + skeletonPath);
}

void MarrowRenderer::OnDestroy() {
}

void MarrowRenderer::Serialize(nlohmann::json& j) const {
    j["skeletonPath"] = skeletonPath;
    j["atlasPath"] = atlasPath;
    molga::SerializeWorldSortSettings(j, GetWorldSortSettings());
    j["color"] = { color.r, color.g, color.b, color.a };

    // Serialize custom mix durations
    nlohmann::json mixArray = nlohmann::json::array();
    for (const auto& [key, val] : customMixDurations) {
        nlohmann::json mixEntry;
        mixEntry["from"] = key.first;
        mixEntry["to"] = key.second;
        mixEntry["duration"] = val;
        mixArray.push_back(mixEntry);
    }
    j["mixDurations"] = mixArray;
}

void MarrowRenderer::Deserialize(const nlohmann::json& j) {
    const molga::WorldSortSettings2D settings =
        molga::DeserializeWorldSortSettings(j);
    sortingLayer = settings.sortingLayer;
    sortingOrder = settings.sortingOrder;
    sortMode = settings.sortMode;
    ySortOffset = settings.ySortOffset;
    if (j.contains("skeletonPath")) {
        skeletonPath = j["skeletonPath"];
    }
    if (j.contains("atlasPath")) {
        atlasPath = j["atlasPath"];
    }
    if (j.contains("color") && j["color"].is_array()) {
        color = Color(j["color"][0], j["color"][1], j["color"][2], j["color"][3]);
    }
    if (j.contains("mixDurations") && j["mixDurations"].is_array()) {
        customMixDurations.clear();
        for (const auto& mixEntry : j["mixDurations"]) {
            if (mixEntry.contains("from") && mixEntry.contains("to") && mixEntry.contains("duration")) {
                std::string from = mixEntry["from"];
                std::string to = mixEntry["to"];
                float duration = mixEntry["duration"];
                customMixDurations[std::make_pair(from, to)] = duration;
            }
        }
    }
}

void MarrowRenderer::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    ImGui::Text("Marrow Skeletal Animation");
    ImGui::Separator();

    // Skeleton file path input
    char msklBuffer[512];
    strncpy(msklBuffer, skeletonPath.c_str(), sizeof(msklBuffer) - 1);
    msklBuffer[sizeof(msklBuffer) - 1] = '\0';
    ImGui::SetNextItemWidth(-80);
    if (ImGui::InputText("Skeleton (.mskl)", msklBuffer, sizeof(msklBuffer))) {
        skeletonPath = msklBuffer;
    }

    // Atlas file path input
    char matlBuffer[512];
    strncpy(matlBuffer, atlasPath.c_str(), sizeof(matlBuffer) - 1);
    matlBuffer[sizeof(matlBuffer) - 1] = '\0';
    ImGui::SetNextItemWidth(-80);
    if (ImGui::InputText("Atlas (.matl)", matlBuffer, sizeof(matlBuffer))) {
        atlasPath = matlBuffer;
    }

    // Sorting order
    int order = sortingOrder;
    if (ImGui::InputInt("Sorting Order", &order)) {
        sortingOrder = order;
    }

    // Color Tint UI
    float colorArr[4] = { color.r, color.g, color.b, color.a };
    if (ImGui::ColorEdit4("Color Tint", colorArr)) {
        SetColor(Color(colorArr[0], colorArr[1], colorArr[2], colorArr[3]));
    }

    if (ImGui::Button("Reload / Resolve Assets")) {
        ResolveAssets(true);
    }

    ImGui::Spacing();
    
    // Playback state display
    if (animationState) {
        ImGui::Text("Playback State:");
        ImGui::Indent();
        if (auto current = animationState->get_current(0)) {
            ImGui::Text("Playing: %s", current->animation_name.c_str());
            double animTime = current->animation_time();
            double animDur = current->animation_duration();
            ImGui::Text("Time: %.2f / %.2f s", animTime, animDur);
            float progress = animDur > 0.0 ? static_cast<float>(animTime / animDur) : 0.0f;
            ImGui::ProgressBar(progress, ImVec2(-1, 0));
        } else {
            ImGui::Text("No animation currently playing on Track 0");
        }
        ImGui::Unindent();
        ImGui::Spacing();
    }

    // Playback debug interface in editor
    if (skeletonData) {
        ImGui::Text("Animations:");
        ImGui::Indent();
        for (const auto& anim : skeletonData->animations()) {
            if (ImGui::Button(anim.name.c_str())) {
                PlayAnimation(anim.name, true);
            }
            ImGui::SameLine();
            ImGui::Text("(duration: %.2fs)", anim.duration());
        }
        ImGui::Unindent();
    }
#endif
}

#endif

void MarrowRenderer::CollectRender(molga::RenderQueue& queue) {
    if (!gameObject || !enabled) return;
#ifdef MOLGA_MARROW_SUPPORT
    if (!skeleton || !skeletonData || !texture || !texture->IsValid()) return;
    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;
#ifdef MOLGA_EDITOR
    if (EditorState::Get().IsEditMode()) Update(ImGui::GetIO().DeltaTime);
#endif
    float worldY = 0.0f;
    worldY = transform->GetWorldPosition().y;
    const molga::SortKey sortKey =
        molga::MakeWorldSortKey(GetWorldSortSettings(), worldY);
    const Vector2 worldPosition = transform->GetWorldPosition();
    const Vector2 worldScale = transform->GetWorldScale();
    const float radians = transform->GetWorldRotation() * 3.14159265f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const auto& slotStates = skeleton->slot_states();
    const auto& slots = skeletonData->slots();
    const auto boneWorldTransforms = skeleton->bone_world_transforms();
    for (std::size_t slotIndex : skeleton->draw_order()) {
        if (slotIndex >= slotStates.size() || slotIndex >= slots.size()) continue;
        const auto& slot = slotStates[slotIndex];
        const Color tint{static_cast<float>(slot.color.r) * color.r,
                         static_cast<float>(slot.color.g) * color.g,
                         static_cast<float>(slot.color.b) * color.b,
                         static_cast<float>(slot.color.a) * color.a};
        auto vertices = std::make_shared<std::vector<molga::Vertex2D>>();
        auto indices = std::make_shared<std::vector<std::uint32_t>>();
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        const auto appendVertex = [&](double skeletonX, double skeletonY,
                                      double u, double v) {
            const float scaledX = static_cast<float>(skeletonX) * worldScale.x;
            const float scaledY = static_cast<float>(skeletonY) * worldScale.y;
            const float x = scaledX * cosine - scaledY * sine + worldPosition.x;
            const float y = scaledX * sine + scaledY * cosine + worldPosition.y;
            vertices->push_back({x, y, static_cast<float>(u),
                                 static_cast<float>(v), tint.r, tint.g,
                                 tint.b, tint.a});
            minX = std::min(minX, x); minY = std::min(minY, y);
            maxX = std::max(maxX, x); maxY = std::max(maxY, y);
        };

        const auto pose = skeleton->evaluate_current_mesh_attachment(slotIndex);
        if (pose && !pose->vertices.empty() && !pose->triangles.empty() &&
            pose->uvs.size() == pose->vertices.size() * 2U) {
            vertices->reserve(pose->vertices.size());
            for (std::size_t index = 0; index < pose->vertices.size(); ++index) {
                appendVertex(pose->vertices[index].x, pose->vertices[index].y,
                             pose->uvs[index * 2U],
                             pose->uvs[index * 2U + 1U]);
            }
            indices->reserve(pose->triangles.size());
            for (std::size_t index : pose->triangles) {
                if (index >= vertices->size()) {
                    indices->clear();
                    break;
                }
                indices->push_back(static_cast<std::uint32_t>(index));
            }
        } else {
            const marrow::runtime::AttachmentData* attachment =
                skeleton->current_attachment(slotIndex);
            const std::string_view regionName =
                skeleton->current_region_name(slotIndex);
            const marrow::runtime::AtlasRegion* region =
                atlasData->find_region_for_attachment(regionName);
            const auto& slotData = slots[slotIndex];
            if (!attachment || attachment->kind !=
                    marrow::runtime::AttachmentKind::Region ||
                !region || slotData.bone_index >= boneWorldTransforms.size() ||
                atlasData->info().width <= 0.0 ||
                atlasData->info().height <= 0.0) {
                continue;
            }
            const auto bone = boneWorldTransforms[slotData.bone_index];
            const std::array<std::array<double, 2>, 4> corners{{
                {{-region->origin_x, -region->origin_y}},
                {{region->width - region->origin_x, -region->origin_y}},
                {{region->width - region->origin_x,
                  region->height - region->origin_y}},
                {{-region->origin_x, region->height - region->origin_y}},
            }};
            const std::array<std::array<double, 2>, 4> uvs{{
                {{region->x / atlasData->info().width,
                  region->y / atlasData->info().height}},
                {{(region->x + region->width) / atlasData->info().width,
                  region->y / atlasData->info().height}},
                {{(region->x + region->width) / atlasData->info().width,
                  (region->y + region->height) / atlasData->info().height}},
                {{region->x / atlasData->info().width,
                  (region->y + region->height) / atlasData->info().height}},
            }};
            vertices->reserve(4U);
            for (std::size_t index = 0; index < corners.size(); ++index) {
                const double x = bone.a * corners[index][0] +
                                 bone.b * corners[index][1] + bone.world_x;
                const double y = bone.c * corners[index][0] +
                                 bone.d * corners[index][1] + bone.world_y;
                appendVertex(x, y, uvs[index][0], uvs[index][1]);
            }
            indices->assign({0U, 2U, 3U, 0U, 1U, 2U});
        }
        if (vertices->empty() || indices->empty() ||
            indices->size() % 3U != 0U) continue;

        BlendMode blend = BlendMode::Alpha;
        if (slotIndex < slots.size()) {
            switch (slots[slotIndex].blend_mode) {
                case marrow::runtime::BlendMode::Additive:
                    blend = BlendMode::Additive; break;
                case marrow::runtime::BlendMode::Multiply:
                    blend = BlendMode::Multiply; break;
                case marrow::runtime::BlendMode::Screen:
                    blend = BlendMode::Screen; break;
                default: break;
            }
        }
        molga::RenderCommand command;
        command.sortKey = sortKey;
        command.batchKey.shaderName = "batch";
        command.batchKey.texture = texture->Handle();
        command.batchKey.textureSampler = texture->Sampler();
        command.batchKey.textureStableId = texture->StableId();
        command.batchKey.blendMode = blend;
        command.batchKey.isBatchable = false;
        command.geometry = std::move(vertices);
        command.geometryIndices = std::move(indices);
        command.worldBounds = AABB{minX, minY, maxX - minX, maxY - minY};
        queue.Submit(command);
    }
#else
    (void)queue;
#endif
}
