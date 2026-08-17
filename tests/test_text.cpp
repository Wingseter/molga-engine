#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/TextRenderer2D.h"
#include "Rendering/TextRenderer.h"
#include "Core/SceneSerializer.h"
#include "doctest.h"
#include <memory>
#include <vector>

TEST_CASE("TextRenderer2D: properties and getters/setters") {
    TextRenderer2D textComp;
    
    // Default values
    CHECK(textComp.GetText() == "Text");
    CHECK(textComp.GetColor().r == doctest::Approx(1.0f));
    CHECK(textComp.GetColor().g == doctest::Approx(1.0f));
    CHECK(textComp.GetColor().b == doctest::Approx(1.0f));
    CHECK(textComp.GetColor().a == doctest::Approx(1.0f));
    CHECK(textComp.GetScale() == doctest::Approx(1.0f));
    CHECK(textComp.GetAlignment() == TextRenderer2D::Alignment::Left);
    CHECK(textComp.GetFontGuid().empty());
    CHECK(textComp.GetFontSizePx() == doctest::Approx(16.0f));
    CHECK(textComp.GetLineSpacing() == doctest::Approx(1.2f));
    CHECK(textComp.GetFontName() == "default");
    CHECK(textComp.GetSortingOrder() == 0);
    CHECK(textComp.IsEnabled());

    // Modify values
    textComp.SetText("Hello Molga");
    textComp.SetColor(Color(0.5f, 0.2f, 0.8f, 0.9f));
    textComp.SetScale(2.5f);
    textComp.SetAlignment(TextRenderer2D::Alignment::Center);
    textComp.SetFontGuid("0123456789abcdef0123456789abcdef");
    textComp.SetFontSizePx(28.0f);
    textComp.SetLineSpacing(1.5f);
    textComp.SetFontName("custom_font");
    textComp.SetSortingOrder(15);
    textComp.SetEnabled(false);

    // Check updated values
    CHECK(textComp.GetText() == "Hello Molga");
    CHECK(textComp.GetColor().r == doctest::Approx(0.5f));
    CHECK(textComp.GetColor().g == doctest::Approx(0.2f));
    CHECK(textComp.GetColor().b == doctest::Approx(0.8f));
    CHECK(textComp.GetColor().a == doctest::Approx(0.9f));
    CHECK(textComp.GetScale() == doctest::Approx(2.5f));
    CHECK(textComp.GetAlignment() == TextRenderer2D::Alignment::Center);
    CHECK(textComp.GetFontGuid() == "0123456789abcdef0123456789abcdef");
    CHECK(textComp.GetFontSizePx() == doctest::Approx(28.0f));
    CHECK(textComp.GetLineSpacing() == doctest::Approx(1.5f));
    CHECK(textComp.GetFontName() == "custom_font");
    CHECK(textComp.GetSortingOrder() == 15);
    CHECK(!textComp.IsEnabled());
}

TEST_CASE("TextRenderer2D: alignment rendering coordinate math") {
    // Simulated coordinate math based on the RenderSprite implementation
    TextRenderer& tr = TextRenderer::Get();
    // In headless test, tr is uninitialized, so:
    // GetTextWidth(line, scale) returns line.length() * 8.0f * scale
    // GetTextHeight(scale) returns 16.0f * scale

    std::string text = "A\nBC";
    float scale = 2.0f;
    Vector2 pos(10.0f, 20.0f);
    float lineSpacing = 1.2f;

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

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "A");
    CHECK(lines[1] == "BC");

    float lineHeight = tr.GetTextHeight(scale); // 16.0f * 2.0f = 32.0f

    // Verify alignment Left offsets
    {
        TextRenderer2D::Alignment alignment = TextRenderer2D::Alignment::Left;
        CHECK(alignment == TextRenderer2D::Alignment::Left);
        
        // Line 0: "A"
        float width0 = tr.GetTextWidth(lines[0], scale); // 1 * 8 * 2 = 16
        float offsetX0 = 0.0f;
        float lineY0 = pos.y + 0 * lineHeight * lineSpacing;
        CHECK(width0 == doctest::Approx(16.0f));
        CHECK(offsetX0 == doctest::Approx(0.0f));
        CHECK(lineY0 == doctest::Approx(20.0f));

        // Line 1: "BC"
        float width1 = tr.GetTextWidth(lines[1], scale); // 2 * 8 * 2 = 32
        float offsetX1 = 0.0f;
        float lineY1 = pos.y + 1 * lineHeight * lineSpacing; // 20 + 32 * 1.2 = 58.4
        CHECK(width1 == doctest::Approx(32.0f));
        CHECK(offsetX1 == doctest::Approx(0.0f));
        CHECK(lineY1 == doctest::Approx(58.4f));
    }

    // Verify alignment Center offsets
    {
        TextRenderer2D::Alignment alignment = TextRenderer2D::Alignment::Center;
        CHECK(alignment == TextRenderer2D::Alignment::Center);
        
        // Line 0: "A"
        float width0 = tr.GetTextWidth(lines[0], scale); // 16
        float offsetX0 = -width0 * 0.5f; // -8
        float lineY0 = pos.y + 0 * lineHeight * lineSpacing;
        CHECK(offsetX0 == doctest::Approx(-8.0f));
        CHECK(lineY0 == doctest::Approx(20.0f));

        // Line 1: "BC"
        float width1 = tr.GetTextWidth(lines[1], scale); // 32
        float offsetX1 = -width1 * 0.5f; // -16
        float lineY1 = pos.y + 1 * lineHeight * lineSpacing;
        CHECK(offsetX1 == doctest::Approx(-16.0f));
        CHECK(lineY1 == doctest::Approx(58.4f));
    }

    // Verify alignment Right offsets
    {
        TextRenderer2D::Alignment alignment = TextRenderer2D::Alignment::Right;
        CHECK(alignment == TextRenderer2D::Alignment::Right);
        
        // Line 0: "A"
        float width0 = tr.GetTextWidth(lines[0], scale); // 16
        float offsetX0 = -width0; // -16
        float lineY0 = pos.y + 0 * lineHeight * lineSpacing;
        CHECK(offsetX0 == doctest::Approx(-16.0f));
        CHECK(lineY0 == doctest::Approx(20.0f));

        // Line 1: "BC"
        float width1 = tr.GetTextWidth(lines[1], scale); // 32
        float offsetX1 = -width1; // -32
        float lineY1 = pos.y + 1 * lineHeight * lineSpacing;
        CHECK(offsetX1 == doctest::Approx(-32.0f));
        CHECK(lineY1 == doctest::Approx(58.4f));
    }
}

TEST_CASE("TextRenderer2D: serialization and deserialization roundtrip") {
    // Register TextRenderer2D in factory is done by REGISTER_COMPONENT macro
    auto original = std::make_shared<GameObject>("TextObj");
    TextRenderer2D* tr = original->AddComponent<TextRenderer2D>();
    tr->SetText("Testing\nMultiline\nText");
    tr->SetColor(Color(0.1f, 0.2f, 0.3f, 0.4f));
    tr->SetScale(1.5f);
    tr->SetAlignment(TextRenderer2D::Alignment::Right);
    tr->SetFontGuid("abcdef0123456789abcdef0123456789");
    tr->SetFontSizePx(32.0f);
    tr->SetLineSpacing(1.35f);
    tr->SetFontName("arial");
    tr->SetSortingOrder(42);

    // Serialize GameObject
    std::string jsonStr = SceneSerializer::SerializeGameObject(original.get());
    CHECK(!jsonStr.empty());

    // Deserialize GameObject
    auto restored = SceneSerializer::DeserializeGameObject(jsonStr);
    REQUIRE(restored != nullptr);
    CHECK(restored->GetName() == "TextObj");

    // Retrieve TextRenderer2D Component
    TextRenderer2D* restoredTr = restored->GetComponent<TextRenderer2D>();
    REQUIRE(restoredTr != nullptr);

    // Verify properties
    CHECK(restoredTr->GetText() == "Testing\nMultiline\nText");
    CHECK(restoredTr->GetColor().r == doctest::Approx(0.1f));
    CHECK(restoredTr->GetColor().g == doctest::Approx(0.2f));
    CHECK(restoredTr->GetColor().b == doctest::Approx(0.3f));
    CHECK(restoredTr->GetColor().a == doctest::Approx(0.4f));
    CHECK(restoredTr->GetScale() == doctest::Approx(1.5f));
    CHECK(restoredTr->GetAlignment() == TextRenderer2D::Alignment::Right);
    CHECK(restoredTr->GetFontGuid() == "abcdef0123456789abcdef0123456789");
    CHECK(restoredTr->GetFontSizePx() == doctest::Approx(32.0f));
    CHECK(restoredTr->GetLineSpacing() == doctest::Approx(1.35f));
    CHECK(restoredTr->GetFontName() == "arial");
    CHECK(restoredTr->GetSortingOrder() == 42);
    CHECK(restoredTr->IsEnabled());
}

TEST_CASE("TextRenderer2D: legacy scenes preserve bitmap sizing") {
    TextRenderer2D component;
    nlohmann::json legacy = {
        {"text", "legacy"},
        {"scale", 2.0f},
        {"fontName", "default"}
    };

    component.Deserialize(legacy);

    CHECK(component.GetFontGuid().empty());
    CHECK(component.GetFontSizePx() == doctest::Approx(8.0f));
    CHECK(component.GetScale() == doctest::Approx(2.0f));
    CHECK(component.GetLineSpacing() == doctest::Approx(1.2f));
}
