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

namespace {

using molga::text::BaseDirection;

// UILabel.cpp와 같은 토큰을 쓴다. 두 컴포넌트가 하나의 텍스트 서비스를 기술하는
// 이상, 같은 정책이 파일에서 다른 이름으로 보이면 안 된다. 정수 서수로 저장하면
// 열거에 값이 끼어드는 순간 디스크의 기존 scene이 다른 정책을 뜻하게 된다.
const char* ToToken(BaseDirection value) {
    switch (value) {
        case BaseDirection::LeftToRight: return "LTR";
        case BaseDirection::RightToLeft: return "RTL";
        case BaseDirection::Auto: break;
    }
    return "Auto";
}

BaseDirection BaseDirectionFromToken(const std::string& token) {
    if (token == "LTR") return BaseDirection::LeftToRight;
    if (token == "RTL") return BaseDirection::RightToLeft;
    return BaseDirection::Auto;
}

} // namespace

void TextRenderer2D::SetFontFamilyGuid(const std::string& val) {
    fontFamilyGuid = val;
    // family를 저작하는 것이 legacy 폰트 지목을 대체할 값을 공급하는 유일한
    // 행위다. 이때 표식을 내려놓지 않으면 저작한 family가 저장에서 사라진다.
    if (!fontFamilyGuid.empty()) {
        loadedLegacyFontGuid = false;
    }
}

void TextRenderer2D::SetLocale(const std::string& val) {
    // 빈 태그는 "locale 없음"이 아니라 root tailoring 요청이다.
    locale = val.empty() ? std::string("und") : val;
}

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
    // schema 2도 legacy 키를 그대로 들고 간다. 위치/회전/월드 스케일은 형제
    // Transform이 권한을 가지므로 여기서 복제하지 않고, 저작된 bound가 없으므로
    // 폭/높이 키도 만들지 않는다.
    j["text"] = text;
    j["color"] = { color.r, color.g, color.b, color.a };
    j["scale"] = scale;
    j["alignment"] = static_cast<int>(alignment);
    j["fontGuid"] = fontGuid;
    j["fontSizePx"] = fontSizePx;
    j["lineSpacing"] = lineSpacing;
    j["fontName"] = fontName;
    if (!loadedLegacyFontGuid) {
        // 아직 이관되지 않은 payload에는 새 키가 하나도 새지 않아야 migration 전
        // scene 파일이 조용히 다시 쓰이지 않는다.
        j["schemaVersion"] = 2;
        j["fontFamilyGuid"] = fontFamilyGuid;
        j["locale"] = locale;
        j["baseDirection"] = ToToken(baseDirection);
    }
    molga::SerializeWorldSortSettings(j, GetWorldSortSettings());
}

void TextRenderer2D::Deserialize(const nlohmann::json& j) {
    const int schemaVersion = j.value("schemaVersion", 1);
    // 표식은 payload에서만 온다. 저작 상태에서 유도하면 같은 파일을 두 번
    // 읽었을 때 서로 다른 형식으로 저장될 수 있다.
    loadedLegacyFontGuid = schemaVersion < 2;
    const molga::WorldSortSettings2D worldSort =
        molga::DeserializeWorldSortSettings(j);
    sortingLayer = worldSort.sortingLayer;
    sortingOrder = worldSort.sortingOrder;
    sortMode = worldSort.sortMode;
    ySortOffset = worldSort.ySortOffset;
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
    } else if (schemaVersion < 2) {
        // Legacy bitmap text used an 8-pixel em. Preserve its previous size
        // while freshly-created components use the new 16-pixel default. A
        // schema 2 document always carries the field, so the bitmap em must not
        // leak into one that merely lost the key.
        fontSizePx = 8.0f;
    }
    if (j.contains("lineSpacing") && j["lineSpacing"].is_number()) {
        SetLineSpacing(j["lineSpacing"].get<float>());
    }
    if (j.contains("fontName")) {
        fontName = j["fontName"].get<std::string>();
    }

    if (schemaVersion >= 2) {
        if (j.contains("fontFamilyGuid") && j["fontFamilyGuid"].is_string()) {
            fontFamilyGuid = j["fontFamilyGuid"].get<std::string>();
        }
        if (j.contains("locale") && j["locale"].is_string()) {
            SetLocale(j["locale"].get<std::string>());
        }
        if (j.contains("baseDirection") && j["baseDirection"].is_string()) {
            baseDirection = BaseDirectionFromToken(j["baseDirection"].get<std::string>());
        }
    } else {
        // schema 1 payload를 읽는 것은 부분 갱신이 아니라 그 상태로 되돌리는
        // 일이다. 실행 취소는 기존 컴포넌트에 스냅샷을 다시 Deserialize하므로,
        // schema 2 전용 값을 남겨 두면 표식은 legacy인데 메모리는 저작된
        // family/locale을 들고 있는, 저장 형식과 다른 텍스트 정체성이 남는다.
        fontFamilyGuid.clear();
        locale = "und";
        baseDirection = molga::text::BaseDirection::Auto;
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
    const molga::SortKey sortKey = molga::MakeWorldSortKey(
        GetWorldSortSettings(), position.y);
    params.cameraPass = sortKey.cameraPass;
    params.sortingLayer = sortKey.sortingLayer;
    params.sortingOrder = sortKey.sortingOrder;
    params.depthOrYSort = sortKey.depthOrYSort;
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
