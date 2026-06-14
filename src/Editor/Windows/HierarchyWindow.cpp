#include "HierarchyWindow.h"
#include "../../ECS/GameObject.h"
#include "../../ECS/Component.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Components/SpriteRenderer.h"
#include "../../ECS/Components/TilemapRenderer.h"
#include "../FontManager.h"
#include "../UIRegistry.h"
#include "Editor/Editor.h"
#include "Editor/Commands/ObjectCommands.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>

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
        CreateEmptyGameObject();
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
            CreateEmptyGameObject();
        }
        if (ImGui::BeginMenu((std::string(Icons::Image) + " Create 2D Object").c_str())) {
            if (ImGui::MenuItem((std::string(Icons::Image) + " Sprite").c_str())) {
                CreateSpriteObject();
            }
            if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Tilemap").c_str())) {
                CreateTilemapObject();
            }
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
    for (auto* comp : obj->GetComponents()) {
        const auto& info = UIRegistry::GetComponentInfo(comp->GetTypeName());
        if (info.icon != Icons::Cog) {  // Use first non-default component icon
            icon = info.icon;
            break;
        }
    }

    // If renaming this object, show input text instead of tree node
    if (isRenaming && renamingObject == obj) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Rename", renameBuffer, sizeof(renameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::RenameObjectCommand>(obj->GetID(), std::string(renameBuffer)));
            isRenaming = false;
            renamingObject = nullptr;
        }
        // Cancel on escape or click elsewhere
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))) {
            isRenaming = false;
            renamingObject = nullptr;
        }
        // Focus the input on first frame
        if (ImGui::IsItemActive() == false) {
            ImGui::SetKeyboardFocusHere(-1);
        }
        return;
    }

    std::string label = std::string(icon) + " " + obj->GetName();
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj->GetID(), flags, "%s", label.c_str());

    if (ImGui::BeginDragDropSource()) {
        unsigned int objId = obj->GetID();
        ImGui::SetDragDropPayload("GAMEOBJECT_ID", &objId, sizeof(unsigned int));
        ImGui::Text("%s", obj->GetName().c_str());
        ImGui::EndDragDropSource();
    }

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
            isRenaming = true;
            renamingObject = obj;
            strncpy(renameBuffer, obj->GetName().c_str(), sizeof(renameBuffer) - 1);
            renameBuffer[sizeof(renameBuffer) - 1] = '\0';
        }
        if (ImGui::MenuItem((std::string(Icons::Cubes) + " Duplicate").c_str())) {
            selectedObject = obj;
            DuplicateSelectedObject();
        }
        ImGui::Separator();
        if (ImGui::MenuItem((std::string(Icons::Trash) + " Delete").c_str())) {
            selectedObject = obj;
            DeleteSelectedObject();
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

void HierarchyWindow::CreateEmptyGameObject() {
    auto cmd = std::make_unique<molga::CreateObjectCommand>("New GameObject");
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    selectedObject = Editor::Get().GetSelectedObject();
}

void HierarchyWindow::CreateSpriteObject() {
    auto cmd = std::make_unique<molga::CreateObjectCommand>("Sprite");
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    // 생성된 오브젝트에 SpriteRenderer 부착
    if (GameObject* obj = Editor::Get().GetSelectedObject()) {
        obj->AddComponent<SpriteRenderer>();
        Editor::Get().MarkSceneModified();
    }
    selectedObject = Editor::Get().GetSelectedObject();
}

void HierarchyWindow::CreateTilemapObject() {
    auto cmd = std::make_unique<molga::CreateObjectCommand>("Tilemap");
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    if (GameObject* obj = Editor::Get().GetSelectedObject()) {
        obj->AddComponent<TilemapRenderer>();
        Editor::Get().MarkSceneModified();
    }
    selectedObject = Editor::Get().GetSelectedObject();
}

void HierarchyWindow::DeleteSelectedObject() {
    if (!selectedObject) return;
    auto cmd = std::make_unique<molga::DeleteObjectCommand>(selectedObject);
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    selectedObject = Editor::Get().GetSelectedObject();  // Command가 nullptr로 비웠음
    if (onSelectionChanged) onSelectionChanged(selectedObject);
}

void HierarchyWindow::DuplicateSelectedObject() {
    if (!gameObjects || !selectedObject) return;
    auto cmd = std::make_unique<molga::DuplicateObjectCommand>(selectedObject);
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    selectedObject = Editor::Get().GetSelectedObject();
    if (onSelectionChanged) {
        onSelectionChanged(selectedObject);
    }
}
