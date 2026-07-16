#include "TextRenderer2D.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Shader.h"
#include "../../Rendering/TextRenderer.h"
#include "Rendering/RenderQueue.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>
#include <cstring>

#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

REGISTER_COMPONENT(TextRenderer2D)

using json = nlohmann::json;

void TextRenderer2D::RenderSprite(Renderer* renderer) {
    if (!gameObject || !enabled || text.empty()) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    Vector2 pos = transform->GetWorldPosition();

    // Split text by '\n'
    std::vector<std::string> lines;
    {
        std::string curLine;
        for (char c : text) {
            if (c == '\n') {
                lines.push_back(curLine);
                curLine.clear();
            } else {
                curLine.push_back(c);
            }
        }
        lines.push_back(curLine);
    }

    TextRenderer& tr = TextRenderer::Get();
    const float legacyBitmapScale = (fontSizePx / 8.0f) * scale;
    float lineHeight = tr.GetTextHeight(legacyBitmapScale);
    Shader* activeShader = renderer->GetCurrentShader();

    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        float width = tr.GetTextWidth(line, legacyBitmapScale);
        float offsetX = 0.0f;
        if (alignment == Alignment::Center) {
            offsetX = -width * 0.5f;
        } else if (alignment == Alignment::Right) {
            offsetX = -width;
        }

        float lineY = pos.y + i * lineHeight * lineSpacing;
        tr.RenderText(renderer, activeShader, line, pos.x + offsetX, lineY,
                      legacyBitmapScale, color);
    }
}

void TextRenderer2D::Serialize(nlohmann::json& j) const {
    j["text"] = text;
    j["color"] = { color.r, color.g, color.b, color.a };
    j["scale"] = scale;
    j["alignment"] = static_cast<int>(alignment);
    j["fontGuid"] = fontGuid;
    j["fontSizePx"] = fontSizePx;
    j["lineSpacing"] = lineSpacing;
    j["fontName"] = fontName;
    j["sortingOrder"] = sortingOrder;
}

void TextRenderer2D::Deserialize(const nlohmann::json& j) {
    if (j.contains("text")) {
        text = j["text"].get<std::string>();
    }
    if (j.contains("color") && j["color"].is_array()) {
        color = Color(j["color"][0], j["color"][1], j["color"][2], j["color"][3]);
    }
    if (j.contains("scale")) {
        scale = j["scale"].get<float>();
    }
    if (j.contains("alignment")) {
        const int value = std::clamp(j["alignment"].get<int>(), 0, 2);
        alignment = static_cast<Alignment>(value);
    }
    if (j.contains("fontGuid") && j["fontGuid"].is_string()) {
        fontGuid = j["fontGuid"].get<std::string>();
    }
    if (j.contains("fontSizePx") && j["fontSizePx"].is_number()) {
        SetFontSizePx(j["fontSizePx"].get<float>());
    } else {
        // Legacy bitmap text used an 8-pixel em. Preserve its previous size
        // while freshly-created components use the new 16-pixel default.
        fontSizePx = 8.0f;
    }
    if (j.contains("lineSpacing") && j["lineSpacing"].is_number()) {
        SetLineSpacing(j["lineSpacing"].get<float>());
    }
    if (j.contains("fontName")) {
        fontName = j["fontName"].get<std::string>();
    }
    if (j.contains("sortingOrder")) {
        sortingOrder = j["sortingOrder"].get<int>();
    }
}

void TextRenderer2D::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    // Multi-line text input
    char textBuffer[1024];
    strncpy(textBuffer, text.c_str(), sizeof(textBuffer) - 1);
    textBuffer[sizeof(textBuffer) - 1] = '\0';
    if (ImGui::InputTextMultiline("Text", textBuffer, sizeof(textBuffer), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4))) {
        text = textBuffer;
    }

    // Color picker
    float colorArr[4] = { color.r, color.g, color.b, color.a };
    if (ImGui::ColorEdit4("Color", colorArr)) {
        color = Color(colorArr[0], colorArr[1], colorArr[2], colorArr[3]);
    }

    // Scale slider (float slider, say from 0.1f to 10.0f)
    float s = scale;
    if (ImGui::SliderFloat("Scale", &s, 0.1f, 10.0f)) {
        scale = s;
    }

    // Alignment combo box
    const char* alignments[] = { "Left", "Center", "Right" };
    int currentAlign = static_cast<int>(alignment);
    if (ImGui::Combo("Alignment", &currentAlign, alignments, IM_ARRAYSIZE(alignments))) {
        alignment = static_cast<Alignment>(currentAlign);
    }

    // Font name
    char fontNameBuffer[128];
    strncpy(fontNameBuffer, fontName.c_str(), sizeof(fontNameBuffer) - 1);
    fontNameBuffer[sizeof(fontNameBuffer) - 1] = '\0';
    if (ImGui::InputText("Font Name", fontNameBuffer, sizeof(fontNameBuffer))) {
        fontName = fontNameBuffer;
    }

    char fontGuidBuffer[128];
    strncpy(fontGuidBuffer, fontGuid.c_str(), sizeof(fontGuidBuffer) - 1);
    fontGuidBuffer[sizeof(fontGuidBuffer) - 1] = '\0';
    if (ImGui::InputText("Font GUID", fontGuidBuffer, sizeof(fontGuidBuffer))) {
        fontGuid = fontGuidBuffer;
    }
    ImGui::DragFloat("Font Size (px)", &fontSizePx, 1.0f, 1.0f, 512.0f);
    ImGui::DragFloat("Line Spacing", &lineSpacing, 0.01f, 0.1f, 10.0f);

    // Sorting order
    int order = sortingOrder;
    if (ImGui::InputInt("Sorting Order", &order)) {
        sortingOrder = order;
    }
#endif
}

void TextRenderer2D::CollectRender(molga::RenderQueue& queue) {
    if (!gameObject || !enabled || text.empty()) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    const Vector2 position = transform->GetWorldPosition();
    TextDrawParams params;
    params.text = text;
    params.fontGuid = fontGuid;
    params.x = position.x;
    params.y = position.y;
    params.fontSizePx = fontSizePx;
    params.scale = scale;
    params.lineSpacing = lineSpacing;
    params.color = color;
    params.sortingOrder = sortingOrder;
    switch (alignment) {
        case Alignment::Center:
            params.alignment = TextHorizontalAlignment::Center;
            break;
        case Alignment::Right:
            params.alignment = TextHorizontalAlignment::Right;
            break;
        case Alignment::Left:
        default:
            params.alignment = TextHorizontalAlignment::Left;
            break;
    }
    TextRenderer::Get().CollectText(queue, params);
}
