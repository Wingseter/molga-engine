#include "MarrowRenderer.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "Transform.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Shader.h"
#include "../../Rendering/Texture.h"
#include "../../Core/TextureManager.h"
#include "../../Core/PathService.h"
#include "../../Common/Log.h"

#ifdef MOLGA_EDITOR
#include "../../Editor/Project.h"
#include <imgui.h>
#endif

#include <marrow/runtime/skeleton.hpp>
#include <marrow/runtime/animation_state.hpp>
#include <marrow/runtime/atlas.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

REGISTER_COMPONENT(MarrowRenderer)

MarrowRenderer::~MarrowRenderer() {
    CleanGLBuffers();
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
    if (!gameObject || !enabled || !skeleton || !texture) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    Shader* activeShader = renderer->GetCurrentShader();
    if (!activeShader) return;

    // 1. Bind texture atlas to unit 0
    texture->Bind(0);

    // 2. Set uniform parameters via Shader class API
    float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    activeShader->SetMat4("model", identity);
    activeShader->SetVec4("uColor", color.r, color.g, color.b, color.a);
    activeShader->SetVec4("uUV", 0.0f, 0.0f, 1.0f, 1.0f);
    activeShader->SetBool("useTexture", true);
    activeShader->SetInt("uTexture", 0);

    // Get GameObject's world transform for CPU vertex projection
    Vector2 worldPos = transform->GetWorldPosition();
    Vector2 worldScale = transform->GetWorldScale();
    float worldRotDeg = transform->GetWorldRotation();
    float worldRotRad = worldRotDeg * (3.14159265f / 180.0f);
    float cosRot = std::cos(worldRotRad);
    float sinRot = std::sin(worldRotRad);

    // Setup or bind VAO
    if (VAO == 0) {
        SetupGLBuffers();
    }
    glBindVertexArray(VAO);

    // 3. Draw slots in order
    const auto& drawOrder = skeleton->draw_order();
    for (std::size_t slotIndex : drawOrder) {
        // Evaluate the active attachment pose for the slot
        auto poseOpt = skeleton->evaluate_current_mesh_attachment(slotIndex);
        if (!poseOpt.has_value()) continue;

        const auto& pose = poseOpt.value();
        if (pose.vertices.empty() || pose.triangles.empty()) continue;

        // Clear and reuse vector buffers to avoid heap allocations per frame
        vboData.clear();
        vboData.reserve(pose.vertices.size() * 4);

        for (std::size_t i = 0; i < pose.vertices.size(); ++i) {
            float vx = static_cast<float>(pose.vertices[i].x);
            float vy = static_cast<float>(pose.vertices[i].y);

            // Apply GameObject transform (rotation -> scale -> translation)
            float rx = vx * cosRot - vy * sinRot;
            float ry = vx * sinRot + vy * cosRot;
            float px = rx * worldScale.x + worldPos.x;
            float py = ry * worldScale.y + worldPos.y;

            float u = static_cast<float>(pose.uvs[i * 2]);
            float v = static_cast<float>(pose.uvs[i * 2 + 1]);

            vboData.push_back(px);
            vboData.push_back(py);
            vboData.push_back(u);
            vboData.push_back(v);
        }

        // Convert index triangles to unsigned int (reusing vector)
        indices.clear();
        indices.reserve(pose.triangles.size());
        for (std::size_t idx : pose.triangles) {
            indices.push_back(static_cast<unsigned int>(idx));
        }

        // Upload vertex & index data to dynamic GL buffers
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vboData.size() * sizeof(float), vboData.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_DYNAMIC_DRAW);

        // Draw current slot mesh elements
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
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
    CleanGLBuffers();
}

void MarrowRenderer::SetupGLBuffers() {
    CleanGLBuffers();

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    // Attribute 0: Position (x, y)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Attribute 1: UV coordinates (u, v)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void MarrowRenderer::CleanGLBuffers() {
    if (VAO != 0) {
        glDeleteVertexArrays(1, &VAO);
        VAO = 0;
    }
    if (VBO != 0) {
        glDeleteBuffers(1, &VBO);
        VBO = 0;
    }
    if (EBO != 0) {
        glDeleteBuffers(1, &EBO);
        EBO = 0;
    }
}

void MarrowRenderer::Serialize(nlohmann::json& j) const {
    j["skeletonPath"] = skeletonPath;
    j["atlasPath"] = atlasPath;
    j["sortingOrder"] = sortingOrder;
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
    if (j.contains("skeletonPath")) {
        skeletonPath = j["skeletonPath"];
    }
    if (j.contains("atlasPath")) {
        atlasPath = j["atlasPath"];
    }
    if (j.contains("sortingOrder")) {
        sortingOrder = j["sortingOrder"];
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
