#include "HierarchyWindow.h"
#include "../../ECS/GameObject.h"
#include <imgui.h>

HierarchyWindow::HierarchyWindow()
    : EditorWindow("Hierarchy") {
}

void HierarchyWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::Begin(title.c_str(), &isOpen);

    // Add object button
    if (ImGui::Button("+ Add GameObject")) {
        // TODO: Add new GameObject to scene
    }

    ImGui::Separator();

    // Draw scene tree
    if (gameObjects) {
        for (auto& obj : *gameObjects) {
            // Only draw root objects (no parent)
            if (obj && !obj->GetParent()) {
                DrawGameObjectNode(obj.get());
            }
        }
    }

    // Right-click context menu in empty space
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty")) {
            // TODO: Create new empty GameObject
        }
        if (ImGui::BeginMenu("Create 2D Object")) {
            if (ImGui::MenuItem("Sprite")) {}
            if (ImGui::MenuItem("Tilemap")) {}
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

    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj->GetID(), flags, "%s", obj->GetName().c_str());

    // Handle selection
    if (ImGui::IsItemClicked()) {
        selectedObject = obj;
        if (onSelectionChanged) {
            onSelectionChanged(obj);
        }
    }

    // Right-click context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Rename")) {
            // TODO: Rename dialog
        }
        if (ImGui::MenuItem("Duplicate")) {
            // TODO: Duplicate object
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
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
