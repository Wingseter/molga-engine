#include "InspectorWindow.h"
#include "../../ECS/GameObject.h"
#include "../../ECS/Component.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Components/SpriteRenderer.h"
#include "../../ECS/Components/TilemapRenderer.h"
#include "../../ECS/Components/ParticleSystem.h"
#include "../../ECS/Components/BoxCollider2D.h"
#include "../../ECS/Components/CircleCollider2D.h"
#include "../../ECS/Components/Rigidbody2D.h"
#include "../../ECS/Components/MarrowRenderer.h"
#include "../../ECS/Components/AudioSource.h"
#include "../../ECS/Components/AudioListener.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/TextRenderer2D.h"
#include "../../ECS/Components/UICanvas.h"
#include "../../ECS/Components/RectTransform.h"
#include "../../ECS/Components/UIImage.h"
#include "../../ECS/Components/UILabel.h"
#include "../../ECS/Components/UIButton.h"
#include "../../Scripting/Script.h"
#include "../../Scripting/ScriptManager.h"
#include "../../Scripting/ScriptField.h"
#include "../../Scripting/BuiltinScripts.h"
#include "../../Core/PrefabRegistry.h"
#include "../../Core/AssetDatabase.h"
#include "../../Core/World.h"
#include "../FontManager.h"
#include "../UIRegistry.h"
#include "../../Core/ProjectSettings.h"
#include "../Editor.h"
#include "../EditorConstants.h"
#include "../../ECS/Components/PrefabInstance.h"
#include "../Commands/PrefabCommands.h"
#include "../Commands/SceneSnapshots.h"
#include "../Commands/PropertyCommands.h"
#include "../Commands/ComponentCommands.h"
#include <imgui.h>
#include <cfloat>
#include <cstring>
#include <vector>

namespace {

Component* FindComponentInstance(GameObject* object, const std::string& typeName,
                                 std::uint64_t instanceId) {
    if (!object || typeName.empty() || instanceId == 0) return nullptr;
    for (Component* component : object->GetComponents()) {
        if (component && component->GetInstanceID() == instanceId &&
            component->GetTypeName() == typeName) {
            return component;
        }
    }
    return nullptr;
}

} // namespace

InspectorWindow::InspectorWindow()
    : EditorWindow("Inspector") {
}

void InspectorWindow::SetTarget(GameObject* object) {
    target = object;
    targetId_ = object ? object->GetID() : 0u;
}

void InspectorWindow::ClearActiveEdit() {
    activeEditComponentType_.clear();
    activeEditComponentInstanceId_ = 0;
    activeEditTargetId_ = 0;
    beforeEditSnap_ = nlohmann::json{};
}

bool InspectorWindow::IsActiveEdit(unsigned int targetId, const Component* component) const {
    return component && activeEditTargetId_ == targetId &&
           activeEditComponentInstanceId_ == component->GetInstanceID() &&
           activeEditComponentType_ == component->GetTypeName();
}

void InspectorWindow::OnGUI() {
    if (!isOpen) return;

    // Check if active drag edit has finished
    if (activeEditComponentInstanceId_ != 0 && !ImGui::IsAnyItemActive()) {
        GameObject* editObj = molga::FindGameObjectById(activeEditTargetId_);
        if (editObj) {
            Component* comp = FindComponentInstance(
                editObj, activeEditComponentType_, activeEditComponentInstanceId_);
            if (comp) {
                nlohmann::json afterSnap = molga::CaptureComponentSnapshot(comp);
                if (beforeEditSnap_ != afterSnap) {
                    molga::RestoreComponentSnapshot(editObj, beforeEditSnap_);
                    Editor::Get().GetCommandHistory().Execute(
                        std::make_unique<molga::ComponentSnapshotCommand>(
                            activeEditTargetId_, activeEditComponentType_,
                            beforeEditSnap_, afterSnap
                        )
                    );
                }
            }
        }
        ClearActiveEdit();
    }

    // Selection changes normally keep this synchronized, but commands and
    // custom inspectors can delete an object between frames. Validate by the
    // stored value identity before the first member dereference.
    if (target) {
        GameObject* liveTarget = molga::FindGameObjectById(targetId_);
        if (!liveTarget || liveTarget != target) {
            target = nullptr;
            targetId_ = 0;
            ClearActiveEdit();
        }
    }

    ImGui::Begin(title.c_str(), &isOpen);

    if (!target) {
        ClearActiveEdit();
        ImGui::Spacing();
        ImGui::TextDisabled("%s No object selected", Icons::Cube);
        ImGui::TextDisabled("Select an object from the Hierarchy");
        ImGui::End();
        return;
    }

    // Lock checkbox
    bool locked = Editor::Get().GetSelection().IsInspectorLocked();
    if (ImGui::Checkbox("Lock", &locked)) {
        if (locked) {
            Editor::Get().GetSelection().LockInspector(target->GetID());
        } else {
            Editor::Get().GetSelection().UnlockInspector();
        }
    }
    ImGui::SameLine();

    // GameObject header with icon
    ImGui::Text("%s", Icons::Cube);
    ImGui::SameLine();

    static char nameBuffer[256];
    strncpy(nameBuffer, target->GetName().c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    static std::string beforeName;
    static bool isEditingName = false;

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
        if (!isEditingName) {
            beforeName = target->GetName();
            isEditingName = true;
        }
        target->SetName(nameBuffer);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        isEditingName = false;
        std::string afterName = target->GetName();
        auto beforeProp = molga::CaptureGameObjectProperties(target);
        beforeProp["name"] = beforeName;
        auto afterProp = molga::CaptureGameObjectProperties(target);
        afterProp["name"] = afterName;

        target->SetName(beforeName); // temporarily revert
        Editor::Get().GetCommandHistory().Execute(
            std::make_unique<molga::GameObjectPropertyCommand>(target->GetID(), beforeProp, afterProp)
        );
    } else if (ImGui::IsItemDeactivated()) {
        isEditingName = false;
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
                if (currentTag != t) {
                    auto beforeProp = molga::CaptureGameObjectProperties(target);
                    auto afterProp = beforeProp;
                    afterProp["tag"] = t;
                    Editor::Get().GetCommandHistory().Execute(
                        std::make_unique<molga::GameObjectPropertyCommand>(target->GetID(), beforeProp, afterProp)
                    );
                }
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
                if (currentLayer != i) {
                    auto beforeProp = molga::CaptureGameObjectProperties(target);
                    auto afterProp = beforeProp;
                    afterProp["layer"] = i;
                    Editor::Get().GetCommandHistory().Execute(
                        std::make_unique<molga::GameObjectPropertyCommand>(target->GetID(), beforeProp, afterProp)
                    );
                }
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
        const unsigned int activeTargetId = target->GetID();
        GameObject* const activeTargetIdentity = target;
        auto beforeProp = molga::CaptureGameObjectProperties(target);
        auto afterProp = beforeProp;
        afterProp["active"] = active;
        Editor::Get().GetCommandHistory().Execute(
            std::make_unique<molga::GameObjectPropertyCommand>(activeTargetId, beforeProp, afterProp)
        );
        GameObject* liveTarget = molga::FindGameObjectById(activeTargetId);
        if (!liveTarget || liveTarget != activeTargetIdentity) {
            target = liveTarget;
            targetId_ = liveTarget ? activeTargetId : 0u;
            ClearActiveEdit();
            ImGui::End();
            return;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Prefab Header Controls
    if (auto* pi = target->GetComponent<PrefabInstance>()) {
        const unsigned int prefabTargetId = target->GetID();
        GameObject* const prefabTargetIdentity = target;
        bool prefabCommandExecuted = false;
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
                    std::make_unique<molga::ApplyPrefabCommand>(prefabTargetId));
                prefabCommandExecuted = true;
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(Icons::Undo) + " Revert").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::RevertPrefabCommand>(prefabTargetId));
                prefabCommandExecuted = true;
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(Icons::Times) + " Unpack").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::UnpackPrefabCommand>(prefabTargetId));
                prefabCommandExecuted = true;
            }

            // Revert can rebuild the instance and Unpack destroys `pi`.
            // Never inspect the old component after one of those commands.
            if (!prefabCommandExecuted) {
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
            }
            
            ImGui::Unindent();
            ImGui::Separator();
            ImGui::Spacing();
        }

        if (prefabCommandExecuted) {
            GameObject* liveTarget = molga::FindGameObjectById(prefabTargetId);
            if (!liveTarget) {
                target = nullptr;
                targetId_ = 0;
                ClearActiveEdit();
                ImGui::End();
                return;
            }
            if (liveTarget != prefabTargetIdentity) ClearActiveEdit();
            target = liveTarget;
            targetId_ = prefabTargetId;
        }
    }

    // Snapshot value identities rather than pointers. Removing one component
    // can synchronously invoke user OnDisable/OnDetach code that removes a
    // different component; re-resolution prevents visiting a dangling entry.
    struct ComponentDrawIdentity {
        std::string typeName;
        std::uint64_t instanceId = 0;
    };
    const unsigned int drawTargetId = target->GetID();
    GameObject* const drawTargetIdentity = target;
    std::vector<ComponentDrawIdentity> drawComponents;
    for (Component* component : target->GetComponents()) {
        if (component) {
            drawComponents.push_back({component->GetTypeName(), component->GetInstanceID()});
        }
    }
    for (const ComponentDrawIdentity& identity : drawComponents) {
        GameObject* liveTarget = molga::FindGameObjectById(drawTargetId);
        if (!liveTarget || liveTarget != drawTargetIdentity) break;
        Component* component = FindComponentInstance(
            liveTarget, identity.typeName, identity.instanceId);
        if (!component) continue;
        target = liveTarget;
        DrawComponent(component);
        ImGui::Spacing();
    }

    // A custom inspector may destroy and flush the whole target object. Do not
    // let the controls below continue through the stale member pointer.
    GameObject* postDrawTarget = molga::FindGameObjectById(drawTargetId);
    if (!postDrawTarget || postDrawTarget != drawTargetIdentity) {
        target = nullptr;
        targetId_ = 0;
        ClearActiveEdit();
        ImGui::End();
        return;
    }
    target = postDrawTarget;

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
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "Transform")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Image) + " Sprite Renderer").c_str())) {
            if (!target->HasComponent<SpriteRenderer>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "SpriteRenderer")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Tilemap Renderer").c_str())) {
            if (!target->HasComponent<TilemapRenderer>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "TilemapRenderer")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Lightbulb) + " Particle System").c_str())) {
            if (!target->HasComponent<ParticleSystem>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "ParticleSystem")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Square) + " Box Collider 2D").c_str())) {
            if (!target->HasComponent<BoxCollider2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "BoxCollider2D")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Circle) + " Circle Collider 2D").c_str())) {
            if (!target->HasComponent<CircleCollider2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "CircleCollider2D")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Cogs) + " Rigidbody 2D").c_str())) {
            if (!target->HasComponent<Rigidbody2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "Rigidbody2D")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Music) + " Audio Source").c_str())) {
            if (!target->HasComponent<AudioSource>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "AudioSource")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::VolumeUp) + " Audio Listener").c_str())) {
            if (!target->HasComponent<AudioListener>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "AudioListener")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::Camera) + " Camera").c_str())) {
            if (!target->HasComponent<Camera>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "Camera")
                );
            }
        }
        if (ImGui::MenuItem((std::string(Icons::ListUl) + " Text Renderer 2D").c_str())) {
            if (!target->HasComponent<TextRenderer2D>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "TextRenderer2D")
                );
            }
        }
        if (ImGui::BeginMenu("UI")) {
            auto addUI = [&](const char* label, const char* type) {
                if (ImGui::MenuItem(label)) {
                    bool exists = false;
                    for (auto* c : target->GetComponents()) {
                        if (c && c->GetTypeName() == type) { exists = true; break; }
                    }
                    if (!exists) {
                        Editor::Get().GetCommandHistory().Execute(
                            std::make_unique<molga::ComponentAddCommand>(target->GetID(), type));
                    }
                }
            };
            addUI("Canvas", "UICanvas");
            addUI("Rect Transform", "RectTransform");
            addUI("Image", "UIImage");
            addUI("Label", "UILabel");
            addUI("Button", "UIButton");
            ImGui::EndMenu();
        }
#ifdef MOLGA_MARROW_SUPPORT
        if (ImGui::MenuItem((std::string(Icons::Image) + " Marrow Renderer").c_str())) {
            if (!target->HasComponent<MarrowRenderer>()) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentAddCommand>(target->GetID(), "MarrowRenderer")
                );
            }
        }
#endif
        ImGui::Separator();
        if (ImGui::BeginMenu((std::string(Icons::Code) + " Scripts").c_str())) {
            auto scripts = ScriptManager::Get().GetRegisteredScripts();
            for (const auto& scriptName : scripts) {
                bool hasScript = false;
                for (auto* c : target->GetComponents()) {
                    if (c && c->GetTypeName() == scriptName) {
                        hasScript = true;
                        break;
                    }
                }
                if (!hasScript) {
                    if (ImGui::MenuItem((std::string(Icons::FileCode) + " " + scriptName).c_str())) {
                        Editor::Get().GetCommandHistory().Execute(
                            std::make_unique<molga::ComponentAddCommand>(target->GetID(), scriptName)
                        );
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

namespace {

bool AcceptAssetGuidDrop(const char* expectedImporter, std::string& outGuid) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool accepted = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
        const auto* data = static_cast<const char*>(payload->Data);
        const std::size_t size = payload->DataSize > 0
            ? static_cast<std::size_t>(payload->DataSize - 1) : 0u;
        const std::string guid(data ? data : "", size);
        const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
        if (record && record->importer == expectedImporter) {
            outGuid = guid;
            accepted = true;
        }
    }
    ImGui::EndDragDropTarget();
    return accepted;
}

bool DrawP0ComponentFields(Component* component) {
    if (auto* body = dynamic_cast<Rigidbody2D*>(component)) {
        const char* bodyTypes[] = {"Static", "Kinematic", "Dynamic"};
        int type = static_cast<int>(body->GetBodyType());
        if (ImGui::Combo("Body Type", &type, bodyTypes, 3))
            body->SetBodyType(static_cast<Rigidbody2D::BodyType>(type));
        float gravityScale = body->GetGravityScale();
        if (ImGui::DragFloat("Gravity Scale", &gravityScale, 0.01f))
            body->SetGravityScale(gravityScale);
        float mass = body->GetMass();
        if (ImGui::DragFloat("Mass", &mass, 0.01f, 0.001f, FLT_MAX)) body->SetMass(mass);
        float linearDamping = body->GetLinearDamping();
        if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, FLT_MAX))
            body->SetLinearDamping(linearDamping);
        float angularDamping = body->GetAngularDamping();
        if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, FLT_MAX))
            body->SetAngularDamping(angularDamping);
        bool freezeRotation = body->IsRotationFrozen();
        if (ImGui::Checkbox("Freeze Rotation", &freezeRotation))
            body->SetFreezeRotation(freezeRotation);
        Vector2 velocity = body->GetVelocity();
        float velocityValues[2] = {velocity.x, velocity.y};
        if (ImGui::DragFloat2("Velocity", velocityValues, 0.1f))
            body->SetVelocity({velocityValues[0], velocityValues[1]});
        float angularVelocity = body->GetAngularVelocity();
        if (ImGui::DragFloat("Angular Velocity", &angularVelocity, 0.1f))
            body->SetAngularVelocity(angularVelocity);
        return true;
    }
    auto drawColliderBase = [](Collider2D* collider) {
        Vector2 offset = collider->GetOffset();
        float offsetValues[2] = {offset.x, offset.y};
        if (ImGui::DragFloat2("Offset", offsetValues, 0.1f))
            collider->SetOffset({offsetValues[0], offsetValues[1]});
        bool trigger = collider->IsTrigger();
        if (ImGui::Checkbox("Is Trigger", &trigger)) collider->SetTrigger(trigger);
        float friction = collider->GetFriction();
        if (ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, FLT_MAX))
            collider->SetFriction(friction);
        float restitution = collider->GetRestitution();
        if (ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, FLT_MAX))
            collider->SetRestitution(restitution);
    };
    if (auto* box = dynamic_cast<BoxCollider2D*>(component)) {
        Vector2 size = box->GetSize();
        float sizeValues[2] = {size.x, size.y};
        if (ImGui::DragFloat2("Size", sizeValues, 0.1f, 0.001f, FLT_MAX))
            box->SetSize({sizeValues[0], sizeValues[1]});
        drawColliderBase(box);
        return true;
    }
    if (auto* circle = dynamic_cast<CircleCollider2D*>(component)) {
        float radius = circle->GetRadius();
        if (ImGui::DragFloat("Radius", &radius, 0.1f, 0.001f, FLT_MAX))
            circle->SetRadius(radius);
        drawColliderBase(circle);
        return true;
    }
    if (auto* textRenderer = dynamic_cast<TextRenderer2D*>(component)) {
        char text[2048];
        std::strncpy(text, textRenderer->GetText().c_str(), sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        if (ImGui::InputTextMultiline("Text", text, sizeof(text), ImVec2(-FLT_MIN, 72.0f)))
            textRenderer->SetText(text);
        char guid[128];
        std::strncpy(guid, textRenderer->GetFontGuid().c_str(), sizeof(guid) - 1);
        guid[sizeof(guid) - 1] = '\0';
        if (ImGui::InputText("Font GUID", guid, sizeof(guid))) textRenderer->SetFontGuid(guid);
        std::string droppedGuid;
        if (AcceptAssetGuidDrop("FontImporter", droppedGuid)) textRenderer->SetFontGuid(droppedGuid);
        float fontSize = textRenderer->GetFontSizePx();
        if (ImGui::DragFloat("Font Size", &fontSize, 0.5f, 1.0f, 512.0f))
            textRenderer->SetFontSizePx(fontSize);
        float lineSpacing = textRenderer->GetLineSpacing();
        if (ImGui::DragFloat("Line Spacing", &lineSpacing, 0.01f, 0.1f, 10.0f))
            textRenderer->SetLineSpacing(lineSpacing);
        float scale = textRenderer->GetScale();
        if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.01f, FLT_MAX))
            textRenderer->SetScale(scale);
        Color color = textRenderer->GetColor();
        float rgba[4] = {color.r, color.g, color.b, color.a};
        if (ImGui::ColorEdit4("Color", rgba))
            textRenderer->SetColor({rgba[0], rgba[1], rgba[2], rgba[3]});
        const char* alignments[] = {"Left", "Center", "Right"};
        int alignment = static_cast<int>(textRenderer->GetAlignment());
        if (ImGui::Combo("Alignment", &alignment, alignments, 3))
            textRenderer->SetAlignment(static_cast<TextRenderer2D::Alignment>(alignment));
        int order = textRenderer->GetSortingOrder();
        if (ImGui::InputInt("Sorting Order", &order)) textRenderer->SetSortingOrder(order);
        return true;
    }
    if (auto* canvas = dynamic_cast<UICanvas*>(component)) {
        Vector2 reference = canvas->GetReferenceResolution();
        float values[2] = {reference.x, reference.y};
        if (ImGui::DragFloat2("Reference Resolution", values, 1.0f, 1.0f, 16384.0f))
            canvas->SetReferenceResolution({values[0], values[1]});
        float match = canvas->GetMatchWidthOrHeight();
        if (ImGui::SliderFloat("Width / Height Match", &match, 0.0f, 1.0f))
            canvas->SetMatchWidthOrHeight(match);
        int order = canvas->GetSortingOrder();
        if (ImGui::InputInt("Sorting Order", &order)) canvas->SetSortingOrder(order);
        return true;
    }
    if (auto* rect = dynamic_cast<RectTransform*>(component)) {
        Vector2 amin = rect->GetAnchorMin();
        Vector2 amax = rect->GetAnchorMax();
        Vector2 pivot = rect->GetPivot();
        Vector2 position = rect->GetAnchoredPosition();
        Vector2 size = rect->GetSizeDelta();
        float v2[2];
        v2[0] = amin.x; v2[1] = amin.y;
        if (ImGui::DragFloat2("Anchor Min", v2, 0.01f, 0.0f, 1.0f)) rect->SetAnchorMin({v2[0], v2[1]});
        v2[0] = amax.x; v2[1] = amax.y;
        if (ImGui::DragFloat2("Anchor Max", v2, 0.01f, 0.0f, 1.0f)) rect->SetAnchorMax({v2[0], v2[1]});
        v2[0] = pivot.x; v2[1] = pivot.y;
        if (ImGui::DragFloat2("Pivot", v2, 0.01f, 0.0f, 1.0f)) rect->SetPivot({v2[0], v2[1]});
        v2[0] = position.x; v2[1] = position.y;
        if (ImGui::DragFloat2("Anchored Position", v2, 0.5f)) rect->SetAnchoredPosition({v2[0], v2[1]});
        v2[0] = size.x; v2[1] = size.y;
        if (ImGui::DragFloat2("Size Delta", v2, 0.5f)) rect->SetSizeDelta({v2[0], v2[1]});
        ImGui::TextDisabled("Anchor Presets");
        if (ImGui::Button("Center")) rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
        ImGui::SameLine();
        if (ImGui::Button("Stretch")) {
            rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
            rect->SetSizeDelta({0.0f, 0.0f});
        }
        return true;
    }
    if (auto* image = dynamic_cast<UIImage*>(component)) {
        char guid[128];
        std::strncpy(guid, image->GetTextureGuid().c_str(), sizeof(guid) - 1);
        guid[sizeof(guid) - 1] = '\0';
        if (ImGui::InputText("Texture GUID", guid, sizeof(guid))) image->SetTextureGuid(guid);
        std::string droppedGuid;
        if (AcceptAssetGuidDrop("TextureImporter", droppedGuid)) image->SetTextureGuid(droppedGuid);
        Color color = image->GetTint();
        float rgba[4] = {color.r, color.g, color.b, color.a};
        if (ImGui::ColorEdit4("Tint", rgba)) image->SetTint({rgba[0], rgba[1], rgba[2], rgba[3]});
        int order = image->GetSortingOrder();
        if (ImGui::InputInt("Sorting Order", &order)) image->SetSortingOrder(order);
        return true;
    }
    if (auto* label = dynamic_cast<UILabel*>(component)) {
        char text[2048];
        std::strncpy(text, label->GetText().c_str(), sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        if (ImGui::InputTextMultiline("Text", text, sizeof(text), ImVec2(-FLT_MIN, 72.0f)))
            label->SetText(text);
        char guid[128];
        std::strncpy(guid, label->GetFontGuid().c_str(), sizeof(guid) - 1);
        guid[sizeof(guid) - 1] = '\0';
        if (ImGui::InputText("Font GUID", guid, sizeof(guid))) label->SetFontGuid(guid);
        std::string droppedGuid;
        if (AcceptAssetGuidDrop("FontImporter", droppedGuid)) label->SetFontGuid(droppedGuid);
        float size = label->GetFontSizePx();
        if (ImGui::DragFloat("Font Size", &size, 0.5f, 1.0f, 512.0f)) label->SetFontSizePx(size);
        float spacing = label->GetLineSpacing();
        if (ImGui::DragFloat("Line Spacing", &spacing, 0.01f, 0.1f, 10.0f)) label->SetLineSpacing(spacing);
        Color color = label->GetColor();
        float rgba[4] = {color.r, color.g, color.b, color.a};
        if (ImGui::ColorEdit4("Color", rgba)) label->SetColor({rgba[0], rgba[1], rgba[2], rgba[3]});
        const char* horizontal[] = {"Left", "Center", "Right"};
        int h = static_cast<int>(label->GetHorizontalAlignment());
        if (ImGui::Combo("Horizontal", &h, horizontal, 3))
            label->SetHorizontalAlignment(static_cast<UILabel::HorizontalAlignment>(h));
        const char* vertical[] = {"Top", "Middle", "Bottom"};
        int v = static_cast<int>(label->GetVerticalAlignment());
        if (ImGui::Combo("Vertical", &v, vertical, 3))
            label->SetVerticalAlignment(static_cast<UILabel::VerticalAlignment>(v));
        int order = label->GetSortingOrder();
        if (ImGui::InputInt("Sorting Order", &order)) label->SetSortingOrder(order);
        return true;
    }
    if (auto* button = dynamic_cast<UIButton*>(component)) {
        bool interactable = button->IsInteractable();
        if (ImGui::Checkbox("Interactable", &interactable)) button->SetInteractable(interactable);
        auto colorField = [](const char* name, Color value, auto setter) {
            float rgba[4] = {value.r, value.g, value.b, value.a};
            if (ImGui::ColorEdit4(name, rgba)) setter(Color{rgba[0], rgba[1], rgba[2], rgba[3]});
        };
        colorField("Normal", button->GetNormalColor(), [&](Color c) { button->SetNormalColor(c); });
        colorField("Hover", button->GetHoverColor(), [&](Color c) { button->SetHoverColor(c); });
        colorField("Pressed", button->GetPressedColor(), [&](Color c) { button->SetPressedColor(c); });
        colorField("Disabled", button->GetDisabledColor(), [&](Color c) { button->SetDisabledColor(c); });
        int order = button->GetSortingOrder();
        if (ImGui::InputInt("Sorting Order", &order)) button->SetSortingOrder(order);
        return true;
    }
    return false;
}

// Script가 RegisterFields()로 노출한 필드를 인스펙터에 그린다.
// 데이터(레지스트리)는 molga_core에, 렌더링(imgui)은 에디터에 있다.
void DrawScriptFields(Script* script) {
    const ScriptFieldRegistry& reg = script->Fields();
    if (reg.Empty()) return;

    World* world = script->GetGameObject() ? script->GetGameObject()->GetWorld() : nullptr;

    for (const auto& f : reg.Fields()) {
        switch (f.type) {
            case ScriptFieldType::Float:
                ImGui::DragFloat(f.name.c_str(), static_cast<float*>(f.ptr),
                                 f.uiSpeed, f.uiMin, f.uiMax);
                break;
            case ScriptFieldType::Int:
                ImGui::DragInt(f.name.c_str(), static_cast<int*>(f.ptr));
                break;
            case ScriptFieldType::Bool:
                ImGui::Checkbox(f.name.c_str(), static_cast<bool*>(f.ptr));
                break;
            case ScriptFieldType::String: {
                auto* s = static_cast<std::string*>(f.ptr);
                char buf[256];
                std::strncpy(buf, s->c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                if (ImGui::InputText(f.name.c_str(), buf, sizeof(buf))) {
                    *s = buf;
                }
                break;
            }
            case ScriptFieldType::Vector2:
                ImGui::DragFloat2(f.name.c_str(),
                                  reinterpret_cast<float*>(static_cast<Vector2*>(f.ptr)),
                                  f.uiSpeed);
                break;
            case ScriptFieldType::Color:
                ImGui::ColorEdit4(f.name.c_str(),
                                  reinterpret_cast<float*>(static_cast<Color*>(f.ptr)));
                break;
            case ScriptFieldType::ObjectRef: {
                auto* ref = static_cast<ObjectRef*>(f.ptr);
                const char* preview = "(None)";
                if (ref->targetId != 0) {
                    GameObject* target = world ? world->FindById(ref->targetId) : nullptr;
                    preview = target ? target->GetName().c_str() : "(Missing)";
                }
                if (ImGui::BeginCombo(f.name.c_str(), preview)) {
                    if (ImGui::Selectable("(None)", ref->targetId == 0)) ref->targetId = 0;
                    if (world) {
                        for (const auto& obj : world->Objects()) {
                            if (!obj) continue;
                            ImGui::PushID(static_cast<int>(obj->GetID()));
                            std::string entry = obj->GetName() + " #" + std::to_string(obj->GetID());
                            if (ImGui::Selectable(entry.c_str(), ref->targetId == obj->GetID())) {
                                ref->targetId = obj->GetID();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case ScriptFieldType::PrefabRef: {
                auto* ref = static_cast<PrefabRef*>(f.ptr);
                auto& registry = PrefabRegistry::Get();
                std::string preview = "(None)";
                if (!ref->guid.empty()) {
                    auto path = registry.GetPrefabPath(ref->guid);
                    preview = path.empty() ? "(Missing)" : path.filename().string();
                }
                if (ImGui::BeginCombo(f.name.c_str(), preview.c_str())) {
                    if (ImGui::Selectable("(None)", ref->guid.empty())) ref->guid.clear();
                    for (const auto& [guid, path] : registry.GetAllPrefabs()) {
                        ImGui::PushID(guid.c_str());
                        if (ImGui::Selectable(path.filename().string().c_str(), ref->guid == guid)) {
                            ref->guid = guid;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
        }
    }
}

} // namespace

void InspectorWindow::DrawComponent(Component* component) {
    if (!component || !target) return;

    const unsigned int componentTargetId = target->GetID();
    GameObject* const componentTargetIdentity = target;
    // User serialization/inspector/lifecycle callbacks may remove the object
    // from the editor World. Keep its storage alive for this stack frame, while
    // re-resolving editor membership before every subsequent use.
    const std::shared_ptr<GameObject> componentTargetHold =
        Editor::Get().ShareObjectById(componentTargetId);
    std::string typeName = component->GetTypeName();
    const std::uint64_t componentInstanceId = component->GetInstanceID();
    auto refreshComponent = [&]() -> bool {
        GameObject* liveTarget = molga::FindGameObjectById(componentTargetId);
        if (!liveTarget || liveTarget != componentTargetIdentity) return false;
        Component* liveComponent = FindComponentInstance(
            liveTarget, typeName, componentInstanceId);
        if (!liveComponent) return false;
        target = liveTarget;
        component = liveComponent;
        return true;
    };

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

    // Capture snapshot before rendering
    nlohmann::json snapBefore = molga::CaptureComponentSnapshot(component);
    if (!refreshComponent()) {
        if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
        return;
    }
    if (IsActiveEdit(componentTargetId, component)) {
        // keep beforeEditSnap_
    } else if (activeEditComponentInstanceId_ == 0) {
        beforeEditSnap_ = snapBefore;
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

    // Context menu for component header (except Transform)
    bool removed = false;
    if (ImGui::BeginPopupContextItem()) {
        if (typeName != "Transform") {
            if (ImGui::MenuItem((std::string(Icons::Trash) + " Remove Component").c_str())) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ComponentRemoveCommand>(target->GetID(), typeName)
                );
                removed = true;
            }
        }
        ImGui::EndPopup();
    }

    // ComponentRemoveCommand destroys the component synchronously. Balance the
    // ImGui tree and leave immediately; even reading IsEnabled()/serializing a
    // snapshot below this point would dereference freed memory.
    if (removed) {
        if (activeEditTargetId_ == componentTargetId &&
            activeEditComponentInstanceId_ == componentInstanceId &&
            activeEditComponentType_ == typeName) ClearActiveEdit();
        if (open) ImGui::TreePop();
        return;
    }

    // Enable/disable checkbox on the same line
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    bool enabled = component->IsEnabled();
    if (ImGui::Checkbox(("##" + typeName + "Enabled").c_str(), &enabled)) {
        nlohmann::json beforeSnap = molga::CaptureComponentSnapshot(component);
        if (!refreshComponent()) {
            if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
            if (open) ImGui::TreePop();
            return;
        }
        nlohmann::json afterSnap = beforeSnap;
        afterSnap["enabled"] = enabled;
        // Revert temporarily so Execute() works
        component->SetEnabled(!enabled);
        Editor::Get().GetCommandHistory().Execute(
            std::make_unique<molga::ComponentSnapshotCommand>(
                componentTargetId, typeName, beforeSnap, afterSnap
            )
        );

        // SetEnabled invokes user lifecycle callbacks. They are allowed to
        // remove this component (or the target object), so refresh before the
        // open-body code below uses either pointer again.
        GameObject* liveTarget = molga::FindGameObjectById(componentTargetId);
        Component* liveComponent = liveTarget && liveTarget == componentTargetIdentity
            ? FindComponentInstance(liveTarget, typeName, componentInstanceId)
            : nullptr;
        if (!liveComponent) {
            if (activeEditTargetId_ == componentTargetId &&
                activeEditComponentInstanceId_ == componentInstanceId &&
                activeEditComponentType_ == typeName) ClearActiveEdit();
            if (open) ImGui::TreePop();
            return;
        }
        target = liveTarget;
        component = liveComponent;
    }

    if (open) {
        ImGui::Spacing();
        // 스크립트는 RegisterFields()로 노출한 필드를 자동 렌더링한다.
        if (auto* script = dynamic_cast<Script*>(component)) {
            DrawScriptFields(script);
        }
        DrawP0ComponentFields(component);
        // 컴포넌트/스크립트의 커스텀 OnInspectorGUI (오버라이드 시).
        component->OnInspectorGUI();
        ImGui::TreePop();
    }

    // Custom inspector code is user code and may remove this component, a
    // sibling, or the target object itself. Re-resolve before touching it again.
    GameObject* liveTarget = molga::FindGameObjectById(componentTargetId);
    if (!liveTarget || liveTarget != componentTargetIdentity) {
        if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
        return;
    }
    component = FindComponentInstance(liveTarget, typeName, componentInstanceId);
    if (!component) {
        if (activeEditTargetId_ == componentTargetId &&
            activeEditComponentInstanceId_ == componentInstanceId &&
            activeEditComponentType_ == typeName) ClearActiveEdit();
        return;
    }
    target = liveTarget;

    // Capture snapshot after rendering and compare
    nlohmann::json snapAfter = molga::CaptureComponentSnapshot(component);
    if (!refreshComponent()) {
        if (activeEditTargetId_ == componentTargetId) ClearActiveEdit();
        return;
    }
    if (snapBefore != snapAfter) {
        if (ImGui::IsAnyItemActive()) {
            activeEditComponentType_ = typeName;
            activeEditComponentInstanceId_ = componentInstanceId;
            activeEditTargetId_ = componentTargetId;
        } else {
            // Immediate change
            molga::RestoreComponentSnapshot(target, beforeEditSnap_);
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::ComponentSnapshotCommand>(
                    componentTargetId, typeName, beforeEditSnap_, snapAfter
                )
            );
            ClearActiveEdit();
        }
    }
}
