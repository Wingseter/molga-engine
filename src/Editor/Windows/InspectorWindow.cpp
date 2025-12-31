#include "InspectorWindow.h"
#include "../../ECS/GameObject.h"
#include "../../ECS/Component.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Components/SpriteRenderer.h"
#include "../../ECS/Components/BoxCollider2D.h"
#include <imgui.h>

InspectorWindow::InspectorWindow()
    : EditorWindow("Inspector") {
}

void InspectorWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::Begin(title.c_str(), &isOpen);

    if (!target) {
        ImGui::TextDisabled("No object selected");
        ImGui::End();
        return;
    }

    // GameObject header
    static char nameBuffer[256];
    strncpy(nameBuffer, target->GetName().c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        target->SetName(nameBuffer);
    }

    bool active = target->IsActive();
    if (ImGui::Checkbox("Active", &active)) {
        target->SetActive(active);
    }

    ImGui::Separator();

    // Draw all components
    for (const auto& comp : target->GetComponents()) {
        DrawComponent(comp.get());
    }

    ImGui::Separator();

    // Add component button
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (ImGui::MenuItem("Transform")) {
            if (!target->HasComponent<Transform>()) {
                target->AddComponent<Transform>();
            }
        }
        if (ImGui::MenuItem("Sprite Renderer")) {
            if (!target->HasComponent<SpriteRenderer>()) {
                target->AddComponent<SpriteRenderer>();
            }
        }
        if (ImGui::MenuItem("Box Collider 2D")) {
            if (!target->HasComponent<BoxCollider2D>()) {
                target->AddComponent<BoxCollider2D>();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void InspectorWindow::DrawComponent(Component* component) {
    if (!component) return;

    std::string typeName = component->GetTypeName();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;

    bool open = ImGui::TreeNodeEx((void*)component, flags, "%s", typeName.c_str());

    // Enable/disable checkbox on the same line
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    bool enabled = component->IsEnabled();
    if (ImGui::Checkbox(("##" + typeName + "Enabled").c_str(), &enabled)) {
        component->SetEnabled(enabled);
    }

    if (open) {
        if (typeName == "Transform") {
            DrawTransformComponent();
        } else if (typeName == "SpriteRenderer") {
            DrawSpriteRendererComponent();
        } else if (typeName == "BoxCollider2D") {
            DrawBoxCollider2DComponent();
        }
        ImGui::TreePop();
    }
}

void InspectorWindow::DrawTransformComponent() {
    Transform* transform = target->GetComponent<Transform>();
    if (!transform) return;

    // Position
    float pos[2] = { transform->GetX(), transform->GetY() };
    if (ImGui::DragFloat2("Position", pos, 0.5f)) {
        transform->SetPosition(pos[0], pos[1]);
    }

    // Rotation
    float rotation = transform->GetRotation();
    if (ImGui::DragFloat("Rotation", &rotation, 0.5f)) {
        transform->SetRotation(rotation);
    }

    // Scale
    Vector2 scale = transform->GetScale();
    float scaleArr[2] = { scale.x, scale.y };
    if (ImGui::DragFloat2("Scale", scaleArr, 0.01f)) {
        transform->SetScale(scaleArr[0], scaleArr[1]);
    }
}

void InspectorWindow::DrawSpriteRendererComponent() {
    SpriteRenderer* spriteRenderer = target->GetComponent<SpriteRenderer>();
    if (!spriteRenderer) return;

    // Size
    float size[2] = { spriteRenderer->GetWidth(), spriteRenderer->GetHeight() };
    if (ImGui::DragFloat2("Size", size, 0.5f)) {
        spriteRenderer->SetSize(size[0], size[1]);
    }

    // Color
    Color c = spriteRenderer->GetColor();
    float color[4] = { c.r, c.g, c.b, c.a };
    if (ImGui::ColorEdit4("Color", color)) {
        spriteRenderer->SetColor(color[0], color[1], color[2], color[3]);
    }

    // Flip
    bool flipX = spriteRenderer->GetFlipX();
    bool flipY = spriteRenderer->GetFlipY();
    if (ImGui::Checkbox("Flip X", &flipX)) {
        spriteRenderer->SetFlipX(flipX);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Flip Y", &flipY)) {
        spriteRenderer->SetFlipY(flipY);
    }

    // Sorting order
    int sortingOrder = spriteRenderer->GetSortingOrder();
    if (ImGui::InputInt("Sorting Order", &sortingOrder)) {
        spriteRenderer->SetSortingOrder(sortingOrder);
    }
}

void InspectorWindow::DrawBoxCollider2DComponent() {
    BoxCollider2D* collider = target->GetComponent<BoxCollider2D>();
    if (!collider) return;

    // Size
    Vector2 size = collider->GetSize();
    float sizeArr[2] = { size.x, size.y };
    if (ImGui::DragFloat2("Size", sizeArr, 0.5f)) {
        collider->SetSize(sizeArr[0], sizeArr[1]);
    }

    // Offset
    Vector2 offset = collider->GetOffset();
    float offsetArr[2] = { offset.x, offset.y };
    if (ImGui::DragFloat2("Offset", offsetArr, 0.5f)) {
        collider->SetOffset(offsetArr[0], offsetArr[1]);
    }

    // Is Trigger
    bool isTrigger = collider->IsTrigger();
    if (ImGui::Checkbox("Is Trigger", &isTrigger)) {
        collider->SetTrigger(isTrigger);
    }
}
