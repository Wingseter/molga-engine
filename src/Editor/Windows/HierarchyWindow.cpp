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
#include "Editor/Commands/PrefabCommands.h"
#include "Editor/Commands/ComponentCommands.h"
#include "../../ECS/Components/PrefabInstance.h"
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
    if (Editor::Get().GetSelection().IsSelected(obj->GetID())) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // If no children, show as leaf
    if (obj->GetChildren().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // Check if this object is a prefab root or child
    bool isPrefabRoot = obj->GetComponent<PrefabInstance>() != nullptr;
    bool isPrefabChild = false;
    if (!isPrefabRoot) {
        GameObject* curr = obj->GetParent();
        while (curr) {
            if (curr->GetComponent<PrefabInstance>()) {
                isPrefabChild = true;
                break;
            }
            curr = curr->GetParent();
        }
    }

    // Determine icon based on components
    const char* icon = Icons::Cube;  // Default icon
    if (isPrefabRoot) {
        icon = Icons::Sitemap;
    } else {
        for (auto* comp : obj->GetComponents()) {
            const auto& info = UIRegistry::GetComponentInfo(comp->GetTypeName());
            if (info.icon != Icons::Cog) {  // Use first non-default component icon
                icon = info.icon;
                break;
            }
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

    if (isPrefabRoot) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.65f, 1.0f, 1.0f)); // Bright blue for root
    } else if (isPrefabChild) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.80f, 1.0f, 1.0f)); // Dim blue for children
    }

    std::string label = std::string(icon) + " " + obj->GetName();
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj->GetID(), flags, "%s", label.c_str());

    if (isPrefabRoot || isPrefabChild) {
        ImGui::PopStyleColor();
    }

    if (ImGui::BeginDragDropSource()) {
        unsigned int objId = obj->GetID();
        ImGui::SetDragDropPayload("GAMEOBJECT_ID", &objId, sizeof(unsigned int));
        ImGui::Text("%s", obj->GetName().c_str());
        ImGui::EndDragDropSource();
    }

    // Handle selection
    if (ImGui::IsItemClicked()) {
        Editor::Get().GetSelection().Select(obj->GetID(), molga::SelectionSource::Hierarchy);
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
            Editor::Get().GetSelection().Select(obj->GetID(), molga::SelectionSource::Hierarchy);
            DuplicateSelectedObject();
        }
        
        ImGui::Separator();
        if (isPrefabRoot) {
            if (ImGui::MenuItem((std::string(Icons::Save) + " Apply Prefab").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ApplyPrefabCommand>(obj->GetID()));
            }
            if (ImGui::MenuItem((std::string(Icons::Undo) + " Revert Prefab").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::RevertPrefabCommand>(obj->GetID()));
            }
            if (ImGui::MenuItem((std::string(Icons::Times) + " Unpack Prefab").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::UnpackPrefabCommand>(obj->GetID()));
            }
        } else if (!isPrefabChild) {
            if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Create Prefab").c_str())) {
                std::string prefabName = obj->GetName() + ".prefab";
                std::filesystem::path relPath = std::filesystem::path("assets") / prefabName;
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::CreatePrefabFromObjectCommand>(obj->GetID(), relPath));
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem((std::string(Icons::Trash) + " Delete").c_str())) {
            Editor::Get().GetSelection().Select(obj->GetID(), molga::SelectionSource::Hierarchy);
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
}

void HierarchyWindow::CreateSpriteObject() {
    auto cmd = std::make_unique<molga::CreateObjectWithComponentsCommand>("Sprite", std::vector<std::string>{"SpriteRenderer"});
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
}

void HierarchyWindow::CreateTilemapObject() {
    auto cmd = std::make_unique<molga::CreateObjectWithComponentsCommand>("Tilemap", std::vector<std::string>{"TilemapRenderer"});
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
}

void HierarchyWindow::DeleteSelectedObject() {
    GameObject* selected = Editor::Get().GetSelectedObject();
    if (!selected) return;
    auto cmd = std::make_unique<molga::DeleteObjectCommand>(selected);
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
}

void HierarchyWindow::DuplicateSelectedObject() {
    GameObject* selected = Editor::Get().GetSelectedObject();
    if (!gameObjects || !selected) return;
    auto cmd = std::make_unique<molga::DuplicateObjectCommand>(selected);
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
}
