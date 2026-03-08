#include "HierarchyWindow.h"
#include "../../ECS/GameObject.h"
#include "../FontManager.h"
#include <imgui.h>

HierarchyWindow::HierarchyWindow()
    : EditorWindow("Hierarchy") {
}

void HierarchyWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::Begin(title.c_str(), &isOpen);

    // Search bar
    static char searchBuffer[128] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", (std::string(Icons::Folder) + " Search...").c_str(), searchBuffer, IM_ARRAYSIZE(searchBuffer));

    ImGui::Spacing();

    // Add object button with icon
    if (ImGui::Button((std::string(Icons::Plus) + " Add GameObject").c_str())) {
        // TODO: Add new GameObject to scene
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Draw scene tree
    if (gameObjects) {
        for (auto& obj : *gameObjects) {
            // Only draw root objects (no parent)
            if (obj && !obj->GetParent()) {
                // Filter by search
                if (strlen(searchBuffer) == 0 || obj->GetName().find(searchBuffer) != std::string::npos) {
                    DrawGameObjectNode(obj.get());
                }
            }
        }
    }

    // Right-click context menu in empty space
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem((std::string(Icons::Cube) + " Create Empty").c_str())) {
            // TODO: Create new empty GameObject
        }
        if (ImGui::BeginMenu((std::string(Icons::Image) + " Create 2D Object").c_str())) {
            if (ImGui::MenuItem((std::string(Icons::Image) + " Sprite").c_str())) {}
            if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Tilemap").c_str())) {}
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void HierarchyWindow::DrawGameObjectNode(GameObject* obj) {
    if (!obj) return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    // Highlight selected object
    if (obj == selectedObject) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // If no children, show as leaf
    if (obj->GetChildren().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // Determine icon based on components
    const char* icon = Icons::Cube;  // Default icon
    // TODO: Check for specific components and change icon accordingly
    // e.g., if has SpriteRenderer -> Icons::Image
    //       if has Camera -> Icons::Camera

    std::string label = std::string(icon) + " " + obj->GetName();
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj->GetID(), flags, "%s", label.c_str());

    // Handle selection
    if (ImGui::IsItemClicked()) {
        selectedObject = obj;
        if (onSelectionChanged) {
            onSelectionChanged(obj);
        }
    }

    // Right-click context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem((std::string(Icons::Code) + " Rename").c_str())) {
            // TODO: Rename dialog
        }
        if (ImGui::MenuItem((std::string(Icons::Cubes) + " Duplicate").c_str())) {
            // TODO: Duplicate object
        }
        ImGui::Separator();
        if (ImGui::MenuItem((std::string(Icons::Trash) + " Delete").c_str())) {
            // TODO: Delete object
        }
        ImGui::EndPopup();
    }

    // Draw children if node is open
    if (nodeOpen) {
        for (auto* child : obj->GetChildren()) {
            DrawGameObjectNode(child);
        }
        ImGui::TreePop();
    }
}
