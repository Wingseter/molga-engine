#include "doctest.h"

#include "Core/SceneSerializer.h"
#include "Core/World.h"
#include "ECS/BuiltinComponents.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/GameObject.h"
#include "UI/UISystem.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIRuntimeInvalidation.h"

#include "Assets/FontArtifactStore.h"
#include "Core/AssetDatabase.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/TextRenderer.h"
#include "Text/TextDiagnostic.h"
#include "TextQualificationAssetTree.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
std::shared_ptr<GameObject> MakeCanvas(World& world) {
    auto object = std::make_shared<GameObject>("Canvas");
    object->AddComponent<UICanvas>();
    auto* rect = object->AddComponent<RectTransform>();
    rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetSizeDelta({0.0f, 0.0f});
    world.Add(object);
    return object;
}

std::shared_ptr<GameObject> MakeButton(World& world, GameObject* parent,
                                       const char* name, int order) {
    auto object = std::make_shared<GameObject>(name);
    auto* rect = object->AddComponent<RectTransform>();
    rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetSizeDelta({200.0f, 80.0f});
    auto* button = object->AddComponent<UIButton>();
    button->SetSortingOrder(order);
    object->SetParent(parent);
    world.Add(object);
    return object;
}
} // namespace

TEST_CASE("UICanvas scale-with-screen-size uses width-height match") {
    UICanvas canvas;
    CHECK(canvas.GetReferenceResolution() == Vector2(800.0f, 600.0f));
    CHECK(canvas.GetMatchWidthOrHeight() == doctest::Approx(0.5f));
    CHECK(canvas.ScaleFactor({800.0f, 600.0f}) == doctest::Approx(1.0f));
    CHECK(canvas.ScaleFactor({1600.0f, 1200.0f}) == doctest::Approx(2.0f));
    CHECK(canvas.ScaleFactor({1600.0f, 600.0f}) == doctest::Approx(std::sqrt(2.0f)));
}

TEST_CASE("RectTransform resolves anchors, pivot, nesting, and viewport scale") {
    World world;
    auto canvas = MakeCanvas(world);

    auto panel = std::make_shared<GameObject>("Panel");
    auto* panelRect = panel->AddComponent<RectTransform>();
    panelRect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    panelRect->SetPivot({0.5f, 0.5f});
    panelRect->SetAnchoredPosition({10.0f, -20.0f});
    panelRect->SetSizeDelta({200.0f, 100.0f});
    panel->SetParent(canvas.get());
    world.Add(panel);

    AABB panelAtReference = panelRect->GetScreenRect({800.0f, 600.0f});
    CHECK(panelAtReference.x == doctest::Approx(310.0f));
    CHECK(panelAtReference.y == doctest::Approx(230.0f));
    CHECK(panelAtReference.width == doctest::Approx(200.0f));
    CHECK(panelAtReference.height == doctest::Approx(100.0f));

    auto child = std::make_shared<GameObject>("Child");
    auto* childRect = child->AddComponent<RectTransform>();
    childRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    childRect->SetPivot({0.5f, 0.5f});
    childRect->SetSizeDelta({-20.0f, -10.0f});
    child->SetParent(panel.get());
    world.Add(child);

    AABB nested = childRect->GetScreenRect({800.0f, 600.0f});
    CHECK(nested.x == doctest::Approx(320.0f));
    CHECK(nested.y == doctest::Approx(235.0f));
    CHECK(nested.width == doctest::Approx(180.0f));
    CHECK(nested.height == doctest::Approx(90.0f));

    AABB scaled = panelRect->GetScreenRect({1600.0f, 1200.0f});
    CHECK(scaled.x == doctest::Approx(620.0f));
    CHECK(scaled.y == doctest::Approx(460.0f));
    CHECK(scaled.width == doctest::Approx(400.0f));
    CHECK(scaled.height == doctest::Approx(200.0f));
}

TEST_CASE("UIButton gives pointer capture to only the topmost draw-order target") {
    World world;
    auto canvas = MakeCanvas(world);
    auto low = MakeButton(world, canvas.get(), "Low", 1);
    auto high = MakeButton(world, canvas.get(), "High", 2);

    int lowClicks = 0;
    int highClicks = 0;
    low->GetComponent<UIButton>()->SetOnClick([&] { ++lowClicks; });
    high->GetComponent<UIButton>()->SetOnClick([&] { ++highClicks; });

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    CHECK_FALSE(low->GetComponent<UIButton>()->IsPressed());
    CHECK(high->GetComponent<UIButton>()->IsPressed());

    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, false, false, true, true});
    CHECK(lowClicks == 0);
    CHECK(highClicks == 1);
    CHECK(high->GetComponent<UIButton>()->WasClickedThisFrame());

    // Click edge is cleared on the following input frame.
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, false, false, false, true});
    CHECK_FALSE(high->GetComponent<UIButton>()->WasClickedThisFrame());
}

TEST_CASE("UIButton release outside its captured rect does not click") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    int clicks = 0;
    buttonObject->GetComponent<UIButton>()->SetOnClick([&] { ++clicks; });

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{10.0f, 10.0f}, false, false, true, true});
    CHECK(clicks == 0);
    CHECK_FALSE(buttonObject->GetComponent<UIButton>()->WasClickedThisFrame());
}

TEST_CASE("UIButton invalid pointer immediately releases capture") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    auto* button = buttonObject->GetComponent<UIButton>();
    int clicks = 0;
    button->SetOnClick([&] { ++clicks; });

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f},
        {{400.0f, 300.0f}, true, true, false, true});
    REQUIRE(button->IsPressed());

    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{}, false, false, false, false});
    CHECK_FALSE(button->IsPressed());
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f},
        {{400.0f, 300.0f}, false, false, true, true});
    CHECK(clicks == 0);
}

TEST_CASE("UI ignores disabled canvases and inactive ancestors") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    auto* button = buttonObject->GetComponent<UIButton>();

    canvas->GetComponent<UICanvas>()->SetEnabled(false);
    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    CHECK_FALSE(button->IsPressed());
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == nullptr);

    canvas->GetComponent<UICanvas>()->SetEnabled(true);
    canvas->SetActive(false);
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    CHECK_FALSE(button->IsPressed());
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == nullptr);
}

TEST_CASE("UI hit testing selects only visible UI components") {
    World world;
    auto canvas = MakeCanvas(world);
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == nullptr);

    auto image = std::make_shared<GameObject>("Image");
    auto* rect = image->AddComponent<RectTransform>();
    rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    rect->SetSizeDelta({100.0f, 100.0f});
    image->AddComponent<UIImage>();
    image->SetParent(canvas.get());
    world.Add(image);
    CHECK(UISystem::Get().HitTest(world, {800.0f, 600.0f}, {400.0f, 300.0f}) == image.get());
}

TEST_CASE("UIButton disabling clears transient pointer state") {
    World world;
    auto canvas = MakeCanvas(world);
    auto buttonObject = MakeButton(world, canvas.get(), "Button", 0);
    auto* button = buttonObject->GetComponent<UIButton>();
    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    REQUIRE(button->IsPressed());
    button->SetInteractable(false);
    CHECK_FALSE(button->IsPressed());
    CHECK_FALSE(button->IsHovered());
    CHECK_FALSE(button->WasClickedThisFrame());
}

TEST_CASE("UIButton callback may clear its world safely") {
    World world;
    auto canvas = MakeCanvas(world);
    auto high = MakeButton(world, canvas.get(), "High", 2);
    auto low = MakeButton(world, canvas.get(), "Low", 1);
    bool clicked = false;
    high->GetComponent<UIButton>()->SetOnClick([&] {
        clicked = true;
        world.Clear();
    });
    // Leave the World as the sole owner so Clear() destroys the callback target.
    canvas.reset();
    high.reset();
    low.reset();

    UISystem::Get().ResetPointerCapture();
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, true, true, false, true});
    UISystem::Get().ProcessInput(
        world, {800.0f, 600.0f}, {{400.0f, 300.0f}, false, false, true, true});
    CHECK(clicked);
    CHECK(world.Objects().empty());
}

TEST_CASE("UI components serialize through the scene component contract") {
    RegisterBuiltinComponents();
    auto canvas = std::make_shared<GameObject>("Canvas");
    canvas->AddComponent<UICanvas>()->SetReferenceResolution({1920.0f, 1080.0f});
    auto* rect = canvas->AddComponent<RectTransform>();
    rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    auto* image = canvas->AddComponent<UIImage>();
    image->SetTextureGuid("0123456789abcdef0123456789abcdef");
    image->SetTint({0.1f, 0.2f, 0.3f, 0.4f});
    auto* label = canvas->AddComponent<UILabel>();
    label->SetText("한글 타이틀");
    label->SetFontGuid("fedcba9876543210fedcba9876543210");
    auto* button = canvas->AddComponent<UIButton>();
    button->SetInteractable(false);

    std::vector<std::shared_ptr<GameObject>> source{canvas};
    const auto json = SceneSerializer::SerializeScene(source, "UI");
    std::vector<std::shared_ptr<GameObject>> restored;
    REQUIRE(SceneSerializer::DeserializeScene(json, restored));
    REQUIRE(restored.size() == 1);
    CHECK(restored[0]->GetComponent<UICanvas>()->GetReferenceResolution() ==
          Vector2(1920.0f, 1080.0f));
    CHECK(restored[0]->GetComponent<UIImage>()->GetTextureGuid() ==
          "0123456789abcdef0123456789abcdef");
    CHECK(restored[0]->GetComponent<UILabel>()->GetText() == "한글 타이틀");
    CHECK_FALSE(restored[0]->GetComponent<UIButton>()->IsInteractable());
}

// ── UILabel schema 2 (Task 8.1) ──────────────────────────────────────────────
// 8.2 이전에는 어떤 production 소비자도 부분 이관되지 않는다. 여기 있는 것은
// 스키마와 로드 형식 표식뿐이고, 렌더 경로는 legacy 그대로다.

TEST_CASE("legacy UILabel load save does not silently rewrite fontGuid") {
    const nlohmann::json legacy = {
        {"type", "UILabel"}, {"fontGuid", "font-a"}, {"text", "hello"}};
    UILabel label;
    label.Deserialize(legacy);
    CHECK(label.LoadedLegacyFontGuid());
    nlohmann::json saved;
    label.Serialize(saved);
    CHECK(saved["fontGuid"] == "font-a");
    CHECK_FALSE(saved.contains("fontFamilyGuid"));
}

TEST_CASE("legacy UILabel saves exactly the schema 1 key set") {
    // 키 하나라도 새로 새면 migration 전 scene 파일이 조용히 다시 쓰인다.
    // round-trip 동등성이 아니라 문자 그대로의 기대값과 비교해야 그것을 잡는다.
    UILabel label;
    label.Deserialize({{"type", "UILabel"}, {"fontGuid", "font-a"}, {"text", "hello"}});
    nlohmann::json saved;
    label.Serialize(saved);

    nlohmann::json expected;
    expected["text"] = "hello";
    expected["fontGuid"] = "font-a";
    expected["fontSizePx"] = 24.0f;
    expected["lineSpacing"] = 1.2f;
    expected["color"] = {1.0f, 1.0f, 1.0f, 1.0f};
    expected["horizontalAlignment"] = 1;
    expected["verticalAlignment"] = 1;
    expected["sortingOrder"] = 1;
    CHECK(saved == expected);
}

TEST_CASE("legacy UILabel fontGuid reads as an implicit one-face family view") {
    UILabel legacyLabel;
    legacyLabel.Deserialize({{"type", "UILabel"}, {"fontGuid", "font-a"}});
    const UILabel::FontFamilyView legacyView = legacyLabel.ResolveFontFamilyView();
    CHECK(legacyView.implicitOneFace);
    CHECK(legacyView.familyGuid.empty());
    REQUIRE(legacyView.faceFontGuids.size() == 1);
    CHECK(legacyView.faceFontGuids[0] == "font-a");

    UILabel authored;
    authored.SetFontFamilyGuid("family-a");
    const UILabel::FontFamilyView authoredView = authored.ResolveFontFamilyView();
    CHECK_FALSE(authoredView.implicitOneFace);
    CHECK(authoredView.familyGuid == "family-a");
    CHECK(authoredView.faceFontGuids.empty());

    UILabel blank;
    const UILabel::FontFamilyView blankView = blank.ResolveFontFamilyView();
    CHECK_FALSE(blankView.implicitOneFace);
    CHECK(blankView.familyGuid.empty());
    CHECK(blankView.faceFontGuids.empty());
}

TEST_CASE("UILabel defaults preserve legacy non-wrapping layout") {
    // 새 필드의 기본값이 legacy 동작과 같아야 8.2 이전에 렌더 결과가 바뀌지 않는다.
    const UILabel label;
    CHECK(label.GetLocale() == "und");
    CHECK(label.GetBaseDirection() == molga::text::BaseDirection::Auto);
    CHECK(label.GetWrapMode() == molga::text::TextWrapMode::NoWrap);
    CHECK(label.GetOverflowMode() == molga::text::TextOverflowMode::Overflow);
    CHECK(label.GetMaxLines() == 0u);
    CHECK(label.GetFontFamilyGuid().empty());
    CHECK_FALSE(label.LoadedLegacyFontGuid());
}

TEST_CASE("authored UILabel persists every schema 2 bounded field") {
    UILabel label;
    label.SetFontGuid("font-a");
    label.SetFontFamilyGuid("family-a");
    label.SetLocale("ko-KR");
    label.SetBaseDirection(molga::text::BaseDirection::RightToLeft);
    label.SetWrapMode(molga::text::TextWrapMode::Grapheme);
    label.SetOverflowMode(molga::text::TextOverflowMode::Ellipsis);
    label.SetMaxLines(3);
    label.SetFontSizePx(48.0f);
    label.SetLineSpacing(1.5f);
    label.SetColor({0.5f, 0.25f, 0.125f, 1.0f});
    label.SetHorizontalAlignment(UILabel::HorizontalAlignment::Right);
    // 두 정렬을 같은 서수로 두면 저장에서 서로 뒤바뀌어도 문자 그대로의 기대값이
    // 통과한다. 서로 다른 값이어야 두 키가 구별된다.
    label.SetVerticalAlignment(UILabel::VerticalAlignment::Top);
    label.SetSortingOrder(7);
    label.SetText("본문");

    nlohmann::json saved;
    label.Serialize(saved);

    nlohmann::json expected;
    expected["schemaVersion"] = 2;
    expected["text"] = "본문";
    expected["fontGuid"] = "font-a";
    expected["fontFamilyGuid"] = "family-a";
    expected["fontSizePx"] = 48.0f;
    expected["lineSpacing"] = 1.5f;
    expected["color"] = {0.5f, 0.25f, 0.125f, 1.0f};
    expected["locale"] = "ko-KR";
    expected["baseDirection"] = "RTL";
    expected["wrap"] = "Grapheme";
    expected["overflow"] = "Ellipsis";
    expected["maxLines"] = 3;
    expected["horizontalAlignment"] = 2;
    expected["verticalAlignment"] = 0;
    expected["sortingOrder"] = 7;
    CHECK(saved == expected);

    // 기본값과 같은 값만 실어 나르면 필드가 무시되어도 통과한다. 모두 기본값이
    // 아닌 값으로 다시 읽어 들여야 읽기 경로에 증인이 생긴다.
    UILabel reloaded;
    reloaded.Deserialize(saved);
    CHECK_FALSE(reloaded.LoadedLegacyFontGuid());
    CHECK(reloaded.GetFontGuid() == "font-a");
    CHECK(reloaded.GetFontFamilyGuid() == "family-a");
    CHECK(reloaded.GetLocale() == "ko-KR");
    CHECK(reloaded.GetBaseDirection() == molga::text::BaseDirection::RightToLeft);
    CHECK(reloaded.GetWrapMode() == molga::text::TextWrapMode::Grapheme);
    CHECK(reloaded.GetOverflowMode() == molga::text::TextOverflowMode::Ellipsis);
    CHECK(reloaded.GetMaxLines() == 3u);
    CHECK(reloaded.GetText() == "본문");
    CHECK(reloaded.GetFontSizePx() == doctest::Approx(48.0f));
    CHECK(reloaded.GetLineSpacing() == doctest::Approx(1.5f));
    CHECK(reloaded.GetColor() == Color(0.5f, 0.25f, 0.125f, 1.0f));
    CHECK(reloaded.GetHorizontalAlignment() == UILabel::HorizontalAlignment::Right);
    CHECK(reloaded.GetVerticalAlignment() == UILabel::VerticalAlignment::Top);
    CHECK(reloaded.GetSortingOrder() == 7);
}

TEST_CASE("a newly authored UILabel keeps its fontGuid across a save and load") {
    // 8.2 이전의 UI 렌더 경로(UISystem)와 에디터 인스펙터의 폰트 슬롯은 둘 다
    // 직렬화된 fontGuid 키에서만 나온다. schema 2가 그 키를 빼면 새로 만든
    // 라벨은 저장할 때마다 폰트 지목을 잃고 인스펙터에 슬롯조차 생기지 않는다.
    UILabel label;
    REQUIRE_FALSE(label.LoadedLegacyFontGuid());
    label.SetFontGuid("font-a");
    nlohmann::json saved;
    label.Serialize(saved);
    REQUIRE(saved["schemaVersion"] == 2);
    CHECK(saved["fontGuid"] == "font-a");

    UILabel reloaded;
    reloaded.Deserialize(saved);
    CHECK(reloaded.GetFontGuid() == "font-a");
}

TEST_CASE("an authored UILabel family outranks the legacy fontGuid") {
    // 두 값이 모두 있는 상태는 Milestone 15 migration이 만들어 내는 바로 그
    // 상태다. 우선순위가 뒤집히면 이관된 라벨이 계속 legacy face로 셰이핑된다.
    UILabel label;
    label.Deserialize({{"type", "UILabel"}, {"fontGuid", "font-a"}});
    label.SetFontFamilyGuid("family-a");
    REQUIRE(label.GetFontGuid() == "font-a");
    const UILabel::FontFamilyView view = label.ResolveFontFamilyView();
    CHECK(view.familyGuid == "family-a");
    CHECK_FALSE(view.implicitOneFace);
    CHECK(view.faceFontGuids.empty());
}

TEST_CASE("a family-less schema 2 UILabel does not downgrade to the legacy shape") {
    // 표식을 payload가 아니라 저작 상태(빈 family 등)에서 유도하면, 갓 만든
    // 라벨을 저장한 유효한 schema 2 문서가 다시 읽힐 때 legacy로 뒤집히고
    // 그 다음 저장에서 locale/direction/wrap/overflow/maxLines가 사라진다.
    UILabel authored;
    nlohmann::json saved;
    authored.Serialize(saved);
    REQUIRE(saved["schemaVersion"] == 2);
    REQUIRE(saved["fontFamilyGuid"] == "");

    UILabel reloaded;
    reloaded.Deserialize(saved);
    CHECK_FALSE(reloaded.LoadedLegacyFontGuid());
    nlohmann::json resaved;
    reloaded.Serialize(resaved);
    CHECK(resaved == saved);
}

TEST_CASE("a schema 1 UILabel payload restores the legacy state exactly") {
    // 실행 취소는 기존 컴포넌트에 스냅샷을 다시 Deserialize한다. schema 2 전용
    // 필드를 그대로 두면 표식은 legacy인데 family view는 저작된 family를
    // 가리키는, 저장 형식과 메모리가 다른 폰트를 지목하는 상태가 남는다.
    UILabel label;
    label.SetFontFamilyGuid("family-a");
    label.SetLocale("ko-KR");
    label.SetBaseDirection(molga::text::BaseDirection::RightToLeft);
    label.SetWrapMode(molga::text::TextWrapMode::Word);
    label.SetOverflowMode(molga::text::TextOverflowMode::Ellipsis);
    label.SetMaxLines(4);
    REQUIRE_FALSE(label.LoadedLegacyFontGuid());

    label.Deserialize({{"type", "UILabel"}, {"fontGuid", "font-a"}, {"text", "hello"}});
    CHECK(label.LoadedLegacyFontGuid());
    CHECK(label.GetFontFamilyGuid().empty());
    CHECK(label.GetLocale() == "und");
    CHECK(label.GetBaseDirection() == molga::text::BaseDirection::Auto);
    CHECK(label.GetWrapMode() == molga::text::TextWrapMode::NoWrap);
    CHECK(label.GetOverflowMode() == molga::text::TextOverflowMode::Overflow);
    CHECK(label.GetMaxLines() == 0u);
    // 표식과 family view가 같은 폰트를 지목해야 8.2가 저장 형식과 다른 원본을
    // 셰이핑하지 않는다.
    CHECK(label.ResolveFontFamilyView().implicitOneFace);
}

TEST_CASE("a schema 1 UILabel payload ignores stray schema 2 keys") {
    // 버전 문지기가 헐거우면 손으로 병합한 v1 문서의 잔여 키가 legacy 라벨의
    // 레이아웃 정책을 바꾼다. 통과해서는 안 되는 방향의 증인이다.
    UILabel label;
    label.Deserialize({{"type", "UILabel"},
                       {"fontGuid", "font-a"},
                       {"wrap", "Word"},
                       {"overflow", "Clip"},
                       {"maxLines", 4},
                       {"locale", "ko-KR"},
                       {"baseDirection", "RTL"},
                       {"fontFamilyGuid", "family-a"}});
    CHECK(label.GetWrapMode() == molga::text::TextWrapMode::NoWrap);
    CHECK(label.GetOverflowMode() == molga::text::TextOverflowMode::Overflow);
    CHECK(label.GetMaxLines() == 0u);
    CHECK(label.GetLocale() == "und");
    CHECK(label.GetBaseDirection() == molga::text::BaseDirection::Auto);
    CHECK(label.GetFontFamilyGuid().empty());
}

TEST_CASE("a UILabel locale read from disk is normalized like an authored one") {
    // 정규화가 setter에만 있으면 손으로 편집한 scene의 빈 태그가 곧장 분석
    // 계층으로 새어 들어간다. 로드 경로에도 증인이 필요하다.
    UILabel label;
    label.Deserialize({{"type", "UILabel"}, {"schemaVersion", 2}, {"locale", ""}});
    CHECK(label.GetLocale() == "und");
}

TEST_CASE("authoring a family is the explicit migration off the legacy UILabel shape") {
    UILabel label;
    label.Deserialize({{"type", "UILabel"}, {"fontGuid", "font-a"}, {"text", "hello"}});
    REQUIRE(label.LoadedLegacyFontGuid());

    // legacy 키만 만지는 저작은 형식을 바꾸지 않는다.
    label.SetText("바뀐 본문");
    label.SetSortingOrder(4);
    CHECK(label.LoadedLegacyFontGuid());
    nlohmann::json stillLegacy;
    label.Serialize(stillLegacy);
    CHECK(stillLegacy["fontGuid"] == "font-a");
    CHECK_FALSE(stillLegacy.contains("schemaVersion"));

    // family를 저작하는 것만이 legacy 표현을 대체하는 값을 공급한다.
    label.SetFontFamilyGuid("family-a");
    CHECK_FALSE(label.LoadedLegacyFontGuid());
    nlohmann::json migrated;
    label.Serialize(migrated);
    CHECK(migrated["schemaVersion"] == 2);
    CHECK(migrated["fontFamilyGuid"] == "family-a");
    CHECK(migrated["text"] == "바뀐 본문");
    // legacy 지목은 schema 2에서도 남는다. 8.2 이전의 렌더 경로와 인스펙터 폰트
    // 슬롯이 이 키만 보므로, 이관이 그것을 지우면 라벨이 폰트를 잃는다.
    CHECK(migrated["fontGuid"] == "font-a");
}

TEST_CASE("UILabel persists every enum value as its documented token") {
    // 표를 한 값만 써 보면 나머지 항목은 잘못 적혀도 살아남는다. 세 열거의
    // 모든 값이 쓰기와 읽기 양쪽을 통과해야 표가 검증된다.
    struct DirectionCase {
        molga::text::BaseDirection value;
        const char* token;
    };
    const DirectionCase directions[] = {
        {molga::text::BaseDirection::Auto, "Auto"},
        {molga::text::BaseDirection::LeftToRight, "LTR"},
        {molga::text::BaseDirection::RightToLeft, "RTL"}};
    for (const auto& testCase : directions) {
        CAPTURE(testCase.token);
        UILabel label;
        label.SetFontFamilyGuid("family-a");
        label.SetBaseDirection(testCase.value);
        nlohmann::json saved;
        label.Serialize(saved);
        CHECK(saved.at("baseDirection") == testCase.token);
        UILabel reloaded;
        reloaded.Deserialize(saved);
        CHECK(reloaded.GetBaseDirection() == testCase.value);
    }

    struct WrapCase {
        molga::text::TextWrapMode value;
        const char* token;
    };
    const WrapCase wraps[] = {{molga::text::TextWrapMode::NoWrap, "NoWrap"},
                              {molga::text::TextWrapMode::Word, "Word"},
                              {molga::text::TextWrapMode::Grapheme, "Grapheme"}};
    for (const auto& testCase : wraps) {
        CAPTURE(testCase.token);
        UILabel label;
        label.SetFontFamilyGuid("family-a");
        label.SetWrapMode(testCase.value);
        nlohmann::json saved;
        label.Serialize(saved);
        CHECK(saved.at("wrap") == testCase.token);
        UILabel reloaded;
        reloaded.Deserialize(saved);
        CHECK(reloaded.GetWrapMode() == testCase.value);
    }

    struct OverflowCase {
        molga::text::TextOverflowMode value;
        const char* token;
    };
    const OverflowCase overflows[] = {
        {molga::text::TextOverflowMode::Overflow, "Overflow"},
        {molga::text::TextOverflowMode::Clip, "Clip"},
        {molga::text::TextOverflowMode::Ellipsis, "Ellipsis"}};
    for (const auto& testCase : overflows) {
        CAPTURE(testCase.token);
        UILabel label;
        label.SetFontFamilyGuid("family-a");
        label.SetOverflowMode(testCase.value);
        nlohmann::json saved;
        label.Serialize(saved);
        CHECK(saved.at("overflow") == testCase.token);
        UILabel reloaded;
        reloaded.Deserialize(saved);
        CHECK(reloaded.GetOverflowMode() == testCase.value);
    }
}

TEST_CASE("clearing a UILabel family is not a migration and an empty locale is und") {
    UILabel label;
    label.Deserialize({{"type", "UILabel"}, {"fontGuid", "font-a"}});
    REQUIRE(label.LoadedLegacyFontGuid());
    // family를 비우는 것은 legacy 지목을 대체할 값을 공급하지 않는다. 이때
    // 표식을 내려놓으면 fontGuid만 있던 라벨이 폰트 없이 저장된다.
    label.SetFontFamilyGuid("");
    CHECK(label.LoadedLegacyFontGuid());
    CHECK(label.ResolveFontFamilyView().implicitOneFace);

    UILabel authored;
    authored.SetLocale("ko-KR");
    REQUIRE(authored.GetLocale() == "ko-KR");
    // 빈 태그는 "locale 없음"이 아니라 root tailoring이다.
    authored.SetLocale("");
    CHECK(authored.GetLocale() == "und");
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 8.2 Step 6/6a: UILabel은 공유 파이프라인으로만 그려진다
// ═══════════════════════════════════════════════════════════════════════════
//
// 이 케이스가 없으면 UISystem::CollectRender의 새 경로는 smoke e2e에서만
// 간접적으로 지나간다 — 실패했을 때 "무엇이" 깨졌는지 말해 주지 않는 자리다.

namespace {

// 자격 트리 위의 실제 family/폰트 하나. 이름이 아니라 이 상수가 계약이다.
constexpr const char* kUiPrimaryFamilyGuid = "11111111111111111111111111111111";

std::shared_ptr<GameObject> MakeLabel(World& world, GameObject* parent,
                                      const char* text,
                                      const std::string& familyGuid) {
    auto object = std::make_shared<GameObject>("Label");
    auto* rect = object->AddComponent<RectTransform>();
    rect->SetAnchors({0.5f, 0.5f}, {0.5f, 0.5f});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetSizeDelta({300.0f, 100.0f});
    auto* label = object->AddComponent<UILabel>();
    label->SetText(text);
    label->SetFontFamilyGuid(familyGuid);
    label->SetFontSizePx(24.0f);
    object->SetParent(parent);
    world.Add(object);
    return object;
}

}  // namespace

TEST_CASE("UISystem collects labels through the shared text renderer") {
    QualificationAssetTreeFixture tree;
    molga::AssetDatabase database;
    std::string bindError;
    REQUIRE_MESSAGE(
        database.BindFontArtifactStore(
            std::make_shared<const molga::FontArtifactStore>(
                molga::FontArtifactStore::ForProject(tree.ProjectRoot())),
            &bindError),
        bindError);
    database.ScanProject(tree.AssetsRoot());
    REQUIRE(database.Find(std::string(kUiPrimaryFamilyGuid)) != nullptr);

    molga::text::VectorTextDiagnosticSink sink;
    TextRenderer renderer;
    REQUIRE(renderer.Init(database, sink));

    World world;
    auto canvas = MakeCanvas(world);
    MakeLabel(world, canvas.get(), "Ag", kUiPrimaryFamilyGuid);

    molga::RenderQueue queue;
    {
        auto scope = renderer.BeginGlyphCollection(1);
        UISystem::Get().CollectRender(world, {800.0f, 600.0f}, queue, renderer,
                                      sink, TextRasterPolicy{});
    }
    // 두 grapheme, 두 명령. 그리고 그 명령들은 atlas page를 실제로 붙들고
    // 있으므로 셰이핑과 래스터를 정말로 지났다.
    REQUIRE(queue.GetCommands().size() == 2U);
    for (const molga::RenderCommand& command : queue.GetCommands()) {
        CHECK(command.sortKey.cameraPass == 1);
        CHECK(command.isBatchableSprite);
        CHECK(command.resourceLifetimeIdentity != 0U);
        CHECK(static_cast<bool>(command.resourceLifetime));
    }
    // 성공 경로는 조용하다.
    CHECK(sink.Diagnostics().empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 10.2 Step 5b: 고유 크기의 프로덕션 생산자
// ═══════════════════════════════════════════════════════════════════════════
//
// `UILayoutSystem`은 고유 크기를 `UIIntrinsicLayoutRegistry`에서 읽기만 한다.
// 이 케이스가 없으면 그 레지스트리에 게시하는 것은 테스트 헬퍼뿐이고, 실제
// 세션의 모든 UILabel은 content fitter 아래에서 크기 0으로 측정된다 — 스위트는
// 손으로 먹인 값 위에서 초록인 채로.
TEST_CASE("a rendered label publishes its intrinsic size to layout") {
    QualificationAssetTreeFixture tree;
    molga::AssetDatabase database;
    std::string bindError;
    REQUIRE_MESSAGE(
        database.BindFontArtifactStore(
            std::make_shared<const molga::FontArtifactStore>(
                molga::FontArtifactStore::ForProject(tree.ProjectRoot())),
            &bindError),
        bindError);
    database.ScanProject(tree.AssetsRoot());

    molga::text::VectorTextDiagnosticSink sink;
    TextRenderer renderer;
    REQUIRE(renderer.Init(database, sink));

    World world;
    auto canvas = MakeCanvas(world);
    auto object = MakeLabel(world, canvas.get(), "Ag Ag Ag Ag",
                            kUiPrimaryFamilyGuid);
    auto* label = object->GetComponent<UILabel>();
    REQUIRE(label != nullptr);
    // 줄바꿈이 켜져 있어야 폭 제약이 실제로 측정을 바꾼다. NoWrap이면 제약을
    // 벗기든 말든 같은 크기가 나오므로 아래 rect 축소 단언이 아무것도 지키지
    // 못한다 — 이 한 줄이 이 케이스를 진짜로 만든다.
    label->SetWrapMode(molga::text::TextWrapMode::Word);
    const auto target = molga::ui::CaptureTarget(world, *label);
    REQUIRE(static_cast<bool>(target));

    const auto epoch = [] {
        return molga::ui::UIRuntimeInvalidationClock::Current()
            .semanticDirtyGeneration;
    };
    const auto collect = [&] {
        molga::RenderQueue queue;
        auto scope = renderer.BeginGlyphCollection(1);
        UISystem::Get().CollectRender(world, {800.0f, 600.0f}, queue, renderer,
                                      sink, TextRasterPolicy{});
    };

    // 배치를 지나기 전에는 아무도 게시하지 않았다.
    REQUIRE_FALSE(molga::ui::UIIntrinsicLayoutRegistry::Get().Find(target));

    collect();
    const auto published =
        molga::ui::UIIntrinsicLayoutRegistry::Get().Find(target);
    REQUIRE(published);
    // 실제로 셰이핑을 지난 크기다. 0이면 소비자 쪽 fitter가 라벨을 접는다.
    CHECK(published->intrinsicSize.width.Raw() > 0);
    CHECK(published->intrinsicSize.height.Raw() > 0);
    CHECK_FALSE(published->contentIdentity.empty());

    // 값이 수렴했다. 같은 프레임을 다시 그려도 세대가 움직이지 않아야
    // 변경 없는 프레임의 빠른 경로가 살아남는다.
    const auto settled = epoch();
    collect();
    CHECK(epoch() == settled);

    // 여기가 이 케이스의 핵심이다. 렌더 요청은 현재 RectTransform을 제약으로
    // 싣고 있다. 그 결과를 고유 크기로 게시하면 fitter가 rect를 바꿀 때마다
    // 값이 흔들려 세대가 매 프레임 오른다. 고유 크기는 rect와 무관해야 한다.
    auto* rect = object->GetComponent<RectTransform>();
    REQUIRE(rect != nullptr);
    // 한 낱말만 겨우 들어가는 폭. 제약 실린 배치를 게시하는 구현은 여기서
    // 접힌 폭을 고유 크기로 내놓으므로 아래 두 단언이 무너진다.
    rect->SetSizeDelta({40.0f, 100.0f});
    collect();
    const auto afterShrink =
        molga::ui::UIIntrinsicLayoutRegistry::Get().Find(target);
    REQUIRE(afterShrink);
    CHECK(afterShrink->intrinsicSize == published->intrinsicSize);
    CHECK(afterShrink->contentIdentity == published->contentIdentity);

    // 반대로 내용이 달라지면 정체성과 크기가 함께 움직인다. 두 단언이 같이
    // 서야 정체성이 내용에서 나온 값이라는 뜻이다.
    label->SetText("Ag Ag Ag Ag Ag Ag Ag Ag");
    collect();
    const auto afterEdit =
        molga::ui::UIIntrinsicLayoutRegistry::Get().Find(target);
    REQUIRE(afterEdit);
    CHECK(afterEdit->contentIdentity != published->contentIdentity);
    CHECK(afterEdit->intrinsicSize.width.Raw() >
          published->intrinsicSize.width.Raw());
}

// 반대편: family를 지목하지 않은 라벨은 명령을 만들지 못하고, 그 사실이 진단에
// 남는다. 이 케이스가 없으면 위 케이스는 "언제나 두 명령"과 구별되지 않는다.
TEST_CASE("a UILabel with no font reaches tofu and a typed diagnostic") {
    QualificationAssetTreeFixture tree;
    molga::AssetDatabase database;
    std::string bindError;
    REQUIRE_MESSAGE(
        database.BindFontArtifactStore(
            std::make_shared<const molga::FontArtifactStore>(
                molga::FontArtifactStore::ForProject(tree.ProjectRoot())),
            &bindError),
        bindError);
    database.ScanProject(tree.AssetsRoot());

    molga::text::VectorTextDiagnosticSink sink;
    TextRenderer renderer;
    REQUIRE(renderer.Init(database, sink));

    World world;
    auto canvas = MakeCanvas(world);
    MakeLabel(world, canvas.get(), "Ag", "missing-family");

    molga::RenderQueue queue;
    {
        auto scope = renderer.BeginGlyphCollection(1);
        UISystem::Get().CollectRender(world, {800.0f, 600.0f}, queue, renderer,
                                      sink, TextRasterPolicy{});
    }
    REQUIRE(queue.GetCommands().size() == 2U);
    for (const molga::RenderCommand& command : queue.GetCommands()) {
        // 절차적 두부: 유효하지 않은 핸들, page 지분 없음.
        CHECK_FALSE(static_cast<bool>(command.batchKey.texture));
        CHECK(command.resourceLifetimeIdentity == 0U);
    }
    CHECK_FALSE(sink.Diagnostics().empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 6a: 라벨의 glyph가 실제로 그 RectTransform 자리에 놓인다
// ═══════════════════════════════════════════════════════════════════════════
//
// UISystem::CollectRender의 두 줄 — layoutToOutput.tx/ty = rect.x/rect.y —
// 은 프로덕션에서 UI 상태로부터 텍스트 affine을 만드는 유일한 자리다.
// test_text의 affine 단언들은 훌륭하지만 전부 픽스처가 TextCollectContext를
// 직접 세우므로 이 자리를 지나지 않고, 위의 두 라벨 케이스는 명령 수와
// 정렬 키와 page 지분만 본다. 그래서 그 두 줄을 0으로 만들어도 — 모든
// 라벨이 캔버스 원점에 그려져도 — 아무 단언도 움직이지 않았다.
//
// 여기서는 실제 UISystem 경로로 수집하고, 나온 정점을 rect가 말하는 자리와
// 맞춰 본다.

namespace {

// 새 케이스들이 함께 쓰는 준비. 위 두 케이스의 본문은 손대지 않는다.
struct UiTextFixture {
    QualificationAssetTreeFixture tree;
    molga::AssetDatabase database;
    molga::text::VectorTextDiagnosticSink sink;
    TextRenderer renderer;

    UiTextFixture() {
        std::string bindError;
        REQUIRE_MESSAGE(
            database.BindFontArtifactStore(
                std::make_shared<const molga::FontArtifactStore>(
                    molga::FontArtifactStore::ForProject(tree.ProjectRoot())),
                &bindError),
            bindError);
        database.ScanProject(tree.AssetsRoot());
        REQUIRE(database.Find(std::string(kUiPrimaryFamilyGuid)) != nullptr);
        REQUIRE(renderer.Init(database, sink));
    }
};

struct QuadBounds {
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};

QuadBounds BoundsOf(const std::vector<molga::RenderCommand>& commands) {
    REQUIRE_FALSE(commands.empty());
    QuadBounds bounds{commands.front().vertices[0].x,
                      commands.front().vertices[0].y,
                      commands.front().vertices[0].x,
                      commands.front().vertices[0].y};
    for (const molga::RenderCommand& command : commands) {
        for (const molga::Vertex2D& vertex : command.vertices) {
            bounds.minX = std::min(bounds.minX, vertex.x);
            bounds.minY = std::min(bounds.minY, vertex.y);
            bounds.maxX = std::max(bounds.maxX, vertex.x);
            bounds.maxY = std::max(bounds.maxY, vertex.y);
        }
    }
    return bounds;
}

}  // namespace

TEST_CASE("UI label glyphs land at the rect the label's RectTransform names") {
    UiTextFixture f;
    World world;
    auto canvas = MakeCanvas(world);
    auto label = MakeLabel(world, canvas.get(), "Ag", kUiPrimaryFamilyGuid);
    auto* rectTransform = label->GetComponent<RectTransform>();
    REQUIRE(rectTransform != nullptr);

    const Vector2 viewport{800.0f, 600.0f};
    std::uint64_t frame = 0;
    const auto collect = [&](molga::RenderQueue& queue) {
        auto scope = f.renderer.BeginGlyphCollection(++frame);
        UISystem::Get().CollectRender(world, viewport, queue, f.renderer,
                                      f.sink, TextRasterPolicy{});
    };

    // 원점에서 떨어진 rect. 이 값이 0이면 아래 등식은 아무것도 말하지 않는다.
    const AABB offOrigin = rectTransform->GetScreenRect(viewport);
    REQUIRE(offOrigin.x > 1.0f);
    REQUIRE(offOrigin.y > 1.0f);
    molga::RenderQueue offOriginQueue;
    collect(offOriginQueue);
    const std::vector<molga::RenderCommand> atRect =
        offOriginQueue.GetCommands();
    REQUIRE(atRect.size() == 2U);

    // 같은 라벨을 캔버스 원점으로 옮긴다. 그러면 논리 배치 좌표가 곧 출력
    // 좌표이므로, 위 수집이 어디에 있어야 하는지를 폰트와 무관하게 말해 준다.
    rectTransform->SetAnchoredPosition({-offOrigin.x, -offOrigin.y});
    const AABB atOrigin = rectTransform->GetScreenRect(viewport);
    REQUIRE(atOrigin.x == doctest::Approx(0.0f));
    REQUIRE(atOrigin.y == doctest::Approx(0.0f));
    REQUIRE(atOrigin.width == doctest::Approx(offOrigin.width));
    REQUIRE(atOrigin.height == doctest::Approx(offOrigin.height));
    molga::RenderQueue originQueue;
    collect(originQueue);
    const std::vector<molga::RenderCommand> atCanvasOrigin =
        originQueue.GetCommands();
    REQUIRE(atCanvasOrigin.size() == atRect.size());

    // 정점 하나하나가 정확히 rect의 원점만큼 떨어져 있다.
    for (std::size_t index = 0; index < atRect.size(); ++index) {
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            CHECK(atRect[index].vertices[corner].x ==
                  doctest::Approx(atCanvasOrigin[index].vertices[corner].x +
                                  offOrigin.x));
            CHECK(atRect[index].vertices[corner].y ==
                  doctest::Approx(atCanvasOrigin[index].vertices[corner].y +
                                  offOrigin.y));
        }
    }

    // 위 등식만으로는 두 수집이 나란히 어긋나는 구현을 배제하지 못한다(tx에
    // 같은 상수를 더하면 등식은 그대로 성립한다). 그래서 절대 위치도 본다:
    // glyph는 자기 rect 안에 있다. 1px 여유는 pen 왼쪽에서 시작하는 잉크를
    // 위한 것이고 — 그것을 폰트에서 되짚으면 이 단언이 폰트의 성질이 된다 —
    // 250px 어긋남 앞에서는 아무것도 가리지 않는다.
    const QuadBounds placed = BoundsOf(atRect);
    CHECK(placed.minX >= offOrigin.x - 1.0f);
    CHECK(placed.maxX <= offOrigin.x + offOrigin.width);
    CHECK(placed.minY >= offOrigin.y - 1.0f);
    CHECK(placed.maxY <= offOrigin.y + offOrigin.height);

    // 그리고 두 수집이 실제로 다른 자리에 있었다. 같았다면 위 등식은
    // "0을 더한 것과 같다"만 말한다.
    const QuadBounds atZero = BoundsOf(atCanvasOrigin);
    CHECK(placed.minX > atZero.minX + 1.0f);
    CHECK(placed.minY > atZero.minY + 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 6: UI 텍스트는 호출자의 래스터 정책으로 래스터된다
// ═══════════════════════════════════════════════════════════════════════════
//
// 위 두 라벨 케이스는 둘 다 기본 TextRasterPolicy{}를 넘긴다. 그러면 인자와
// UISystem이 문맥에 싣는 필드가 언제나 같은 값이므로, 문맥 쪽을 상수로
// 바꿔치기해도 아무 단언도 움직이지 않는다 — HiDPI 화면의 모든 라벨이 1x로
// 래스터되어도 조용하다. Task 8.1의 "서로 바꿔치기 가능한 값" 그 모양이다.
//
// 설계 결정 2에 따라 정책은 키의 배율 자리가 아니라 pixelSize로만 나타난다:
// pixelSize = roundHalfAway(fontSize.Raw() * rasterScaleKey / 4096).

TEST_CASE("UI text rasterizes at the policy the caller supplies") {
    UiTextFixture f;
    World world;
    auto canvas = MakeCanvas(world);
    MakeLabel(world, canvas.get(), "Ag", kUiPrimaryFamilyGuid);

    const Vector2 viewport{800.0f, 600.0f};
    std::uint64_t frame = 0;
    const auto collectAt = [&](const TextRasterPolicy& policy) {
        molga::RenderQueue queue;
        {
            auto scope = f.renderer.BeginGlyphCollection(++frame);
            UISystem::Get().CollectRender(world, viewport, queue, f.renderer,
                                          f.sink, policy);
        }
        REQUIRE(queue.GetCommands().size() == 2U);
        return f.renderer.GlyphAtlas().LastUploadedKeyForTest();
    };

    // MakeLabel의 24px: 24*64 * 64 / 4096 = 24.
    const molga::GlyphAtlasKey at1x = collectAt(TextRasterPolicy{});
    CHECK(at1x.pixelSize == 24);

    TextRasterPolicy hiDpi;
    hiDpi.rasterScaleKey = 128;  // 2x
    const molga::GlyphAtlasKey at2x = collectAt(hiDpi);
    // 24*64 * 128 / 4096 = 48. 정책을 무시하는 구현은 여기서도 24를 낸다.
    CHECK(at2x.pixelSize == 48);
    // 같은 glyph의 두 래스터 높이다. 다른 glyph를 본 것이 아니다.
    CHECK(at1x.glyphId == at2x.glyphId);
    CHECK(at1x.fontGuid == at2x.fontGuid);
    // 설계 결정 2: 배율 자리는 두 경우 모두 항등원이다.
    CHECK(at1x.rasterScaleKey == 64);
    CHECK(at2x.rasterScaleKey == 64);
}
