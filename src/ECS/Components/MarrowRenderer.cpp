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
#include <filesystem>
#include <iostream>

REGISTER_COMPONENT(MarrowRenderer)

MarrowRenderer::~MarrowRenderer() {
    CleanGLBuffers();
}

void MarrowRenderer::PlayAnimation(const std::string& animName, bool loop, int track) {
    if (animationState) {
        animationState->set_animation(track, animName, loop);
    } else {
        Log::Warn("MarrowRenderer", "Cannot play animation: AnimationState is null.");
    }
}

void MarrowRenderer::SetMix(const std::string& from, const std::string& to, float duration) {
    // Note: Mix definitions can be added directly to SkeletonData or custom mixed.
    // In Marrow, you can configure mix duration globally or in skeleton data.
    // If you need manual mixing, you can configure it on the skeletonData mix definition.
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

    // Get current shader program ID (renderer is drawing inside Begin/End pass)
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (currentProgram == 0) return;

    // 1. Bind texture atlas to unit 0
    texture->Bind(0);

    // 2. Set default uniform parameters
    // In molga-engine, default shader expects:
    // uniform mat4 model;
    // uniform vec4 uColor;
    // uniform vec4 uUV;
    // uniform bool useTexture;
    
    // We compute world vertices on CPU, so model matrix is identity.
    float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    glUniformMatrix4fv(glGetUniformLocation(currentProgram, "model"), 1, GL_FALSE, identity);
    glUniform4f(glGetUniformLocation(currentProgram, "uColor"), 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform4f(glGetUniformLocation(currentProgram, "uUV"), 0.0f, 0.0f, 1.0f, 1.0f);
    glUniform1i(glGetUniformLocation(currentProgram, "useTexture"), 1);
    glUniform1i(glGetUniformLocation(currentProgram, "uTexture"), 0);

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

        // Construct dynamic vertex buffer containing projected Position and UV coordinates
        // Vertex format: X (float), Y (float), U (float), V (float) -> 4 floats per vertex
        std::vector<float> vboData;
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

        // Convert index triangles from std::size_t to unsigned int
        std::vector<unsigned int> indices;
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

void MarrowRenderer::ResolveAssets() {
    if (skeletonPath.empty() || atlasPath.empty()) return;

    // Resolve absolute paths
    std::string absMskl = PathService::Get().ResolveAsset(skeletonPath);
    std::string absMatl = PathService::Get().ResolveAsset(atlasPath);

    // 1. Load Skeleton Data
    marrow::runtime::SkeletonDataResult msklResult = marrow::runtime::load_skeleton_data(absMskl);
    if (!msklResult) {
        Log::Error("MarrowRenderer", "Failed to load skeleton data: " + absMskl);
        return;
    }
    skeletonData = msklResult.skeleton_data;

    // 2. Load Atlas Data
    marrow::runtime::AtlasDataResult matlResult = marrow::runtime::AtlasLoader::load(absMatl);
    if (!matlResult) {
        Log::Error("MarrowRenderer", "Failed to load atlas data: " + absMatl);
        return;
    }
    atlasData = matlResult.atlas_data;

    // 3. Resolve and load texture atlas image
    std::string imgName = atlasData->info().image;
    std::filesystem::path matlDirectory = std::filesystem::path(absMatl).parent_path();
    std::string absTexturePath = (matlDirectory / imgName).string();

    texture = TextureManager::Get().Load(absTexturePath);
    if (!texture) {
        Log::Error("MarrowRenderer", "Failed to load texture for atlas: " + absTexturePath);
        return;
    }

    // 4. Instantiate runtime instances
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

    if (ImGui::Button("Reload / Resolve Assets")) {
        ResolveAssets();
    }

    ImGui::Spacing();
    
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
