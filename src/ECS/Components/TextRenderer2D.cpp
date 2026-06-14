#include "TextRenderer2D.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Shader.h"
#include "../../Rendering/TextRenderer.h"
#include <nlohmann/json.hpp>
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
    float lineSpacing = 1.2f;
    float lineHeight = tr.GetTextHeight(scale);
    Shader* activeShader = renderer->GetCurrentShader();

    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        float width = tr.GetTextWidth(line, scale);
        float offsetX = 0.0f;
        if (alignment == Alignment::Center) {
            offsetX = -width * 0.5f;
        } else if (alignment == Alignment::Right) {
            offsetX = -width;
        }

        float lineY = pos.y + i * lineHeight * lineSpacing;
        tr.RenderText(renderer, activeShader, line, pos.x + offsetX, lineY, scale, color);
    }
}

void TextRenderer2D::Serialize(nlohmann::json& j) const {
    j["text"] = text;
    j["color"] = { color.r, color.g, color.b, color.a };
    j["scale"] = scale;
    j["alignment"] = static_cast<int>(alignment);
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
        alignment = static_cast<Alignment>(j["alignment"].get<int>());
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

    // Sorting order
    int order = sortingOrder;
    if (ImGui::InputInt("Sorting Order", &order)) {
        sortingOrder = order;
    }
#endif
}
