#include "InspectorWindow.h"
#include "../../ECS/GameObject.h"
#include "../../ECS/Component.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Components/SpriteRenderer.h"
#include "../../ECS/Components/TilemapRenderer.h"
#include "../../ECS/Components/ParticleSystem.h"
#include "../../ECS/Components/BoxCollider2D.h"
#include "../../ECS/Components/MarrowRenderer.h"
#include "../../ECS/Components/AudioSource.h"
#include "../../ECS/Components/AudioListener.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/TextRenderer2D.h"
#include "../../Scripting/Script.h"
#include "../../Scripting/ScriptManager.h"
#include "../../Scripting/BuiltinScripts.h"
#include "../FontManager.h"
#include "../UIRegistry.h"
#include "../../Core/ProjectSettings.h"
#include "../Editor.h"
#include "../EditorConstants.h"
#include "../../ECS/Components/PrefabInstance.h"
#include "../Commands/PrefabCommands.h"
#include <imgui.h>

InspectorWindow::InspectorWindow()
    : EditorWindow("Inspector") {
}

void InspectorWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::Begin(title.c_str(), &isOpen);

    if (!target) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s No object selected", Icons::Cube);
        ImGui::TextDisabled("Select an object from the Hierarchy");
        ImGui::End();
        return;
    }

    // GameObject header with icon
    ImGui::Text("%s", Icons::Cube);
    ImGui::SameLine();

    static char nameBuffer[256];
    strncpy(nameBuffer, target->GetName().c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
        target->SetName(nameBuffer);
    }

    ImGui::Spacing();

    // Tag and Layer dropdowns
    auto& settings = ProjectSettings::Get();
    
    // Tag Combo
    std::string currentTag = target->GetTag();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 8.0f);
    if (ImGui::BeginCombo("##TagCombo", ("Tag: " + currentTag).c_str())) {
        for (const auto& t : settings.tags) {
            bool isSelected = (currentTag == t);
            if (ImGui::Selectable(t.c_str(), isSelected)) {
                target->SetTag(t);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Add Tag...")) {
            Editor::Get().GetWindowManager().SetVisible(EditorConstants::WIN_PROJECT_SETTINGS, true);
        }
        ImGui::EndCombo();
    }
    
    ImGui::SameLine();
    
    // Layer Combo
    int currentLayer = target->GetLayer();
    std::string currentLayerName = settings.GetLayerName(currentLayer);
    if (currentLayerName.empty()) {
        currentLayerName = "Layer " + std::to_string(currentLayer);
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4.0f);
    if (ImGui::BeginCombo("##LayerCombo", ("Layer: " + currentLayerName).c_str())) {
        for (int i = 0; i < 32; ++i) {
            std::string name = settings.GetLayerName(i);
            std::string displayName = name.empty() ? ("Layer " + std::to_string(i) + " (Empty)") : name;
            bool isSelected = (currentLayer == i);
            if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                target->SetLayer(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Edit Layers...")) {
            Editor::Get().GetWindowManager().SetVisible(EditorConstants::WIN_PROJECT_SETTINGS, true);
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    bool active = target->IsActive();
    if (ImGui::Checkbox((std::string(Icons::Eye) + " Active").c_str(), &active)) {
        target->SetActive(active);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Prefab Header Controls
    if (auto* pi = target->GetComponent<PrefabInstance>()) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.28f, 0.48f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.15f, 0.35f, 0.60f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.15f, 0.35f, 0.60f, 1.0f));
        
        bool openPrefabHeader = ImGui::CollapsingHeader((std::string(Icons::Sitemap) + " Prefab Instance").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        
        ImGui::PopStyleColor(3);
        
        if (openPrefabHeader) {
            ImGui::Indent();
            ImGui::TextDisabled("GUID: %s", pi->GetPrefabGuid().c_str());
            
            ImGui::Spacing();
            if (ImGui::Button((std::string(Icons::Save) + " Apply").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ApplyPrefabCommand>(target->GetID()));
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(Icons::Undo) + " Revert").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::RevertPrefabCommand>(target->GetID()));
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(Icons::Times) + " Unpack").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::UnpackPrefabCommand>(target->GetID()));
            }
            
            ImGui::Spacing();
            const auto& mods = pi->GetModifications();
            if (!mods.empty()) {
                ImGui::Text("Overridden Properties (%d):", (int)mods.size());
                for (const auto& mod : mods) {
                    if (mod.contains("component") && mod.contains("key")) {
                        ImGui::BulletText("%s: %s", 
                            mod["component"].get<std::string>().c_str(),
                            mod["key"].get<std::string>().c_str());
                    }
                }
            } else {
                ImGui::TextDisabled("No overrides (follows template)");
            }
            
            ImGui::Unindent();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    // Draw all components using their OnInspectorGUI
    for (auto* comp : target->GetComponents()) {
        DrawComponent(comp);
        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Centered Add component button
    float buttonWidth = 200.0f;
    float windowWidth = ImGui::GetWindowWidth();
    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);

    if (ImGui::Button((std::string(Icons::Plus) + " Add Component").c_str(), ImVec2(buttonWidth, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        ImGui::Text("%s Components", Icons::Cubes);
        ImGui::Separator();

        if (ImGui::MenuItem((std::string(Icons::ArrowsAlt) + " Transform").c_str())) {
            if (!target->HasComponent<Transform>()) {
                target->AddComponent<Transform>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Image) + " Sprite Renderer").c_str())) {
            if (!target->HasComponent<SpriteRenderer>()) {
                target->AddComponent<SpriteRenderer>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Tilemap Renderer").c_str())) {
            if (!target->HasComponent<TilemapRenderer>()) {
                target->AddComponent<TilemapRenderer>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Lightbulb) + " Particle System").c_str())) {
            if (!target->HasComponent<ParticleSystem>()) {
                target->AddComponent<ParticleSystem>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Square) + " Box Collider 2D").c_str())) {
            if (!target->HasComponent<BoxCollider2D>()) {
                target->AddComponent<BoxCollider2D>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Music) + " Audio Source").c_str())) {
            if (!target->HasComponent<AudioSource>()) {
                target->AddComponent<AudioSource>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::VolumeUp) + " Audio Listener").c_str())) {
            if (!target->HasComponent<AudioListener>()) {
                target->AddComponent<AudioListener>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Camera) + " Camera").c_str())) {
            if (!target->HasComponent<Camera>()) {
                target->AddComponent<Camera>();
            }
        }
        if (ImGui::MenuItem((std::string(Icons::ListUl) + " Text Renderer 2D").c_str())) {
            if (!target->HasComponent<TextRenderer2D>()) {
                target->AddComponent<TextRenderer2D>();
            }
        }
#ifdef MOLGA_MARROW_SUPPORT
        if (ImGui::MenuItem((std::string(Icons::Image) + " Marrow Renderer").c_str())) {
            if (!target->HasComponent<MarrowRenderer>()) {
                target->AddComponent<MarrowRenderer>();
            }
        }
#endif
        ImGui::Separator();
        if (ImGui::BeginMenu((std::string(Icons::Code) + " Scripts").c_str())) {
            auto scripts = ScriptManager::Get().GetRegisteredScripts();
            for (const auto& scriptName : scripts) {
                if (ImGui::MenuItem((std::string(Icons::FileCode) + " " + scriptName).c_str())) {
                    auto script = ScriptManager::Get().CreateScript(scriptName);
                    if (script) {
                        target->AddComponentRaw(script.release());
                    }
                }
            }
            if (scripts.empty()) {
                ImGui::TextDisabled("No scripts registered");
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void InspectorWindow::DrawComponent(Component* component) {
    if (!component) return;

    std::string typeName = component->GetTypeName();

    // Check if this component has overrides in PrefabInstance
    bool isOverridden = false;
    if (target) {
        if (auto* pi = target->GetComponent<PrefabInstance>()) {
            for (const auto& mod : pi->GetModifications()) {
                if (mod.contains("component") && mod["component"].get<std::string>() == typeName) {
                    isOverridden = true;
                    break;
                }
            }
        }
    }

    // Determine icon based on component type
    const char* icon = UIRegistry::GetComponentInfo(typeName).icon;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;

    if (isOverridden) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.7f, 1.0f, 1.0f)); // Style color for override highlight
    }

    std::string label = std::string(icon) + " " + typeName + (isOverridden ? " *" : "");
    bool open = ImGui::TreeNodeEx((void*)component, flags, "%s", label.c_str());

    if (isOverridden) {
        ImGui::PopStyleColor();
    }

    // Enable/disable checkbox on the same line
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    bool enabled = component->IsEnabled();
    if (ImGui::Checkbox(("##" + typeName + "Enabled").c_str(), &enabled)) {
        component->SetEnabled(enabled);
    }

    if (open) {
        ImGui::Spacing();
        // Use the component's own OnInspectorGUI method
        component->OnInspectorGUI();
        ImGui::TreePop();
    }
}
