#include "doctest.h"

#include "Core/PathService.h"
#include "Core/PrefabRegistry.h"
#include "Core/PrefabUtil.h"
#include "Core/SceneSerializer.h"
#include "ECS/BuiltinComponents.h"
// ComponentFactory.h touches GameObject members inline, so the definition has
// to arrive first.
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"
#include "ECS/SceneObjectRef.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/TextRenderer2D.h"
#include "ECS/Components/UIAccessibility.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIComponent.h"
#include "ECS/Components/UIContentSizeFitter.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Components/UILayoutElement.h"
#include "ECS/Components/UILayoutGroup.h"
#include "ECS/Components/UIMask.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/Components/UISelectable.h"
#include "ECS/Components/UITextInput.h"
#include "Text/TextDiagnostic.h"
#include "UI/UINavigationTypes.h"
#include "UI/UIRuntimeInvalidation.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

using molga::text::TextDiagnosticCode;
using molga::ui::ScopedUIRuntimeGenerationForTesting;
using molga::ui::UIRuntimeGenerationKind;
using molga::ui::UIRuntimeInvalidationClock;

// 이 파일은 프로세스 전역 무효화 시계를 만지므로 SUBCASE를 하나도 쓰지 않는다.
// 전역 상태를 다시 세우는 SUBCASE는 실패가 아니라 무한 재실행으로 끝난다
// (tests/test_game_builder.cpp:197의 경고와 같은 함정).

namespace {

std::set<std::string> JsonObjectKeys(const nlohmann::json& value) {
    REQUIRE(value.is_object());
    std::set<std::string> keys;
    for (auto it = value.begin(); it != value.end(); ++it) keys.insert(it.key());
    return keys;
}

// 실패를 값으로 돌려주는 fail-closed 헬퍼. "던지지 않았다"와 "다른 코드로
// 던졌다"를 호출부가 구분할 수 있어야, 회귀가 예외를 아예 없애 버려도 통과하지
// 않는다. 호출부는 반드시 engagement를 먼저 확인한다.
template <typename Fn>
std::optional<TextDiagnosticCode> ThrownSchemaCode(Fn&& fn) {
    try {
        fn();
    } catch (const UIComponentSchemaError& error) {
        return error.Code();
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

// 한 컴포넌트의 스키마 계약 네 가지를 한 번에 건다.
//   1) 갓 만든 인스턴스는 문서화된 기본 payload를 정확히 낸다.
//   2) 모든 필드를 저작한 인스턴스는 기대 payload와 문자 그대로 같다.
//      (round-trip 동등성만 보면 직렬화/역직렬화가 같은 버그를 공유할 때 통과한다)
//   3) 저작 payload를 새 인스턴스에 읽히고 다시 쓰면 같은 payload가 나온다.
//   4) 기본 payload를 이미 저작된 인스턴스에 읽히면 저작값이 하나도 남지 않는다.
//      이것이 "기본값 보존"과 "필드를 아예 무시함"을 가르는 유일한 시험이다.
//   5) 키를 하나씩 지운 payload를 저작된 인스턴스에 읽히면 그 키만 문서화된
//      기본값으로 돌아가고 나머지는 payload가 시킨 대로 남는다. 3)과 4)의
//      payload는 모든 키를 명시하므로 fallback 인자가 한 번도 쓰이지 않는다 —
//      그 인자를 "현재 값"으로 바꾼 구현(실행 취소가 지운 글자를 되살리는
//      정확한 결함)이 3)과 4)만으로는 통과한다.
template <typename ComponentT, typename AuthorFn>
void CheckSchemaContract(const char* name, AuthorFn author,
                         const nlohmann::json& expectedDefault,
                         const nlohmann::json& expectedAuthored) {
    CAPTURE(name);

    ComponentT fresh;
    nlohmann::json defaultEncoded;
    fresh.Serialize(defaultEncoded);
    CHECK(defaultEncoded == expectedDefault);

    ComponentT authored;
    author(authored);
    nlohmann::json encoded;
    authored.Serialize(encoded);
    CHECK(encoded == expectedAuthored);

    ComponentT decoded;
    decoded.Deserialize(expectedAuthored);
    nlohmann::json reencoded;
    decoded.Serialize(reencoded);
    CHECK(reencoded == expectedAuthored);

    ComponentT restored;
    author(restored);
    restored.Deserialize(expectedDefault);
    nlohmann::json restoredEncoded;
    restored.Serialize(restoredEncoded);
    CHECK(restoredEncoded == expectedDefault);

    for (auto it = expectedAuthored.begin(); it != expectedAuthored.end();
         ++it) {
        const std::string key = it.key();
        CAPTURE(key);
        nlohmann::json missing = expectedAuthored;
        missing.erase(key);

        nlohmann::json expected = expectedAuthored;
        expected[key] = expectedDefault.at(key);

        ComponentT target;
        author(target);
        target.Deserialize(missing);
        nlohmann::json targetEncoded;
        target.Serialize(targetEncoded);
        CHECK(targetEncoded == expected);
    }
}

nlohmann::json ParseJson(const char* text) {
    return nlohmann::json::parse(text);
}

// 서브트리를 복제하고 원본 -> 새 id 매핑을 돌려준다.
struct ClonedSubtree {
    std::vector<std::shared_ptr<GameObject>> objects;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* root = nullptr;
};

ClonedSubtree CloneSubtree(GameObject* root) {
    ClonedSubtree cloned;
    cloned.root = SceneSerializer::DeserializeSubtreeRemapped(
        SceneSerializer::SerializeSubtree(root), cloned.objects,
        cloned.idRemap);
    return cloned;
}

unsigned int Remapped(const ClonedSubtree& cloned, unsigned int original) {
    const auto found = cloned.idRemap.find(original);
    REQUIRE(found != cloned.idRemap.end());
    return found->second;
}

// 한 컴포넌트 타입에 대해 생성된 override의 key 집합.
std::set<std::string> ModificationKeys(const nlohmann::json& modifications,
                                       const char* componentType) {
    REQUIRE(modifications.is_array());
    std::set<std::string> keys;
    for (const auto& modification : modifications) {
        if (modification.value("component", std::string{}) != componentType) {
            continue;
        }
        keys.insert(modification.value("key", std::string{}));
    }
    return keys;
}

} // namespace

// ── Step 1a: 저작 상태만 도는 완전한 왕복 표 ─────────────────────────────────

TEST_CASE("UITextInput serializes authored state only") {
    UITextInput input;
    input.SetInitialText(u8"초기값");
    input.SetReadOnly(true);
    input.SetMultiline(true);
    input.SetMaxGraphemes(17);
    input.SetTextViewport(SceneObjectRef{11});
    input.SetRenderedLabel(SceneObjectRef{12});
    input.SetPlaceholderLabel(SceneObjectRef{13});
    nlohmann::json encoded;
    input.Serialize(encoded);
    const std::set<std::string> expectedKeys = {
        "schemaVersion", "initialText", "readOnly", "multiline",
        "maxGraphemes", "contentPolicy", "submitPolicy", "textViewport",
        "renderedLabel", "placeholderLabel", "fontFamilyGuid",
        "paragraphStyle"};
    CHECK(JsonObjectKeys(encoded) == expectedKeys);
    CHECK(encoded["schemaVersion"] == UITextInput::CurrentSchemaVersion);
    CHECK(encoded["initialText"] == u8"초기값");
    CHECK(encoded["textViewport"]["targetId"] == 11);

    encoded["runtimeValue"] = "must-not-round-trip";
    encoded["caret"] = 4;
    encoded["selection"] = nlohmann::json::array({1, 3});
    encoded["composition"] = "stale";
    encoded["blink"] = true;
    UITextInput decoded;
    decoded.Deserialize(encoded);
    nlohmann::json reencoded;
    decoded.Serialize(reencoded);
    CHECK(JsonObjectKeys(reencoded) == expectedKeys);
}

TEST_CASE("every authored UI component round-trips its exact schema payload") {
    CheckSchemaContract<UILayoutElement>(
        "UILayoutElement",
        [](UILayoutElement& element) {
            element.SetHorizontal({2.0f, 6.0f, 1.5f});
            element.SetVertical({3.0f, 4.0f, 0.5f});
            element.SetIgnoreLayout(true);
        },
        ParseJson(R"({"schemaVersion":1,
                      "horizontal":{"minimum":0.0,"preferred":0.0,"flexible":0.0},
                      "vertical":{"minimum":0.0,"preferred":0.0,"flexible":0.0},
                      "ignoreLayout":false})"),
        ParseJson(R"({"schemaVersion":1,
                      "horizontal":{"minimum":2.0,"preferred":6.0,"flexible":1.5},
                      "vertical":{"minimum":3.0,"preferred":4.0,"flexible":0.5},
                      "ignoreLayout":true})"));

    CheckSchemaContract<UIContentSizeFitter>(
        "UIContentSizeFitter",
        [](UIContentSizeFitter& fitter) {
            fitter.SetHorizontalFit(UIFitMode::Min);
            fitter.SetVerticalFit(UIFitMode::Preferred);
        },
        ParseJson(R"({"schemaVersion":1,"horizontalFit":"Unconstrained",
                      "verticalFit":"Unconstrained"})"),
        ParseJson(R"({"schemaVersion":1,"horizontalFit":"Min",
                      "verticalFit":"Preferred"})"));

    CheckSchemaContract<UILayoutGroup>(
        "UILayoutGroup",
        [](UILayoutGroup& group) {
            group.SetMode(UILayoutMode::Grid);
            group.SetPadding(1.0f, 2.0f, 3.0f, 4.0f);
            group.SetSpacingX(5.0f);
            group.SetSpacingY(6.0f);
            group.SetChildHorizontalAlignment(
                molga::text::TextHorizontalAlignment::Right);
            group.SetChildVerticalAlignment(
                molga::text::TextVerticalAlignment::Middle);
            group.SetControlChildWidth(true);
            group.SetControlChildHeight(true);
            group.SetChildForceExpandWidth(true);
            group.SetChildForceExpandHeight(true);
            group.SetCellSize(24.0f, 12.0f);
            group.SetStartCorner(UIGridStartCorner::LowerRight);
            group.SetFillAxis(UIGridFillAxis::Vertical);
            group.SetGridConstraint(UIGridConstraint::FixedColumns);
            group.SetConstraintCount(3);
        },
        ParseJson(R"({"schemaVersion":1,"mode":"Horizontal",
                      "paddingLeft":0.0,"paddingRight":0.0,
                      "paddingTop":0.0,"paddingBottom":0.0,
                      "spacingX":0.0,"spacingY":0.0,
                      "childHorizontalAlignment":"Left",
                      "childVerticalAlignment":"Top",
                      "controlChildWidth":false,"controlChildHeight":false,
                      "childForceExpandWidth":false,
                      "childForceExpandHeight":false,
                      "cellSizeX":100.0,"cellSizeY":100.0,
                      "startCorner":"UpperLeft","fillAxis":"Horizontal",
                      "gridConstraint":"Flexible","constraintCount":1})"),
        ParseJson(R"({"schemaVersion":1,"mode":"Grid",
                      "paddingLeft":1.0,"paddingRight":2.0,
                      "paddingTop":3.0,"paddingBottom":4.0,
                      "spacingX":5.0,"spacingY":6.0,
                      "childHorizontalAlignment":"Right",
                      "childVerticalAlignment":"Middle",
                      "controlChildWidth":true,"controlChildHeight":true,
                      "childForceExpandWidth":true,
                      "childForceExpandHeight":true,
                      "cellSizeX":24.0,"cellSizeY":12.0,
                      "startCorner":"LowerRight","fillAxis":"Vertical",
                      "gridConstraint":"FixedColumns","constraintCount":3})"));

    CheckSchemaContract<UIMask>(
        "UIMask",
        [](UIMask& mask) { mask.SetClipsDescendants(false); },
        ParseJson(R"({"schemaVersion":1,"clipsDescendants":true})"),
        ParseJson(R"({"schemaVersion":1,"clipsDescendants":false})"));

    CheckSchemaContract<UIScrollView>(
        "UIScrollView",
        [](UIScrollView& view) {
            view.SetViewport(SceneObjectRef{21});
            view.SetContent(SceneObjectRef{22});
            view.SetHorizontal(false);
            view.SetVertical(false);
            view.SetElasticity(0.25f);
            view.SetMovement(UIScrollMovement::Elastic);
            view.SetInertia(false);
            view.SetDecelerationRate(0.5f);
            view.SetScrollSensitivity(2.5f);
            view.SetInitialNormalizedX(0.25f);
            view.SetInitialNormalizedY(0.75f);
        },
        ParseJson(R"({"schemaVersion":1,
                      "viewport":{"targetId":0},"content":{"targetId":0},
                      "horizontal":true,"vertical":true,
                      "movement":"Clamped","elasticity":0.125,"inertia":true,
                      "decelerationRate":0.875,"scrollSensitivity":1.0,
                      "initialNormalizedX":0.0,"initialNormalizedY":0.0})"),
        ParseJson(R"({"schemaVersion":1,
                      "viewport":{"targetId":21},"content":{"targetId":22},
                      "horizontal":false,"vertical":false,
                      "movement":"Elastic","elasticity":0.25,"inertia":false,
                      "decelerationRate":0.5,"scrollSensitivity":2.5,
                      "initialNormalizedX":0.25,"initialNormalizedY":0.75})"));

    CheckSchemaContract<UISelectable>(
        "UISelectable",
        [](UISelectable& selectable) {
            selectable.SetInteractable(false);
            selectable.SetNavigationMode(UINavigationMode::Explicit);
            selectable.SetNavigateUp(SceneObjectRef{31});
            selectable.SetNavigateDown(SceneObjectRef{32});
            selectable.SetNavigateLeft(SceneObjectRef{33});
            selectable.SetNavigateRight(SceneObjectRef{34});
        },
        ParseJson(R"({"schemaVersion":1,"interactable":true,
                      "navigationMode":"Auto",
                      "navigateUp":{"targetId":0},"navigateDown":{"targetId":0},
                      "navigateLeft":{"targetId":0},
                      "navigateRight":{"targetId":0}})"),
        ParseJson(R"({"schemaVersion":1,"interactable":false,
                      "navigationMode":"Explicit",
                      "navigateUp":{"targetId":31},
                      "navigateDown":{"targetId":32},
                      "navigateLeft":{"targetId":33},
                      "navigateRight":{"targetId":34}})"));

    // contentPolicy/submitPolicy는 Milestone A에서 열거값이 하나뿐이라
    // "기본값이 아닌 값"이 존재하지 않는다. 두 payload에서 모두 같은 정규
    // 문자열이 나오는 것이 이 단계의 계약이다.
    CheckSchemaContract<UITextInput>(
        "UITextInput",
        [](UITextInput& input) {
            input.SetInitialText(u8"초기값");
            input.SetReadOnly(true);
            input.SetMultiline(true);
            input.SetMaxGraphemes(17);
            input.SetTextViewport(SceneObjectRef{11});
            input.SetRenderedLabel(SceneObjectRef{12});
            input.SetPlaceholderLabel(SceneObjectRef{13});
            input.SetFontFamilyGuid("family-a");
            UIAuthoredParagraphStyle style;
            style.fontSizePx = 32.0f;
            style.lineSpacing = 1.5f;
            style.color = Color{0.25f, 0.5f, 0.75f, 1.0f};
            style.locale = "ko-KR";
            style.baseDirection = molga::text::BaseDirection::RightToLeft;
            style.wrap = molga::text::TextWrapMode::Word;
            style.overflow = molga::text::TextOverflowMode::Ellipsis;
            style.maxLines = 3;
            style.horizontal = molga::text::TextHorizontalAlignment::Right;
            style.vertical = molga::text::TextVerticalAlignment::Middle;
            input.SetParagraphStyle(style);
        },
        ParseJson(u8R"({"schemaVersion":1,"initialText":"","readOnly":false,
                        "multiline":false,"maxGraphemes":0,
                        "contentPolicy":"Any","submitPolicy":"OnEnter",
                        "textViewport":{"targetId":0},
                        "renderedLabel":{"targetId":0},
                        "placeholderLabel":{"targetId":0},
                        "fontFamilyGuid":"",
                        "paragraphStyle":{"fontSizePx":24.0,"lineSpacing":1.25,
                          "color":[1.0,1.0,1.0,1.0],"locale":"und",
                          "baseDirection":"Auto","wrap":"NoWrap",
                          "overflow":"Overflow","maxLines":0,
                          "horizontalAlignment":"Left",
                          "verticalAlignment":"Top"}})"),
        ParseJson(u8R"({"schemaVersion":1,"initialText":"초기값",
                        "readOnly":true,"multiline":true,"maxGraphemes":17,
                        "contentPolicy":"Any","submitPolicy":"OnEnter",
                        "textViewport":{"targetId":11},
                        "renderedLabel":{"targetId":12},
                        "placeholderLabel":{"targetId":13},
                        "fontFamilyGuid":"family-a",
                        "paragraphStyle":{"fontSizePx":32.0,"lineSpacing":1.5,
                          "color":[0.25,0.5,0.75,1.0],"locale":"ko-KR",
                          "baseDirection":"RTL","wrap":"Word",
                          "overflow":"Ellipsis","maxLines":3,
                          "horizontalAlignment":"Right",
                          "verticalAlignment":"Middle"}})"));

    CheckSchemaContract<UIAccessibility>(
        "UIAccessibility",
        [](UIAccessibility& accessibility) {
            accessibility.SetRole(UIAccessibilityRole::Button);
            accessibility.SetName(u8"확인");
            accessibility.SetDescription(u8"폼을 제출한다");
            accessibility.SetHidden(true);
        },
        ParseJson(R"({"schemaVersion":1,"role":"None","name":"",
                      "description":"","hidden":false})"),
        ParseJson(u8R"({"schemaVersion":1,"role":"Button","name":"확인",
                        "description":"폼을 제출한다","hidden":true})"));
}

TEST_CASE("paired UI axis fields are never interchangeable") {
    // 위 표는 모든 bool을 기본값의 반대로 세우므로 축 한 쌍이 통째로 뒤바뀌어도
    // 두 값이 같아 통과한다. Task 8.1이 정확히 그 구멍으로 정렬 두 축이 뒤바뀐
    // 채 통과했다. 여기서만 축마다 다른 값을 준다.
    UILayoutGroup group;
    group.SetControlChildWidth(true);
    group.SetControlChildHeight(false);
    group.SetChildForceExpandWidth(false);
    group.SetChildForceExpandHeight(true);
    nlohmann::json encoded;
    group.Serialize(encoded);
    CHECK(encoded["controlChildWidth"] == true);
    CHECK(encoded["controlChildHeight"] == false);
    CHECK(encoded["childForceExpandWidth"] == false);
    CHECK(encoded["childForceExpandHeight"] == true);

    UILayoutGroup decodedGroup;
    decodedGroup.Deserialize(encoded);
    CHECK(decodedGroup.ControlChildWidth());
    CHECK_FALSE(decodedGroup.ControlChildHeight());
    CHECK_FALSE(decodedGroup.ChildForceExpandWidth());
    CHECK(decodedGroup.ChildForceExpandHeight());

    UIScrollView view;
    view.SetHorizontal(true);
    view.SetVertical(false);
    view.SetInitialNormalizedX(0.25f);
    view.SetInitialNormalizedY(0.75f);
    nlohmann::json viewEncoded;
    view.Serialize(viewEncoded);
    CHECK(viewEncoded["horizontal"] == true);
    CHECK(viewEncoded["vertical"] == false);
    CHECK(viewEncoded["initialNormalizedX"] == 0.25);
    CHECK(viewEncoded["initialNormalizedY"] == 0.75);

    UIScrollView decodedView;
    decodedView.Deserialize(viewEncoded);
    CHECK(decodedView.Horizontal());
    CHECK_FALSE(decodedView.Vertical());
    CHECK(decodedView.InitialNormalizedX() == doctest::Approx(0.25f));
    CHECK(decodedView.InitialNormalizedY() == doctest::Approx(0.75f));

    // horizontal/vertical/inertia는 셋 다 같은 타입의 bool이다. 위 표는 셋을
    // 모두 false로 두므로 둘을 맞바꿔도 통과한다.
    UIScrollView axes;
    axes.SetHorizontal(true);
    axes.SetVertical(false);
    axes.SetInertia(false);
    nlohmann::json axesEncoded;
    axes.Serialize(axesEncoded);
    CHECK(axesEncoded["horizontal"] == true);
    CHECK(axesEncoded["vertical"] == false);
    CHECK(axesEncoded["inertia"] == false);
    UIScrollView decodedAxes;
    decodedAxes.Deserialize(axesEncoded);
    CHECK(decodedAxes.Horizontal());
    CHECK_FALSE(decodedAxes.Vertical());
    CHECK_FALSE(decodedAxes.Inertia());

    // readOnly/multiline도 같은 타입의 bool 한 쌍이고, 위 표에서 둘 다 true다.
    UITextInput input;
    input.SetReadOnly(true);
    input.SetMultiline(false);
    nlohmann::json inputEncoded;
    input.Serialize(inputEncoded);
    CHECK(inputEncoded["readOnly"] == true);
    CHECK(inputEncoded["multiline"] == false);
    UITextInput decodedInput;
    decodedInput.Deserialize(inputEncoded);
    CHECK(decodedInput.ReadOnly());
    CHECK_FALSE(decodedInput.Multiline());

    // 반대 조합도 건다. 한 조합만 있으면 두 setter가 같은 필드를 쓰는 구현이
    // 남는다.
    UITextInput flipped;
    flipped.SetReadOnly(false);
    flipped.SetMultiline(true);
    nlohmann::json flippedEncoded;
    flipped.Serialize(flippedEncoded);
    CHECK(flippedEncoded["readOnly"] == false);
    CHECK(flippedEncoded["multiline"] == true);
    UITextInput decodedFlipped;
    decodedFlipped.Deserialize(flippedEncoded);
    CHECK_FALSE(decodedFlipped.ReadOnly());
    CHECK(decodedFlipped.Multiline());
}

// ── Step 1b: 저작 revision은 감기지 않는다 ───────────────────────────────────

TEST_CASE("UI authored revision exhaustion disables snapshot caching") {
    UILabel label;
    label.SetAuthoredRevisionForTesting(UINT64_MAX);
    label.SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
    CHECK(label.AuthoredRevision() == UINT64_MAX);
    CHECK_FALSE(label.RevisionCacheable());
    CHECK((label.DirtyMask() & UIAllInvalidationBits) ==
          UIAllInvalidationBits);
}

TEST_CASE("an unexhausted UI revision advances and keeps the narrow bit") {
    // 위 케이스만 있으면 "언제나 소진"으로 구현해도 통과한다. 성공 증인.
    UILabel label;
    const std::uint64_t before = label.AuthoredRevision();
    CHECK(before == 1u);
    CHECK(label.DirtyMask() == 0u);
    label.SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
    CHECK(label.RevisionCacheable());
    CHECK(label.AuthoredRevision() == before + 1u);
    CHECK(label.DirtyMask() ==
          static_cast<std::uint8_t>(UIInvalidation::Visual));

    // 같은 정규값을 다시 쓰는 것은 변경이 아니다.
    label.SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
    CHECK(label.AuthoredRevision() == before + 1u);

    label.ClearDirtyMask();
    CHECK(label.DirtyMask() == 0u);
    CHECK(label.AuthoredRevision() == before + 1u);
}

// ── Step 1c: 집계 세대도 감기지 않는다 ───────────────────────────────────────

TEST_CASE("aggregate UI dirty generation never wraps or aliases") {
    ScopedUIRuntimeGenerationForTesting scoped(
        UIRuntimeGenerationKind::SemanticDirty, UINT64_MAX);
    UILabel label;
    label.SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
    const auto generations = UIRuntimeInvalidationClock::Current();
    CHECK(generations.semanticDirtyGeneration == UINT64_MAX);
    CHECK_FALSE(generations.cacheable);
    CHECK_FALSE(label.RevisionCacheable());
}

TEST_CASE("every aggregate UI generation exhausts without wrapping") {
    const UIRuntimeGenerationKind kinds[] = {
        UIRuntimeGenerationKind::SemanticDirty,
        UIRuntimeGenerationKind::ScrollDisplacement,
        UIRuntimeGenerationKind::TextureBinding,
        UIRuntimeGenerationKind::Device};
    const std::function<std::uint64_t(const molga::ui::UIRuntimeGenerationSnapshot&)>
        readers[] = {
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.semanticDirtyGeneration;
            },
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.scrollDisplacementGeneration;
            },
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.textureBindingGeneration;
            },
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.deviceGeneration;
            }};

    for (std::size_t index = 0; index < 4; ++index) {
        CAPTURE(index);
        {
            // 성공 증인: 소진되지 않은 시계는 정확히 candidate + 1을 발급하고
            // 그 값을 그대로 저장한다. 두 취득이 같은 값을 볼 수 없어야 한다.
            ScopedUIRuntimeGenerationForTesting live(kinds[index], 7u);
            const auto first = UIRuntimeInvalidationClock::Advance(kinds[index]);
            REQUIRE(first.has_value());
            CHECK(*first == 8u);
            CHECK(readers[index](UIRuntimeInvalidationClock::Current()) == 8u);
            const auto second = UIRuntimeInvalidationClock::Advance(kinds[index]);
            REQUIRE(second.has_value());
            CHECK(*second == 9u);
            CHECK(*second != *first);
            CHECK(UIRuntimeInvalidationClock::Current().cacheable);
        }
        {
            // 마지막 한 칸: UINT64_MAX를 발급하고 거기서 멈춘다. 0은 어느
            // 단계에서도 관측되지 않는다.
            ScopedUIRuntimeGenerationForTesting last(
                kinds[index], UINT64_MAX - 1u);
            const auto edge = UIRuntimeInvalidationClock::Advance(kinds[index]);
            REQUIRE(edge.has_value());
            CHECK(*edge == UINT64_MAX);
            CHECK(readers[index](UIRuntimeInvalidationClock::Current()) ==
                  UINT64_MAX);
            CHECK_FALSE(UIRuntimeInvalidationClock::Current().cacheable);

            const auto exhausted =
                UIRuntimeInvalidationClock::Advance(kinds[index]);
            CHECK_FALSE(exhausted.has_value());
            CHECK(readers[index](UIRuntimeInvalidationClock::Current()) ==
                  UINT64_MAX);
        }
        {
            // 0은 어느 축에서도 발행되지 않는 값이다. 증가 전 검사가 사라져
            // 1을 발행하기 시작하면 "아직 아무것도 없음"과 첫 세대를 구분할 수
            // 없게 되므로, 0에서 출발한 시계는 아예 발행을 거절한다.
            ScopedUIRuntimeGenerationForTesting zeroed(kinds[index], 0u);
            const auto refused = UIRuntimeInvalidationClock::Advance(kinds[index]);
            CHECK_FALSE(refused.has_value());
            CHECK(readers[index](UIRuntimeInvalidationClock::Current()) == 0u);
        }
        // 스코프를 벗어나면 시계가 복원되어 다음 축이 깨끗한 상태에서 시작한다.
        CHECK(UIRuntimeInvalidationClock::Current().cacheable);
    }
}

TEST_CASE("advancing one aggregate UI generation moves no other axis") {
    // 위 케이스는 쓴 축을 같은 매핑으로 다시 읽으므로, 두 축이 한 카운터를
    // 공유해도 자기 자신과만 비교되어 보이지 않는다. Task 11은 Advance가
    // 돌려준 값을 텍스처 바인딩과 디바이스 세대의 정체성으로 그대로 발행하므로,
    // 두 축이 붙으면 텍스처 재업로드가 디바이스 재생성을 흉내 내게 된다.
    const UIRuntimeGenerationKind kinds[] = {
        UIRuntimeGenerationKind::SemanticDirty,
        UIRuntimeGenerationKind::ScrollDisplacement,
        UIRuntimeGenerationKind::TextureBinding,
        UIRuntimeGenerationKind::Device};
    const std::function<std::uint64_t(const molga::ui::UIRuntimeGenerationSnapshot&)>
        readers[] = {
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.semanticDirtyGeneration;
            },
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.scrollDisplacementGeneration;
            },
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.textureBindingGeneration;
            },
            [](const molga::ui::UIRuntimeGenerationSnapshot& s) {
                return s.deviceGeneration;
            }};

    for (std::size_t moved = 0; moved < 4; ++moved) {
        CAPTURE(moved);
        const auto before = UIRuntimeInvalidationClock::Current();
        const auto issued = UIRuntimeInvalidationClock::Advance(kinds[moved]);
        REQUIRE(issued.has_value());
        const auto after = UIRuntimeInvalidationClock::Current();
        CHECK(readers[moved](after) == readers[moved](before) + 1u);
        CHECK(readers[moved](after) == *issued);
        for (std::size_t other = 0; other < 4; ++other) {
            if (other == moved) continue;
            CAPTURE(other);
            CHECK(readers[other](after) == readers[other](before));
        }
    }
}

// ── Step 1d: 정확한 정책 열거/기본값/문자열 ──────────────────────────────────

static_assert(static_cast<std::uint8_t>(UITextInputContentPolicy::Any) == 0);
static_assert(static_cast<std::uint8_t>(UITextInputSubmitPolicy::OnEnter) == 0);
static_assert(static_cast<std::uint8_t>(UIAccessibilityRole::None) == 0);

// 아래 CHECK들은 ToCanonicalString의 반환값을 리터럴과 직접 비교한다. 반환형이
// const char*였다면 그것은 문자열 비교가 아니라 포인터 비교이고, 두 번역 단위의
// 같은 리터럴이 링커에서 하나로 합쳐질 때만 우연히 통과한다. ASan은 전역마다
// redzone을 붙여 그 합침을 없애므로, 똑같은 소스가 그 구성에서만 실패한다
// (scratch에서 확인: plain/UBSan은 pooled=1, ASan은 pooled=0). 반환형이 문자열
// 뷰여야 == 가 실제로 문자를 비교한다.
template <typename T>
constexpr bool CanonicalStringIsAView =
    std::is_same_v<decltype(ToCanonicalString(std::declval<T>())),
                   std::string_view>;
static_assert(CanonicalStringIsAView<UITextInputContentPolicy>);
static_assert(CanonicalStringIsAView<UITextInputSubmitPolicy>);
static_assert(CanonicalStringIsAView<UIAccessibilityRole>);
static_assert(CanonicalStringIsAView<UILayoutMode>);
static_assert(CanonicalStringIsAView<UIGridConstraint>);
static_assert(CanonicalStringIsAView<UIFitMode>);
static_assert(CanonicalStringIsAView<UIScrollMovement>);
static_assert(CanonicalStringIsAView<UIGridStartCorner>);
static_assert(CanonicalStringIsAView<UIGridFillAxis>);
static_assert(CanonicalStringIsAView<UINavigationMode>);
static_assert(CanonicalStringIsAView<UICanvasScaleMode>);
static_assert(CanonicalStringIsAView<molga::text::BaseDirection>);
static_assert(CanonicalStringIsAView<molga::text::TextWrapMode>);
static_assert(CanonicalStringIsAView<molga::text::TextOverflowMode>);
static_assert(CanonicalStringIsAView<molga::text::TextHorizontalAlignment>);
static_assert(CanonicalStringIsAView<molga::text::TextVerticalAlignment>);

TEST_CASE("Milestone A UI policy strings and defaults are exact") {
    UITextInput input;
    UIAccessibility accessibility;
    CHECK(input.ContentPolicy() == UITextInputContentPolicy::Any);
    CHECK(input.SubmitPolicy() == UITextInputSubmitPolicy::OnEnter);
    CHECK(accessibility.Role() == UIAccessibilityRole::None);
    CHECK(ToCanonicalString(input.ContentPolicy()) == "Any");
    CHECK(ToCanonicalString(input.SubmitPolicy()) == "OnEnter");
    CHECK((AllAccessibilityRoleStrings() ==
           std::vector<std::string>{"None", "Panel", "Label", "Button",
                                    "TextInput", "Image", "ScrollView"}));
}

TEST_CASE("every accessibility role round-trips its exact PascalCase spelling") {
    const auto names = AllAccessibilityRoleStrings();
    REQUIRE(names.size() == 7u);
    for (std::size_t index = 0; index < names.size(); ++index) {
        CAPTURE(names[index]);
        const auto role = static_cast<UIAccessibilityRole>(index);
        CHECK(ToCanonicalString(role) == names[index]);

        UIAccessibility accessibility;
        accessibility.SetRole(role);
        nlohmann::json encoded;
        accessibility.Serialize(encoded);
        CHECK(encoded["role"] == names[index]);

        UIAccessibility decoded;
        decoded.Deserialize(encoded);
        CHECK(decoded.Role() == role);
    }
}

TEST_CASE("UITextInput paragraph tokens match the UILabel tokens exactly") {
    // 두 컴포넌트가 서로 다른 토큰을 쓰기 시작하면 같은 문단 정책이 파일마다
    // 다른 문자열로 남는다. 토큰 표가 각 .cpp에 하나씩 있는 한 이 시험만이
    // 그 표류를 잡는다.
    struct Row {
        molga::text::BaseDirection direction;
        molga::text::TextWrapMode wrap;
        molga::text::TextOverflowMode overflow;
    };
    const Row rows[] = {
        {molga::text::BaseDirection::Auto, molga::text::TextWrapMode::NoWrap,
         molga::text::TextOverflowMode::Overflow},
        {molga::text::BaseDirection::LeftToRight,
         molga::text::TextWrapMode::Word, molga::text::TextOverflowMode::Clip},
        {molga::text::BaseDirection::RightToLeft,
         molga::text::TextWrapMode::Grapheme,
         molga::text::TextOverflowMode::Ellipsis}};
    for (const auto& row : rows) {
        UILabel label;
        label.SetFontFamilyGuid("family-a");
        label.SetBaseDirection(row.direction);
        label.SetWrapMode(row.wrap);
        label.SetOverflowMode(row.overflow);
        nlohmann::json labelJson;
        label.Serialize(labelJson);

        UITextInput input;
        UIAuthoredParagraphStyle style;
        style.baseDirection = row.direction;
        style.wrap = row.wrap;
        style.overflow = row.overflow;
        input.SetParagraphStyle(style);
        nlohmann::json inputJson;
        input.Serialize(inputJson);

        CHECK(inputJson["paragraphStyle"]["baseDirection"] ==
              labelJson["baseDirection"]);
        CHECK(inputJson["paragraphStyle"]["wrap"] == labelJson["wrap"]);
        CHECK(inputJson["paragraphStyle"]["overflow"] == labelJson["overflow"]);
    }
}

TEST_CASE("UITextInput clamps its own font size and line spacing like UILabel") {
    // UILabel의 같은 규칙은 다른 시험이 이미 건다. 이 태스크가 UITextInput.cpp에
    // 두 번째 독립 구현을 두었으므로, 그 사본에도 증인이 필요하다. 상한이 없으면
    // 문서 하나가 512px가 넘는 em을 요구해 아틀라스를 통째로 소진시킨다.
    UITextInput input;
    UIAuthoredParagraphStyle style;
    style.fontSizePx = 4096.0f;
    style.lineSpacing = 0.0f;
    style.locale = "";
    input.SetParagraphStyle(style);
    CHECK(input.ParagraphStyle().fontSizePx == doctest::Approx(512.0f));
    CHECK(input.ParagraphStyle().lineSpacing == doctest::Approx(0.1f));
    // 빈 태그는 "locale 없음"이 아니라 root tailoring 요청이다.
    CHECK(input.ParagraphStyle().locale == "und");

    UIAuthoredParagraphStyle low;
    low.fontSizePx = 0.0f;
    low.lineSpacing = 99.0f;
    input.SetParagraphStyle(low);
    CHECK(input.ParagraphStyle().fontSizePx == doctest::Approx(1.0f));
    CHECK(input.ParagraphStyle().lineSpacing == doctest::Approx(10.0f));

    // 성공 증인: 범위 안의 값은 손대지 않는다.
    UIAuthoredParagraphStyle inRange;
    inRange.fontSizePx = 42.0f;
    inRange.lineSpacing = 1.75f;
    inRange.locale = "ko-KR";
    input.SetParagraphStyle(inRange);
    CHECK(input.ParagraphStyle().fontSizePx == doctest::Approx(42.0f));
    CHECK(input.ParagraphStyle().lineSpacing == doctest::Approx(1.75f));
    CHECK(input.ParagraphStyle().locale == "ko-KR");
}

TEST_CASE("a malformed paragraph color is a typed failure, never a short read") {
    // 길이 검사를 지우면 nlohmann의 const operator[](size_type)가 std::vector의
    // 것으로 넘어가 경계 검사 없이 힙 밖을 읽고, 역직렬화는 쓰레기 float으로
    // "성공"한다. 역직렬화 경계에서 fail-open은 이 하위 시스템이 절대 해서는
    // 안 되는 일이다.
    const std::vector<std::pair<const char*, nlohmann::json>> malformed = {
        {"short array", nlohmann::json::array({1.0, 0.0})},
        {"not an array", nlohmann::json("white")},
        {"string channel", nlohmann::json::array({"1.0", 1.0, 1.0, 1.0})},
        {"null channel", nlohmann::json::array({nullptr, 1.0, 1.0, 1.0})},
    };
    for (const auto& row : malformed) {
        CAPTURE(row.first);
        UITextInput input;
        nlohmann::json payload;
        input.Serialize(payload);
        payload["paragraphStyle"]["color"] = row.second;
        const auto code =
            ThrownSchemaCode([&] { input.Deserialize(payload); });
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }

    // 성공 증인: 네 채널짜리 숫자 배열은 그대로 실린다.
    UITextInput good;
    nlohmann::json payload;
    good.Serialize(payload);
    payload["paragraphStyle"]["color"] =
        nlohmann::json::array({0.25, 0.5, 0.75, 1.0});
    const auto clean = ThrownSchemaCode([&] { good.Deserialize(payload); });
    CHECK_FALSE(clean.has_value());
    CHECK(good.ParagraphStyle().color.r == doctest::Approx(0.25f));
    CHECK(good.ParagraphStyle().color.g == doctest::Approx(0.5f));
    CHECK(good.ParagraphStyle().color.b == doctest::Approx(0.75f));
    CHECK(good.ParagraphStyle().color.a == doctest::Approx(1.0f));
}

TEST_CASE("a single authored field change is never swallowed by an equality check") {
    // 아래 두 기록은 필드가 여럿인 값 타입이고, 변경 판정을 위한 Same()이
    // 지나치게 관대하면 setter가 조기 반환해 저작된 변경을 조용히 버린다.
    // 왕복 표는 언제나 여러 필드를 한꺼번에 바꾸므로 빠진 항 하나를 볼 수 없다.
    // 기준값에서 정확히 한 필드만 옮긴다. 정규화가 항등이 되도록 preferred는
    // 언제나 minimum 이상으로 고른다 — 그러지 않으면 한 필드를 옮긴 것이
    // 두 필드를 옮긴 것이 되어 다시 단일 항을 시험하지 못한다.
    const UIAxisConstraint base{2.0f, 7.0f, 1.0f};
    struct AxisRow {
        const char* what;
        UIAxisConstraint value;
    };
    const AxisRow axisRows[] = {
        {"minimum only", UIAxisConstraint{3.0f, 7.0f, 1.0f}},
        {"preferred only", UIAxisConstraint{2.0f, 8.0f, 1.0f}},
        {"flexible only", UIAxisConstraint{2.0f, 7.0f, 4.0f}},
    };
    for (const auto& row : axisRows) {
        CAPTURE(row.what);
        UILayoutElement element;
        element.SetHorizontal(base);
        const std::uint64_t revision = element.AuthoredRevision();
        element.SetHorizontal(row.value);
        CHECK(element.AuthoredRevision() == revision + 1u);
        CHECK(element.Horizontal().minimum == doctest::Approx(row.value.minimum));
        CHECK(element.Horizontal().preferred ==
              doctest::Approx(row.value.preferred));
        CHECK(element.Horizontal().flexible ==
              doctest::Approx(row.value.flexible));

        // 같은 값을 다시 쓰는 것은 변경이 아니다.
        element.SetHorizontal(row.value);
        CHECK(element.AuthoredRevision() == revision + 1u);
    }

    const std::vector<std::pair<const char*, std::function<void(UIAuthoredParagraphStyle&)>>>
        styleRows = {
            {"fontSizePx", [](UIAuthoredParagraphStyle& s) { s.fontSizePx = 30.0f; }},
            {"lineSpacing", [](UIAuthoredParagraphStyle& s) { s.lineSpacing = 2.0f; }},
            {"color", [](UIAuthoredParagraphStyle& s) {
                 s.color = Color{0.25f, 0.5f, 0.75f, 1.0f};
             }},
            {"locale", [](UIAuthoredParagraphStyle& s) { s.locale = "ko-KR"; }},
            {"baseDirection", [](UIAuthoredParagraphStyle& s) {
                 s.baseDirection = molga::text::BaseDirection::RightToLeft;
             }},
            {"wrap", [](UIAuthoredParagraphStyle& s) {
                 s.wrap = molga::text::TextWrapMode::Word;
             }},
            {"overflow", [](UIAuthoredParagraphStyle& s) {
                 s.overflow = molga::text::TextOverflowMode::Ellipsis;
             }},
            {"maxLines", [](UIAuthoredParagraphStyle& s) { s.maxLines = 3; }},
            {"horizontal", [](UIAuthoredParagraphStyle& s) {
                 s.horizontal = molga::text::TextHorizontalAlignment::Right;
             }},
            {"vertical", [](UIAuthoredParagraphStyle& s) {
                 s.vertical = molga::text::TextVerticalAlignment::Bottom;
             }},
        };
    for (const auto& row : styleRows) {
        CAPTURE(row.first);
        UITextInput input;
        const std::uint64_t revision = input.AuthoredRevision();
        UIAuthoredParagraphStyle style;
        row.second(style);
        input.SetParagraphStyle(style);
        CHECK(input.AuthoredRevision() > revision);
        nlohmann::json encoded;
        input.Serialize(encoded);
        nlohmann::json baseline;
        UITextInput fresh;
        fresh.Serialize(baseline);
        CHECK(encoded["paragraphStyle"] != baseline["paragraphStyle"]);

        // 그리고 같은 값을 다시 쓰는 것은 변경이 아니다(너무 엄격한 Same()도
        // 결함이다 — 매 프레임 캐시를 무너뜨린다).
        const std::uint64_t settled = input.AuthoredRevision();
        input.SetParagraphStyle(style);
        CHECK(input.AuthoredRevision() == settled);
    }
}

// ── Step 1e: 알 수 없는 정책과 migration ─────────────────────────────────────

TEST_CASE("an unknown UI policy value is a typed deserialize failure") {
    {
        UITextInput input;
        nlohmann::json payload;
        input.Serialize(payload);
        payload["contentPolicy"] = "Decimal";
        const auto code =
            ThrownSchemaCode([&] { input.Deserialize(payload); });
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }
    {
        UITextInput input;
        nlohmann::json payload;
        input.Serialize(payload);
        payload["submitPolicy"] = "OnBlur";
        const auto code =
            ThrownSchemaCode([&] { input.Deserialize(payload); });
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }
    {
        UIAccessibility accessibility;
        nlohmann::json payload;
        accessibility.Serialize(payload);
        payload["role"] = "Unknown";
        const auto code =
            ThrownSchemaCode([&] { accessibility.Deserialize(payload); });
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }
    {
        // 숫자 서수도 암묵 대체가 아니라 같은 실패다.
        UIAccessibility accessibility;
        nlohmann::json payload;
        accessibility.Serialize(payload);
        payload["role"] = 3;
        const auto code =
            ThrownSchemaCode([&] { accessibility.Deserialize(payload); });
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }
    {
        // 성공 증인: 정규 문자열은 던지지 않고 값을 옮긴다. 이 줄이 없으면
        // "언제나 던진다"는 구현도 위 네 블록을 모두 통과한다.
        UIAccessibility accessibility;
        nlohmann::json payload;
        accessibility.Serialize(payload);
        payload["role"] = "ScrollView";
        const auto code =
            ThrownSchemaCode([&] { accessibility.Deserialize(payload); });
        CHECK_FALSE(code.has_value());
        CHECK(accessibility.Role() == UIAccessibilityRole::ScrollView);
    }
}

TEST_CASE("a policy-less legacy payload migrates to the documented defaults") {
    // UITextInput: 두 정책 키가 없는 승인된 payload.
    UITextInput input;
    nlohmann::json payload;
    input.Serialize(payload);
    payload.erase("contentPolicy");
    payload.erase("submitPolicy");
    input.Deserialize(payload);
    CHECK(input.ContentPolicy() == UITextInputContentPolicy::Any);
    CHECK(input.SubmitPolicy() == UITextInputSubmitPolicy::OnEnter);
    nlohmann::json migrated;
    input.Serialize(migrated);
    CHECK(migrated["contentPolicy"] == "Any");
    CHECK(migrated["submitPolicy"] == "OnEnter");

    // UIAccessibility: role 키가 없는 payload는 형제 컴포넌트를 보고 역할을
    // 짐작하지 않고 None이 된다. 이미 Button을 저작한 인스턴스에 읽혀야
    // "보존된 기본값"과 "필드를 아예 무시함"이 구분된다.
    UIAccessibility accessibility;
    accessibility.SetRole(UIAccessibilityRole::Button);
    REQUIRE(accessibility.Role() == UIAccessibilityRole::Button);
    nlohmann::json rolelessPayload;
    accessibility.Serialize(rolelessPayload);
    rolelessPayload.erase("role");
    accessibility.Deserialize(rolelessPayload);
    CHECK(accessibility.Role() == UIAccessibilityRole::None);
    nlohmann::json migratedRole;
    accessibility.Serialize(migratedRole);
    CHECK(migratedRole["role"] == "None");
}

TEST_CASE("an unknown UI policy aborts the whole scene load") {
    RegisterBuiltinComponents();
    const nlohmann::json scene = {
        {"version", "1.0"},
        {"name", "BadPolicy"},
        {"gameObjects",
         {{{"name", "Good"},
           {"id", 71},
           {"parentId", -1},
           {"components",
            {{{"type", "UIAccessibility"},
              {"enabled", true},
              {"schemaVersion", 1},
              {"role", "Panel"},
              {"name", ""},
              {"description", ""},
              {"hidden", false}}}}},
          {{"name", "Bad"},
           {"id", 72},
           {"parentId", -1},
           {"components",
            {{{"type", "UITextInput"},
              {"enabled", true},
              {"schemaVersion", 1},
              {"contentPolicy", "Decimal"}}}}}}}};

    std::vector<std::shared_ptr<GameObject>> restored;
    CHECK_FALSE(SceneSerializer::DeserializeScene(scene, restored));
    // 첫 오브젝트는 완전히 유효했다. 부분 발행이 있었다면 여기 남는다.
    CHECK(restored.empty());

    // 성공 증인: 같은 문서에서 정책만 정규값으로 바꾸면 두 오브젝트가 모두
    // 실린다. 이 줄이 없으면 "언제나 실패"하는 로더도 위를 통과한다.
    nlohmann::json good = scene;
    good["gameObjects"][1]["components"][0]["contentPolicy"] = "Any";
    std::vector<std::shared_ptr<GameObject>> loaded;
    CHECK(SceneSerializer::DeserializeScene(good, loaded));
    CHECK(loaded.size() == 2u);
}

TEST_CASE("an unknown UI policy fails a subtree load closed") {
    // prefab 인스턴스화 경로다. 여기서 예외가 새면 prefab을 만드는 모든 호출부로
    // 그대로 올라가는데, 그중 어느 것도 예외를 기다리지 않는다.
    RegisterBuiltinComponents();
    const nlohmann::json subtree = {
        {"gameObjects",
         {{{"name", "Good"},
           {"id", 1},
           {"parentId", -1},
           {"components",
            {{{"type", "UIAccessibility"},
              {"enabled", true},
              {"role", "Panel"}}}}},
          {{"name", "Bad"},
           {"id", 2},
           {"parentId", 1},
           {"components",
            {{{"type", "UIAccessibility"},
              {"enabled", true},
              {"role", "Nope"}}}}}}}};

    std::vector<std::shared_ptr<GameObject>> objects;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* root = nullptr;
    const auto escaped = ThrownSchemaCode([&] {
        root = SceneSerializer::DeserializeSubtreeRemapped(subtree, objects,
                                                           idRemap);
    });
    CHECK_FALSE(escaped.has_value());
    CHECK(root == nullptr);
    // RollbackGuard가 이미 덧붙인 오브젝트와 idRemap을 되돌린다.
    CHECK(objects.empty());

    // 성공 증인: 정규값으로 바꾸면 두 오브젝트가 모두 실린다.
    nlohmann::json good = subtree;
    good["gameObjects"][1]["components"][0]["role"] = "Label";
    std::vector<std::shared_ptr<GameObject>> loadedObjects;
    std::unordered_map<unsigned int, unsigned int> loadedRemap;
    CHECK(SceneSerializer::DeserializeSubtreeRemapped(good, loadedObjects,
                                                      loadedRemap) != nullptr);
    CHECK(loadedObjects.size() == 2u);
}

TEST_CASE("invalid authored layout values leave the component untouched") {
    UILayoutGroup group;
    const std::uint64_t revision = group.AuthoredRevision();
    const std::vector<std::function<void()>> rejected = {
        [&] { group.SetSpacingX(std::numeric_limits<float>::quiet_NaN()); },
        [&] { group.SetSpacingY(-1.0f); },
        [&] { group.SetPadding(-1.0f, 0.0f, 0.0f, 0.0f); },
        [&] { group.SetCellSize(0.0f, 4.0f); },
        [&] { group.SetCellSize(4.0f, std::numeric_limits<float>::infinity()); },
        [&] { group.SetConstraintCount(0); }};
    for (std::size_t index = 0; index < rejected.size(); ++index) {
        CAPTURE(index);
        const auto code = ThrownSchemaCode(rejected[index]);
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }
    nlohmann::json encoded;
    group.Serialize(encoded);
    CHECK(encoded == ParseJson(R"({"schemaVersion":1,"mode":"Horizontal",
                      "paddingLeft":0.0,"paddingRight":0.0,
                      "paddingTop":0.0,"paddingBottom":0.0,
                      "spacingX":0.0,"spacingY":0.0,
                      "childHorizontalAlignment":"Left",
                      "childVerticalAlignment":"Top",
                      "controlChildWidth":false,"controlChildHeight":false,
                      "childForceExpandWidth":false,
                      "childForceExpandHeight":false,
                      "cellSizeX":100.0,"cellSizeY":100.0,
                      "startCorner":"UpperLeft","fillAxis":"Horizontal",
                      "gridConstraint":"Flexible","constraintCount":1})"));
    CHECK(group.AuthoredRevision() == revision);

    // 성공 증인: 유효한 값은 정확히 한 번 revision을 올린다.
    group.SetSpacingX(5.0f);
    CHECK(group.AuthoredRevision() == revision + 1u);
    CHECK(group.SpacingX() == doctest::Approx(5.0f));
}

TEST_CASE("UILayoutElement canonicalizes each axis independently") {
    UILayoutElement element;
    const std::uint64_t revision = element.AuthoredRevision();
    element.SetHorizontal({-5.0f, -9.0f, -2.0f});
    CHECK(element.Horizontal().minimum == doctest::Approx(0.0f));
    CHECK(element.Horizontal().preferred == doctest::Approx(0.0f));
    CHECK(element.Horizontal().flexible == doctest::Approx(0.0f));
    // 정규값이 바뀌지 않았으므로 revision도 그대로다.
    CHECK(element.AuthoredRevision() == revision);

    element.SetVertical({4.0f, 1.0f, 3.0f});
    CHECK(element.Vertical().minimum == doctest::Approx(4.0f));
    // preferred는 정규화된 minimum까지 끌어올려진다.
    CHECK(element.Vertical().preferred == doctest::Approx(4.0f));
    CHECK(element.Vertical().flexible == doctest::Approx(3.0f));
    CHECK(element.Horizontal().minimum == doctest::Approx(0.0f));
    CHECK(element.AuthoredRevision() == revision + 1u);
    CHECK(element.DirtyMask() ==
          static_cast<std::uint8_t>(UIInvalidation::Layout));

    const auto code = ThrownSchemaCode([&] {
        element.SetHorizontal({std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    });
    REQUIRE(code.has_value());
    CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    CHECK(element.AuthoredRevision() == revision + 1u);
}

TEST_CASE("UIScrollView rejects out-of-contract values and normalizes signed zero") {
    UIScrollView view;
    const std::uint64_t revision = view.AuthoredRevision();
    const std::vector<std::function<void()>> rejected = {
        [&] { view.SetInitialNormalizedX(-0.25f); },
        [&] { view.SetInitialNormalizedY(1.5f); },
        [&] { view.SetScrollSensitivity(-1.0f); },
        [&] { view.SetDecelerationRate(-0.5f); },
        [&] { view.SetElasticity(std::numeric_limits<float>::infinity()); }};
    for (std::size_t index = 0; index < rejected.size(); ++index) {
        CAPTURE(index);
        const auto code = ThrownSchemaCode(rejected[index]);
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }
    CHECK(view.AuthoredRevision() == revision);

    // Elastic은 양의 elasticity를 요구한다. 두 setter 모두 그 불변식을 지킨다.
    view.SetElasticity(0.5f);
    view.SetMovement(UIScrollMovement::Elastic);
    CHECK(view.Movement() == UIScrollMovement::Elastic);
    const auto zeroElasticity =
        ThrownSchemaCode([&] { view.SetElasticity(0.0f); });
    REQUIRE(zeroElasticity.has_value());
    CHECK(*zeroElasticity == TextDiagnosticCode::LayoutInvalid);
    CHECK(view.Elasticity() == doctest::Approx(0.5f));

    UIScrollView flat;
    flat.SetElasticity(0.0f);
    const auto elasticSwitch =
        ThrownSchemaCode([&] { flat.SetMovement(UIScrollMovement::Elastic); });
    REQUIRE(elasticSwitch.has_value());
    CHECK(*elasticSwitch == TextDiagnosticCode::LayoutInvalid);
    CHECK(flat.Movement() == UIScrollMovement::Clamped);

    // -0.0은 저장 전에 +0.0으로 정규화된다. 부호 비트가 남으면 같은 위치가
    // 두 개의 서로 다른 캐시 정체성을 만든다.
    //
    // 반드시 0이 아닌 값을 먼저 넣어야 한다. 갓 만든 컴포넌트에 -0.0f를 주면
    // IEEE에서 -0.0f == 0.0f이라 setter의 동일성 검사가 먼저 걸려 저장 자체가
    // 일어나지 않고, 정규화가 통째로 없는 구현도 통과한다.
    UIScrollView zeroed;
    zeroed.SetInitialNormalizedX(0.25f);
    zeroed.SetInitialNormalizedX(-0.0f);
    CHECK_FALSE(std::signbit(zeroed.InitialNormalizedX()));
    zeroed.SetInitialNormalizedY(0.25f);
    zeroed.SetInitialNormalizedY(-0.0f);
    CHECK_FALSE(std::signbit(zeroed.InitialNormalizedY()));
    zeroed.SetScrollSensitivity(2.0f);
    zeroed.SetScrollSensitivity(-0.0f);
    CHECK_FALSE(std::signbit(zeroed.ScrollSensitivity()));
    zeroed.SetDecelerationRate(0.5f);
    zeroed.SetDecelerationRate(-0.0f);
    CHECK_FALSE(std::signbit(zeroed.DecelerationRate()));
    zeroed.SetElasticity(0.5f);
    zeroed.SetElasticity(-0.0f);
    CHECK_FALSE(std::signbit(zeroed.Elasticity()));
    nlohmann::json encoded;
    zeroed.Serialize(encoded);
    CHECK(encoded["initialNormalizedX"].dump() == "0.0");
    CHECK(encoded["initialNormalizedY"].dump() == "0.0");
    CHECK(encoded["scrollSensitivity"].dump() == "0.0");
    CHECK(encoded["decelerationRate"].dump() == "0.0");
    CHECK(encoded["elasticity"].dump() == "0.0");
}

TEST_CASE("UILayoutGroup and UICanvas normalize signed zero before storing") {
    // -0.0f는 +0.0f와 값이 같아 setter의 동일성 검사를 통과하지 못하지만,
    // 직전 값이 0이 아니면 그대로 저장된다. 그러면 논리적으로 같은 저작 상태가
    // 편집 순서에 따라 "-0.0" 또는 "0.0"으로 저장돼, Milestone 18이 증명해야
    // 하는 바이트 동일 정규 JSON이 깨진다.
    UILayoutGroup group;
    group.SetSpacingX(5.0f);
    group.SetSpacingX(-0.0f);
    group.SetSpacingY(5.0f);
    group.SetSpacingY(-0.0f);
    group.SetPadding(5.0f, 5.0f, 5.0f, 5.0f);
    group.SetPadding(-0.0f, -0.0f, -0.0f, -0.0f);
    CHECK_FALSE(std::signbit(group.SpacingX()));
    CHECK_FALSE(std::signbit(group.SpacingY()));
    CHECK_FALSE(std::signbit(group.PaddingLeft()));
    CHECK_FALSE(std::signbit(group.PaddingRight()));
    CHECK_FALSE(std::signbit(group.PaddingTop()));
    CHECK_FALSE(std::signbit(group.PaddingBottom()));
    nlohmann::json encoded;
    group.Serialize(encoded);
    CHECK(encoded["spacingX"].dump() == "0.0");
    CHECK(encoded["spacingY"].dump() == "0.0");
    CHECK(encoded["paddingLeft"].dump() == "0.0");
    CHECK(encoded["paddingRight"].dump() == "0.0");
    CHECK(encoded["paddingTop"].dump() == "0.0");
    CHECK(encoded["paddingBottom"].dump() == "0.0");

    UICanvas canvas;
    canvas.SetMatchWidthOrHeight(-0.0f);
    CHECK_FALSE(std::signbit(canvas.GetMatchWidthOrHeight()));
    nlohmann::json canvasJson;
    canvas.Serialize(canvasJson);
    CHECK(canvasJson["matchWidthOrHeight"].dump() == "0.0");

    // UILayoutElement는 std::max(0.0f, -0.0f)가 첫 인자를 돌려주는 덕에
    // 우연히 안전하다. 그 우연에 기대지 않도록 여기서도 못을 박는다.
    UILayoutElement element;
    element.SetHorizontal({1.0f, 1.0f, 1.0f});
    element.SetHorizontal({-0.0f, -0.0f, -0.0f});
    CHECK_FALSE(std::signbit(element.Horizontal().minimum));
    CHECK_FALSE(std::signbit(element.Horizontal().preferred));
    CHECK_FALSE(std::signbit(element.Horizontal().flexible));
}

TEST_CASE("UIScrollView deserialize orders the movement/elasticity pair safely") {
    // 이 짝 불변식(Elastic이면 elasticity > 0)은 두 값을 함께 봐야 판정된다.
    // 살아 있는 컴포넌트에 다시 읽히는 경로(실행 취소, prefab override 적용)에서
    // 남아 있는 옛 값과 새 값이 섞이면 완전히 유효한 문서가 거부된다.
    UIScrollView elastic;
    elastic.SetElasticity(0.5f);
    elastic.SetMovement(UIScrollMovement::Elastic);
    REQUIRE(elastic.Movement() == UIScrollMovement::Elastic);

    nlohmann::json clamped;
    UIScrollView source;
    source.Serialize(clamped);
    clamped["movement"] = "Clamped";
    clamped["elasticity"] = 0.0;
    const auto code = ThrownSchemaCode([&] { elastic.Deserialize(clamped); });
    CHECK_FALSE(code.has_value());
    CHECK(elastic.Movement() == UIScrollMovement::Clamped);
    CHECK(elastic.Elasticity() == doctest::Approx(0.0f));

    // 반대 방향도 마찬가지다. Clamped/0.0인 컴포넌트에 Elastic/0.25를 읽히면
    // movement를 먼저 세우는 순서는 옛 elasticity 0.0을 보고 거부한다.
    nlohmann::json toElastic = clamped;
    toElastic["movement"] = "Elastic";
    toElastic["elasticity"] = 0.25;
    const auto back = ThrownSchemaCode([&] { elastic.Deserialize(toElastic); });
    CHECK_FALSE(back.has_value());
    CHECK(elastic.Movement() == UIScrollMovement::Elastic);
    CHECK(elastic.Elasticity() == doctest::Approx(0.25f));

    // 실패 증인: 짝이 실제로 모순인 문서는 여전히 거부된다.
    nlohmann::json broken = clamped;
    broken["movement"] = "Elastic";
    broken["elasticity"] = 0.0;
    UIScrollView target;
    const auto rejected = ThrownSchemaCode([&] { target.Deserialize(broken); });
    REQUIRE(rejected.has_value());
    CHECK(*rejected == TextDiagnosticCode::LayoutInvalid);

    // 그리고 이미 payload와 같은 상태인 컴포넌트를 다시 읽히는 것은 저작
    // 변경이 아니다 — 중간값을 거치는 구현은 여기서 revision을 두 번 올린다.
    UIScrollView idle;
    nlohmann::json snapshot;
    idle.Serialize(snapshot);
    const std::uint64_t revision = idle.AuthoredRevision();
    idle.Deserialize(snapshot);
    CHECK(idle.AuthoredRevision() == revision);
}

// ── Step 2: prefab 복제에서 참조가 리매핑된다 ────────────────────────────────

TEST_CASE("UI references remap through a cloned prefab subtree") {
    RegisterBuiltinComponents();
    auto root = std::make_shared<GameObject>("ScrollRoot");
    auto viewport = std::make_shared<GameObject>("Viewport");
    auto content = std::make_shared<GameObject>("Content");
    auto up = std::make_shared<GameObject>("Up");
    auto down = std::make_shared<GameObject>("Down");
    auto left = std::make_shared<GameObject>("Left");
    auto right = std::make_shared<GameObject>("Right");
    auto rendered = std::make_shared<GameObject>("Rendered");
    auto placeholder = std::make_shared<GameObject>("Placeholder");
    for (auto* child : {viewport.get(), content.get(), up.get(), down.get(),
                        left.get(), right.get(), rendered.get(),
                        placeholder.get()}) {
        child->SetParent(root.get());
    }

    constexpr unsigned int kExternalId = 90001u;
    auto* scroll = root->AddComponent<UIScrollView>();
    scroll->SetViewport(SceneObjectRef{viewport->GetID()});
    scroll->SetContent(SceneObjectRef{content->GetID()});
    auto* selectable = root->AddComponent<UISelectable>();
    selectable->SetNavigateUp(SceneObjectRef{up->GetID()});
    selectable->SetNavigateDown(SceneObjectRef{down->GetID()});
    selectable->SetNavigateLeft(SceneObjectRef{left->GetID()});
    selectable->SetNavigateRight(SceneObjectRef{kExternalId});
    auto* input = root->AddComponent<UITextInput>();
    input->SetTextViewport(SceneObjectRef{right->GetID()});
    input->SetRenderedLabel(SceneObjectRef{rendered->GetID()});
    input->SetPlaceholderLabel(SceneObjectRef{placeholder->GetID()});

    const ClonedSubtree cloned = CloneSubtree(root.get());
    REQUIRE(cloned.root != nullptr);
    auto* clonedScroll = cloned.root->GetComponent<UIScrollView>();
    auto* clonedSelectable = cloned.root->GetComponent<UISelectable>();
    auto* clonedInput = cloned.root->GetComponent<UITextInput>();
    REQUIRE(clonedScroll != nullptr);
    REQUIRE(clonedSelectable != nullptr);
    REQUIRE(clonedInput != nullptr);

    CHECK(clonedScroll->Viewport().targetId ==
          Remapped(cloned, viewport->GetID()));
    CHECK(clonedScroll->Content().targetId ==
          Remapped(cloned, content->GetID()));
    CHECK(clonedSelectable->NavigateUp().targetId ==
          Remapped(cloned, up->GetID()));
    CHECK(clonedSelectable->NavigateDown().targetId ==
          Remapped(cloned, down->GetID()));
    CHECK(clonedSelectable->NavigateLeft().targetId ==
          Remapped(cloned, left->GetID()));
    CHECK(clonedInput->TextViewport().targetId ==
          Remapped(cloned, right->GetID()));
    CHECK(clonedInput->RenderedLabel().targetId ==
          Remapped(cloned, rendered->GetID()));
    CHECK(clonedInput->PlaceholderLabel().targetId ==
          Remapped(cloned, placeholder->GetID()));

    // 서브트리 바깥을 가리키는 참조는 손대지 않는다.
    CHECK(clonedSelectable->NavigateRight().targetId == kExternalId);
    // 그리고 리매핑이 "아무것도 안 함"으로 퇴화하지 않았다는 증인.
    CHECK(clonedScroll->Viewport().targetId != viewport->GetID());
}

// ── Step 15: prefab override 비교가 schemaVersion을 무시한다 ─────────────────

TEST_CASE("prefab override diffing ignores schemaVersion but keeps authored refs") {
    RegisterBuiltinComponents();
    auto templateRoot = std::make_shared<GameObject>("Selectable");
    templateRoot->AddComponent<UISelectable>();

    nlohmann::json prefabJson =
        SceneSerializer::SerializeSubtree(templateRoot.get());
    // 이 prefab 파일은 컴포넌트가 schemaVersion을 쓰기 전에 저장된 것이다.
    // 그 키가 override 비교에 들어가면 손대지 않은 인스턴스마다 가짜 override가
    // 하나씩 생긴다.
    for (auto& objJson : prefabJson["gameObjects"]) {
        for (auto& compJson : objJson["components"]) {
            if (compJson.value("type", std::string{}) == "UISelectable") {
                compJson.erase("schemaVersion");
            }
        }
    }

    std::vector<std::shared_ptr<GameObject>> instantiated;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* instanceRoot = SceneSerializer::DeserializeSubtreeRemapped(
        prefabJson, instantiated, idRemap);
    REQUIRE(instanceRoot != nullptr);

    const nlohmann::json unchanged =
        PrefabUtil::GenerateModifications(instanceRoot, prefabJson, idRemap);
    CHECK(ModificationKeys(unchanged, "UISelectable").empty());

    auto* live = instanceRoot->GetComponent<UISelectable>();
    REQUIRE(live != nullptr);
    live->SetInteractable(false);
    live->SetNavigateUp(SceneObjectRef{4242u});
    const nlohmann::json changed =
        PrefabUtil::GenerateModifications(instanceRoot, prefabJson, idRemap);
    const auto keys = ModificationKeys(changed, "UISelectable");
    CHECK(keys.count("interactable") == 1u);
    CHECK(keys.count("navigateUp") == 1u);
    CHECK(keys.count("schemaVersion") == 0u);
}

TEST_CASE("prefab overrides keep in-prefab UI references in the prefab id space") {
    // 런타임 스냅샷은 리매핑된 런타임 id를 담고, prefab 파일은 prefab-local id를
    // 담는다. 그 둘을 그대로 비교하면 prefab 안쪽을 가리키는 참조는 손대지 않은
    // 인스턴스에서도 언제나 달라 보이고, 그 가짜 override에는 런타임 id가 실려
    // 파일에 저장된다. 다음 로드는 그 죽은 id를 새 인스턴스에 강제해 참조를
    // 끊는다.
    RegisterBuiltinComponents();
    auto templateRoot = std::make_shared<GameObject>("ScrollRoot");
    auto viewport = std::make_shared<GameObject>("Viewport");
    viewport->SetParent(templateRoot.get());
    auto* scroll = templateRoot->AddComponent<UIScrollView>();
    scroll->SetViewport(SceneObjectRef{viewport->GetID()});

    const nlohmann::json prefabJson =
        SceneSerializer::SerializeSubtree(templateRoot.get());

    std::vector<std::shared_ptr<GameObject>> instantiated;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* instanceRoot = SceneSerializer::DeserializeSubtreeRemapped(
        prefabJson, instantiated, idRemap);
    REQUIRE(instanceRoot != nullptr);
    auto* liveScroll = instanceRoot->GetComponent<UIScrollView>();
    REQUIRE(liveScroll != nullptr);
    // 리매핑이 실제로 일어났다는 증인. 이 줄이 없으면 아래 빈 override 집합이
    // "id가 애초에 같아서"일 수도 있다.
    CHECK(liveScroll->Viewport().targetId != viewport->GetID());

    const nlohmann::json unchanged =
        PrefabUtil::GenerateModifications(instanceRoot, prefabJson, idRemap);
    CHECK(ModificationKeys(unchanged, "UIScrollView").empty());

    // 저작자가 실제로 참조를 바꾸면 override는 나오되, 값은 prefab id 공간이다.
    constexpr unsigned int kExternalId = 90002u;
    liveScroll->SetContent(SceneObjectRef{kExternalId});
    liveScroll->SetViewport(SceneObjectRef{instanceRoot->GetID()});
    const nlohmann::json changed =
        PrefabUtil::GenerateModifications(instanceRoot, prefabJson, idRemap);
    nlohmann::json viewportOverride;
    nlohmann::json contentOverride;
    for (const auto& modification : changed) {
        if (modification.value("component", std::string{}) != "UIScrollView") {
            continue;
        }
        const std::string key = modification.value("key", std::string{});
        if (key == "viewport") viewportOverride = modification["value"];
        if (key == "content") contentOverride = modification["value"];
    }
    REQUIRE(viewportOverride.is_object());
    // prefab 파일에서 루트의 local id.
    unsigned int localRootId = 0;
    for (const auto& [localId, runtimeId] : idRemap) {
        if (runtimeId == instanceRoot->GetID()) localRootId = localId;
    }
    REQUIRE(localRootId != 0u);
    CHECK(viewportOverride["targetId"] == localRootId);
    // 서브트리 바깥을 가리키는 id는 어느 공간에도 속하지 않으므로 그대로다.
    REQUIRE(contentOverride.is_object());
    CHECK(contentOverride["targetId"] == kExternalId);

    // 그리고 그 override를 새 인스턴스에 적용하면 런타임 id로 되돌아온다.
    std::vector<std::shared_ptr<GameObject>> reinstantiated;
    std::unordered_map<unsigned int, unsigned int> reRemap;
    GameObject* reinstanced = SceneSerializer::DeserializeSubtreeRemapped(
        prefabJson, reinstantiated, reRemap);
    REQUIRE(reinstanced != nullptr);
    PrefabUtil::ApplyModifications(reinstanced, changed, reRemap);
    auto* reScroll = reinstanced->GetComponent<UIScrollView>();
    REQUIRE(reScroll != nullptr);
    CHECK(reScroll->Viewport().targetId == reinstanced->GetID());
    CHECK(reScroll->Content().targetId == kExternalId);
}

TEST_CASE("applying prefab overrides ignores schemaVersion and fails closed") {
    RegisterBuiltinComponents();
    auto templateRoot = std::make_shared<GameObject>("Group");
    templateRoot->AddComponent<UILayoutGroup>();
    const nlohmann::json prefabJson =
        SceneSerializer::SerializeSubtree(templateRoot.get());

    std::vector<std::shared_ptr<GameObject>> instantiated;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* instanceRoot = SceneSerializer::DeserializeSubtreeRemapped(
        prefabJson, instantiated, idRemap);
    REQUIRE(instanceRoot != nullptr);
    auto* live = instanceRoot->GetComponent<UILayoutGroup>();
    REQUIRE(live != nullptr);

    unsigned int localRootId = 0;
    for (const auto& [localId, runtimeId] : idRemap) {
        if (runtimeId == instanceRoot->GetID()) localRootId = localId;
    }
    REQUIRE(localRootId != 0u);

    // 옛 파일에 남아 있는 형식 표식 override는 적용되지 않는다. UICanvas로
    // 건다 — 그 컴포넌트는 schemaVersion으로 분기해 읽으므로, 표식이 주입되면
    // 저작된 schema 2 값이 그 자리에서 지워지는 것이 눈에 보인다. schemaVersion을
    // 아예 읽지 않는 컴포넌트로는 이 계약을 관측할 수 없다.
    auto* canvas = instanceRoot->AddComponent<UICanvas>();
    canvas->SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
    REQUIRE(canvas->GetLoadedSchema() == LoadedSchema::Current);

    const nlohmann::json markerOverride = nlohmann::json::array(
        {{{"target", localRootId},
          {"component", "UICanvas"},
          {"key", "schemaVersion"},
          {"value", 1}},
         {{"target", localRootId},
          {"component", "UILayoutGroup"},
          {"key", "spacingX"},
          {"value", 7.0}}});
    PrefabUtil::ApplyModifications(instanceRoot, markerOverride, idRemap);
    CHECK(canvas->GetLoadedSchema() == LoadedSchema::Current);
    CHECK(canvas->GetScaleMode() == UICanvasScaleMode::ConstantPixelSize);
    nlohmann::json applied;
    live->Serialize(applied);
    CHECK(applied["schemaVersion"] == UILayoutGroup::CurrentSchemaVersion);
    // 성공 증인: 표식이 아닌 override는 그대로 적용된다.
    CHECK(live->SpacingX() == doctest::Approx(7.0f));

    // 계약을 벗어난 override는 예외가 아니라 typed 실패로 SceneSerializer까지
    // 올라가고, 거기서 로드 전체가 닫힌다.
    const nlohmann::json badOverride = nlohmann::json::array(
        {{{"target", localRootId},
          {"component", "UILayoutGroup"},
          {"key", "spacingX"},
          {"value", -3.0}}});
    const auto code = ThrownSchemaCode(
        [&] { PrefabUtil::ApplyModifications(instanceRoot, badOverride, idRemap); });
    REQUIRE(code.has_value());
    CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    CHECK(live->SpacingX() == doctest::Approx(7.0f));
}

TEST_CASE("a scene whose prefab override violates the schema fails closed") {
    // ApplyModifications는 SceneSerializer 안에서 불린다. 그 호출이 감싸이지
    // 않으면 예외가 DeserializeScene의 objects.clear()를 건너뛰고 LoadScene을
    // 지나 호출부까지 올라가는데, 에디터에서 그 objects는 살아 있는 월드의
    // 오브젝트 벡터 그 자체라 반쯤 실린 씬이 그대로 남는다.
    RegisterBuiltinComponents();
    const std::filesystem::path tempDir = "/tmp/molga_test_ui_components_prefab";
    const std::filesystem::path oldRoot = PathService::Get().AssetRoot();
    std::filesystem::create_directories(tempDir);
    PathService::Get().SetAssetRoot(tempDir);
    PrefabRegistry::Get().ScanAssets();

    auto templateRoot = std::make_shared<GameObject>("Group");
    templateRoot->AddComponent<UILayoutGroup>();
    const nlohmann::json subtree =
        SceneSerializer::SerializeSubtree(templateRoot.get());
    const std::string guid = PrefabRegistry::GenerateGUID();
    REQUIRE(PrefabRegistry::Get().SavePrefab(guid, "group.prefab", subtree));

    const unsigned int localRootId =
        subtree["gameObjects"][0]["id"].get<unsigned int>();
    auto sceneWith = [&](const nlohmann::json& value) {
        return nlohmann::json{
            {"version", "1.0"},
            {"name", "PrefabOverride"},
            {"gameObjects",
             {{{"name", "Plain"}, {"id", 501}, {"parentId", -1}},
              {{"id", 502},
               {"prefabInstance",
                {{"guid", guid},
                 {"rootId", 502},
                 {"parentId", -1},
                 {"modifications",
                  nlohmann::json::array({{{"target", localRootId},
                                          {"component", "UILayoutGroup"},
                                          {"key", "spacingX"},
                                          {"value", value}}})}}}}}}};
    };

    std::vector<std::shared_ptr<GameObject>> restored;
    bool loaded = true;
    const auto escaped = ThrownSchemaCode([&] {
        loaded = SceneSerializer::DeserializeScene(sceneWith(-3.0), restored);
    });
    CHECK_FALSE(escaped.has_value());
    CHECK_FALSE(loaded);
    CHECK(restored.empty());

    // 성공 증인: 계약 안의 값은 실리고 실제로 적용된다.
    std::vector<std::shared_ptr<GameObject>> good;
    CHECK(SceneSerializer::DeserializeScene(sceneWith(3.0), good));
    CHECK(good.size() == 2u);

    PathService::Get().SetAssetRoot(oldRoot);
    std::filesystem::remove_all(tempDir);
}

// ── Step 14b: 모든 컴포넌트 factory 등록 ─────────────────────────────────────

TEST_CASE("every new UI component type constructs from its serialized name") {
    // 이 케이스가 무엇을 걸지 못하는지 적어 둔다. REGISTER_COMPONENT가 각 .cpp
    // 위쪽에 파일 정적 등록기를 심고, 이 번역 단위는 여덟 타입을 전부 이름으로
    // 부르므로 그 .o들은 언제나 링크되고 언제나 스스로 등록된다. 따라서 여기서
    // 증명되는 것은 자기 등록이지 BuiltinComponents.cpp의 명시적 줄이 아니다.
    // 그 줄은 UIMask를 코드에서 한 번도 부르지 않는 바이너리(씬을 읽기만 하는
    // molga_runtime)가 아카이브에서 그 .o를 끌어오게 하는 유일한 장치이므로
    // 실제로 필요하다 — 그것을 거는 시험은 컴포넌트 헤더를 include하지 않는
    // 별도 타깃에서만 쓸 수 있다.
    RegisterBuiltinComponents();
    auto object = std::make_shared<GameObject>("Registry");
    struct Row {
        const char* typeName;
        std::size_t runtimeTypeId;
    };
    const Row rows[] = {
        {"UILayoutElement", UILayoutElement::StaticRuntimeTypeID()},
        {"UILayoutGroup", UILayoutGroup::StaticRuntimeTypeID()},
        {"UIContentSizeFitter", UIContentSizeFitter::StaticRuntimeTypeID()},
        {"UIMask", UIMask::StaticRuntimeTypeID()},
        {"UIScrollView", UIScrollView::StaticRuntimeTypeID()},
        {"UISelectable", UISelectable::StaticRuntimeTypeID()},
        {"UITextInput", UITextInput::StaticRuntimeTypeID()},
        {"UIAccessibility", UIAccessibility::StaticRuntimeTypeID()}};
    for (const auto& row : rows) {
        CAPTURE(row.typeName);
        Component* created =
            ComponentFactory::Get().Create(row.typeName, object.get());
        REQUIRE(created != nullptr);
        CHECK(created->GetTypeName() == row.typeName);
        CHECK(created->GetRuntimeTypeID() == row.runtimeTypeId);
    }
    // 등록되지 않은 이름은 여전히 실패한다.
    CHECK(ComponentFactory::Get().Create("UINotAComponent", object.get()) ==
          nullptr);
}

// ── Step 3: 레거시 payload는 읽은 모양 그대로 남는다 ─────────────────────────

TEST_CASE("a legacy UICanvas payload keeps its bytes and its scaling") {
    const nlohmann::json legacy = ParseJson(
        R"({"type":"UICanvas","referenceResolution":[1024.0,768.0],
            "matchWidthOrHeight":0.25,"sortingOrder":3})");
    UICanvas canvas;
    canvas.Deserialize(legacy);
    CHECK(canvas.GetLoadedSchema() == LoadedSchema::Legacy);
    CHECK(canvas.GetReferenceResolution() == Vector2(1024.0f, 768.0f));
    CHECK(canvas.GetMatchWidthOrHeight() == doctest::Approx(0.25f));
    CHECK(canvas.GetSortingOrder() == 3);
    // 레거시 캔버스는 언제나 뷰포트에 맞춰 배율을 잡던 그 동작을 유지한다.
    CHECK(canvas.GetScaleMode() == UICanvasScaleMode::ScaleWithViewport);
    CHECK(canvas.ScaleFactor({1024.0f, 768.0f}) == doctest::Approx(1.0f));
    CHECK(canvas.ScaleFactor({2048.0f, 1536.0f}) == doctest::Approx(2.0f));

    nlohmann::json saved;
    canvas.Serialize(saved);
    nlohmann::json expected = legacy;
    expected.erase("type");
    CHECK(saved == expected);
    CHECK_FALSE(saved.contains("schemaVersion"));
    CHECK_FALSE(saved.contains("scaleMode"));

    // 갓 만든 캔버스의 기본 모드가 이미 ScaleWithViewport이므로, 위 블록만으로는
    // 레거시 분기의 모드 강제를 지운 구현도 통과한다. 그 분기가 실제로 일하는
    // 경우는 schema 2 값을 저작한 컴포넌트에 레거시 스냅샷을 다시 읽힐 때다.
    UICanvas reused;
    reused.SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
    REQUIRE(reused.GetScaleMode() == UICanvasScaleMode::ConstantPixelSize);
    reused.Deserialize(legacy);
    CHECK(reused.GetLoadedSchema() == LoadedSchema::Legacy);
    CHECK(reused.GetScaleMode() == UICanvasScaleMode::ScaleWithViewport);
    CHECK(reused.ScaleFactor({2048.0f, 1536.0f}) == doctest::Approx(2.0f));
}

TEST_CASE("authoring the schema-2-only canvas mode leaves the legacy shape") {
    // 레거시로 읽힌 캔버스에서 SetScaleMode가 표식을 그대로 두면 그 값은
    // 직렬화되지 않고 다음 Deserialize가 되돌린다 — 실행 취소와 다시 실행이
    // 모두 ScaleWithViewport로 떨어져 변경을 되살릴 수조차 없다. UILabel이
    // fontFamilyGuid에서 이미 푼 문제와 같은 모양이다.
    const nlohmann::json legacy = ParseJson(
        R"({"referenceResolution":[1024.0,768.0],
            "matchWidthOrHeight":0.25,"sortingOrder":3})");
    UICanvas canvas;
    canvas.Deserialize(legacy);
    REQUIRE(canvas.GetLoadedSchema() == LoadedSchema::Legacy);

    // 레거시 문서로도 표현할 수 있는 값을 쓰는 것은 이관이 아니다.
    canvas.SetSortingOrder(9);
    CHECK(canvas.GetLoadedSchema() == LoadedSchema::Legacy);

    canvas.SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
    CHECK(canvas.GetLoadedSchema() == LoadedSchema::Current);
    nlohmann::json saved;
    canvas.Serialize(saved);
    CHECK(saved["scaleMode"] == "ConstantPixelSize");
    CHECK(saved["schemaVersion"] == UICanvas::CurrentSchemaVersion);

    UICanvas reloaded;
    reloaded.Deserialize(saved);
    CHECK(reloaded.GetScaleMode() == UICanvasScaleMode::ConstantPixelSize);
    CHECK(reloaded.ScaleFactor({2048.0f, 1536.0f}) == doctest::Approx(1.0f));
}

TEST_CASE("UICanvas rejects non-finite scaling values and clamps the range") {
    UICanvas canvas;
    const std::vector<std::function<void()>> rejected = {
        [&] {
            canvas.SetReferenceResolution(
                {std::numeric_limits<float>::quiet_NaN(), 600.0f});
        },
        [&] {
            canvas.SetReferenceResolution(
                {800.0f, std::numeric_limits<float>::infinity()});
        },
        [&] {
            canvas.SetMatchWidthOrHeight(
                std::numeric_limits<float>::quiet_NaN());
        }};
    for (std::size_t index = 0; index < rejected.size(); ++index) {
        CAPTURE(index);
        const auto code = ThrownSchemaCode(rejected[index]);
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }
    CHECK(canvas.GetReferenceResolution() == Vector2(800.0f, 600.0f));
    CHECK(canvas.GetMatchWidthOrHeight() == doctest::Approx(0.5f));

    // 범위 밖 값은 거부가 아니라 조임이다. 레거시 문서가 이미 그런 값을 담고
    // 있을 수 있고, 그때의 화면 결과를 바꾸지 않는 것이 Step 12b의 계약이다.
    canvas.SetReferenceResolution({0.0f, -4.0f});
    CHECK(canvas.GetReferenceResolution() == Vector2(1.0f, 1.0f));
    canvas.SetMatchWidthOrHeight(2.0f);
    CHECK(canvas.GetMatchWidthOrHeight() == doctest::Approx(1.0f));
    canvas.SetMatchWidthOrHeight(-3.0f);
    CHECK(canvas.GetMatchWidthOrHeight() == doctest::Approx(0.0f));

    // 성공 증인: 범위 안의 유한값은 그대로 실린다.
    canvas.SetReferenceResolution({1920.0f, 1080.0f});
    canvas.SetMatchWidthOrHeight(0.25f);
    CHECK(canvas.GetReferenceResolution() == Vector2(1920.0f, 1080.0f));
    CHECK(canvas.GetMatchWidthOrHeight() == doctest::Approx(0.25f));
}

TEST_CASE("a newly authored UICanvas saves the current schema") {
    UICanvas canvas;
    CHECK(canvas.GetLoadedSchema() == LoadedSchema::Current);
    nlohmann::json saved;
    canvas.Serialize(saved);
    CHECK(saved == ParseJson(
        R"({"schemaVersion":2,"scaleMode":"ScaleWithViewport",
            "referenceResolution":[800.0,600.0],"matchWidthOrHeight":0.5,
            "sortingOrder":0})"));

    canvas.SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
    // 고정 픽셀 모드는 뷰포트가 어떻든 배율 1이다.
    CHECK(canvas.ScaleFactor({1600.0f, 1200.0f}) == doctest::Approx(1.0f));
    nlohmann::json constant;
    canvas.Serialize(constant);
    CHECK(constant["scaleMode"] == "ConstantPixelSize");

    UICanvas reloaded;
    reloaded.Deserialize(constant);
    CHECK(reloaded.GetLoadedSchema() == LoadedSchema::Current);
    CHECK(reloaded.GetScaleMode() == UICanvasScaleMode::ConstantPixelSize);

    const auto code = ThrownSchemaCode([&] {
        nlohmann::json bad = constant;
        bad["scaleMode"] = "Stretch";
        reloaded.Deserialize(bad);
    });
    REQUIRE(code.has_value());
    CHECK(*code == TextDiagnosticCode::LayoutInvalid);
}

TEST_CASE("a legacy UILabel and TextRenderer2D payload keeps its legacy shape") {
    const nlohmann::json legacyLabel = ParseJson(
        u8R"({"type":"UILabel","text":"제목","fontGuid":"font-a",
              "fontSizePx":32.0,"lineSpacing":1.5,"color":[1.0,1.0,1.0,1.0],
              "horizontalAlignment":1,"verticalAlignment":1,"sortingOrder":1})");
    UILabel label;
    label.Deserialize(legacyLabel);
    CHECK(label.GetLoadedSchema() == LoadedSchema::Legacy);
    CHECK(label.LoadedLegacyFontGuid());
    CHECK(label.GetFontGuid() == "font-a");
    CHECK(label.GetFontFamilyGuid().empty());
    nlohmann::json savedLabel;
    label.Serialize(savedLabel);
    nlohmann::json expectedLabel = legacyLabel;
    expectedLabel.erase("type");
    CHECK(savedLabel == expectedLabel);

    UILabel authoredLabel;
    CHECK(authoredLabel.GetLoadedSchema() == LoadedSchema::Current);

    // std::clamp는 NaN을 그대로 통과시킨다. 저장된 NaN은 자기 자신과 같지
    // 않으므로 이후 모든 SetFontSizePx가 무효화를 일으켜 그 라벨의 스냅샷
    // 빠른 경로를 영구히 무너뜨리고, Serialize는 JSON null을 쓴다.
    const auto nanSize = ThrownSchemaCode([&] {
        authoredLabel.SetFontSizePx(std::numeric_limits<float>::quiet_NaN());
    });
    REQUIRE(nanSize.has_value());
    CHECK(*nanSize == TextDiagnosticCode::LayoutInvalid);
    const auto nanSpacing = ThrownSchemaCode([&] {
        authoredLabel.SetLineSpacing(std::numeric_limits<float>::infinity());
    });
    REQUIRE(nanSpacing.has_value());
    CHECK(*nanSpacing == TextDiagnosticCode::LayoutInvalid);
    // 성공 증인: 유한값은 여전히 잘리기만 한다(레거시 계약 그대로).
    authoredLabel.SetFontSizePx(4096.0f);
    CHECK(authoredLabel.GetFontSizePx() == doctest::Approx(512.0f));
    authoredLabel.SetLineSpacing(0.0f);
    CHECK(authoredLabel.GetLineSpacing() == doctest::Approx(0.1f));

    // TextRenderer2D는 이 태스크의 Files 목록 밖이라 새 접근자를 얻지 않는다.
    // 같은 표식이 기존 이름으로 남아 있으므로 그것으로 건다.
    const nlohmann::json legacyText = ParseJson(
        R"({"type":"TextRenderer2D","text":"world","fontGuid":"font-b",
            "fontName":"arial","fontSizePx":12.0,"lineSpacing":1.0,
            "color":[1.0,1.0,1.0,1.0],"scale":1.0,"alignment":0,
            "sortingLayer":"Default","sortingOrder":0,"sortMode":"Fixed",
            "ySortOffset":0.0})");
    TextRenderer2D text;
    text.Deserialize(legacyText);
    CHECK(text.LoadedLegacyFontGuid());
    CHECK(text.GetFontGuid() == "font-b");
    nlohmann::json savedText;
    text.Serialize(savedText);
    nlohmann::json expectedText = legacyText;
    expectedText.erase("type");
    CHECK(savedText == expectedText);
}

// ── 남은 저작 컴포넌트 세 개의 스키마 버전 ───────────────────────────────────

namespace {

// 기본 UIButton 색은 이진수로 정확히 표현되지 않는 십진값이라(0.30, 0.42 …)
// JSON 텍스트로 적으면 파싱된 double이 float를 거쳐 온 값과 달라진다. 기대값도
// 같은 float 리터럴에서 만들어, 비교를 근사가 아니라 동일성으로 둔다.
nlohmann::json ExpectedDefaultUIButtonJson() {
    nlohmann::json expected = nlohmann::json::object();
    expected["schemaVersion"] = 1u;
    expected["interactable"] = true;
    expected["normalColor"] = {0.25f, 0.30f, 0.42f, 1.0f};
    expected["hoverColor"] = {0.35f, 0.42f, 0.58f, 1.0f};
    expected["pressedColor"] = {0.17f, 0.20f, 0.30f, 1.0f};
    expected["disabledColor"] = {0.25f, 0.25f, 0.25f, 0.55f};
    expected["sortingOrder"] = 0;
    return expected;
}

// RectTransform/UIImage/UIButton은 저작 모양이 한 번도 바뀐 적이 없어 schema 1이
// 곧 레거시 문서의 모양이다. 그래서 UILabel/UICanvas가 쓰는 LoadedSchema 분기가
// 없고, 걸 계약은 "언제나 키를 쓴다"와 "키가 없는 문서도 그대로 읽는다" 둘이다.
//
// CheckSchemaContract의 4)/5)(없는 키가 문서화된 기본값으로 돌아간다)는 여기서
// 걸지 않는다. 이 셋의 Deserialize는 없는 키의 fallback을 현재 값으로 두는
// 기존 계약이고(UILabel과 UICanvas도 같다), 그것을 바꾸는 일은 스키마 버전과
// 다른 변경이다.
template <typename ComponentT, typename AuthorFn>
void CheckVersionedSchema(const char* name, AuthorFn author,
                          const nlohmann::json& expectedDefault,
                          const nlohmann::json& expectedAuthored) {
    CAPTURE(name);
    REQUIRE(expectedDefault.at("schemaVersion") == 1);
    REQUIRE(expectedAuthored.at("schemaVersion") == 1);

    ComponentT fresh;
    nlohmann::json defaultEncoded;
    fresh.Serialize(defaultEncoded);
    CHECK(defaultEncoded == expectedDefault);

    ComponentT authored;
    author(authored);
    nlohmann::json encoded;
    authored.Serialize(encoded);
    CHECK(encoded == expectedAuthored);

    // 왕복 동등성만 보면 Serialize와 Deserialize가 같은 결함을 공유할 때
    // 통과한다. 위 두 검사가 기대 payload를 문자 그대로 못박은 뒤에야 이
    // 검사가 뜻을 갖는다.
    ComponentT decoded;
    decoded.Deserialize(expectedAuthored);
    nlohmann::json reencoded;
    decoded.Serialize(reencoded);
    CHECK(reencoded == expectedAuthored);
}

// 디스크의 옛 문서에는 이 키가 없다. 새 문서를 쓰고 다시 읽기만 하는 시험은
// 언제나 키가 있는 payload만 지나므로 이 경로를 한 번도 밟지 않는다. 두 모양을
// 모두 건다 — 키가 없는 문서만 시험하면 새 스키마가, 키가 있는 문서만 시험하면
// 레거시 기본값이 검사되지 않은 채 남는다.
template <typename ComponentT>
void CheckLegacyLoad(const char* name, const nlohmann::json& expectedDefault,
                     const nlohmann::json& legacy) {
    CAPTURE(name);
    REQUIRE_FALSE(legacy.contains("schemaVersion"));

    // 저장에서 달라지는 것은 표식 하나뿐이다. type/enabled는 컴포넌트가 아니라
    // 씬 봉투가 쓰는 키라 Serialize의 출력에 들어가지 않는다.
    nlohmann::json expected = legacy;
    expected.erase("type");
    expected.erase("enabled");
    expected["schemaVersion"] = 1u;

    ComponentT fresh;
    fresh.Deserialize(legacy);
    nlohmann::json saved;
    fresh.Serialize(saved);
    CHECK(saved == expected);

    // 이미 저작된 컴포넌트에 옛 문서를 다시 읽히는 것이 실행 취소와 prefab
    // override의 경로다. 갓 만든 인스턴스만 쓰면 "문서의 값이 적용됐다"와
    // "기본값이 우연히 같다"를 가르지 못한다.
    ComponentT reused;
    reused.Deserialize(expectedDefault);
    reused.Deserialize(legacy);
    nlohmann::json reusedSaved;
    reused.Serialize(reusedSaved);
    CHECK(reusedSaved == expected);

    // 키를 명시한 같은 문서도 정확히 같은 결과여야 한다.
    nlohmann::json versioned = legacy;
    versioned["schemaVersion"] = 1u;
    ComponentT reloaded;
    reloaded.Deserialize(versioned);
    nlohmann::json reloadedSaved;
    reloaded.Serialize(reloadedSaved);
    CHECK(reloadedSaved == expected);

    // 버전은 payload에서 되받아 적는 값이 아니라 컴포넌트가 아는 상수다.
    // 되받아 적는 구현은 위 세 검사를 모두 통과한다.
    nlohmann::json foreign = legacy;
    foreign["schemaVersion"] = 99u;
    ComponentT echoed;
    echoed.Deserialize(foreign);
    nlohmann::json echoedSaved;
    echoed.Serialize(echoedSaved);
    CHECK(echoedSaved == expected);
}

// 스칼라가 하나도 겹치지 않는다. 필드 한 쌍이나 축 한 쌍이 통째로 뒤바뀌어도
// 값이 달라 반드시 걸린다 — Task 9.2에서 readOnly/multiline이 모든 fixture에서
// 같은 값이라 서로 바꿔치기해도 아무 시험이 밟지 않았던 그 구멍이다.
const char* const kAuthoredRectTransform =
    R"({"schemaVersion":1,"anchorMin":[0.0625,0.125],
        "anchorMax":[0.1875,0.25],"pivot":[0.3125,0.375],
        "anchoredPosition":[12.0,-34.0],"sizeDelta":[56.0,78.0]})";

const char* const kAuthoredUIImage =
    R"({"schemaVersion":1,"textureGuid":"texture-a",
        "tint":[0.0625,0.125,0.1875,0.25],"sortingOrder":7})";

const char* const kAuthoredUIButton =
    R"({"schemaVersion":1,"interactable":false,
        "normalColor":[0.0625,0.125,0.1875,0.25],
        "hoverColor":[0.3125,0.375,0.4375,0.5],
        "pressedColor":[0.5625,0.625,0.6875,0.75],
        "disabledColor":[0.8125,0.875,0.9375,1.0],
        "sortingOrder":9})";

} // namespace

TEST_CASE("the remaining authored UI components save an explicit schema version") {
    CheckVersionedSchema<RectTransform>(
        "RectTransform",
        [](RectTransform& rect) {
            rect.SetAnchorMin({0.0625f, 0.125f});
            rect.SetAnchorMax({0.1875f, 0.25f});
            rect.SetPivot({0.3125f, 0.375f});
            rect.SetAnchoredPosition({12.0f, -34.0f});
            rect.SetSizeDelta({56.0f, 78.0f});
        },
        ParseJson(R"({"schemaVersion":1,"anchorMin":[0.5,0.5],
                      "anchorMax":[0.5,0.5],"pivot":[0.5,0.5],
                      "anchoredPosition":[0.0,0.0],
                      "sizeDelta":[100.0,100.0]})"),
        ParseJson(kAuthoredRectTransform));

    CheckVersionedSchema<UIImage>(
        "UIImage",
        [](UIImage& image) {
            image.SetTextureGuid("texture-a");
            image.SetTint(Color{0.0625f, 0.125f, 0.1875f, 0.25f});
            image.SetSortingOrder(7);
        },
        ParseJson(R"({"schemaVersion":1,"textureGuid":"",
                      "tint":[1.0,1.0,1.0,1.0],"sortingOrder":0})"),
        ParseJson(kAuthoredUIImage));

    CheckVersionedSchema<UIButton>(
        "UIButton",
        [](UIButton& button) {
            button.SetInteractable(false);
            button.SetNormalColor(Color{0.0625f, 0.125f, 0.1875f, 0.25f});
            button.SetHoverColor(Color{0.3125f, 0.375f, 0.4375f, 0.5f});
            button.SetPressedColor(Color{0.5625f, 0.625f, 0.6875f, 0.75f});
            button.SetDisabledColor(Color{0.8125f, 0.875f, 0.9375f, 1.0f});
            button.SetSortingOrder(9);
        },
        ExpectedDefaultUIButtonJson(), ParseJson(kAuthoredUIButton));
}

TEST_CASE("a UI document written before the schema version still loads") {
    // 키 집합은 tests/smoke/create_fixture.cmake가 디스크에 쓰는 문서 그대로다.
    // 값만 이진수로 정확한 것으로 골라, 저장 결과를 근사가 아니라 문자 그대로
    // 맞댈 수 있게 했다.
    CheckLegacyLoad<RectTransform>(
        "RectTransform",
        ParseJson(R"({"schemaVersion":1,"anchorMin":[0.5,0.5],
                      "anchorMax":[0.5,0.5],"pivot":[0.5,0.5],
                      "anchoredPosition":[0.0,0.0],
                      "sizeDelta":[100.0,100.0]})"),
        ParseJson(R"({"type":"RectTransform","enabled":true,
                      "anchorMin":[0.0625,0.125],"anchorMax":[0.1875,0.25],
                      "pivot":[0.3125,0.375],"anchoredPosition":[12.0,-34.0],
                      "sizeDelta":[56.0,78.0]})"));

    CheckLegacyLoad<UIImage>(
        "UIImage",
        ParseJson(R"({"schemaVersion":1,"textureGuid":"",
                      "tint":[1.0,1.0,1.0,1.0],"sortingOrder":0})"),
        ParseJson(R"({"type":"UIImage","enabled":true,
                      "textureGuid":"11111111111111111111111111111111",
                      "tint":[0.0625,0.125,0.1875,0.25],"sortingOrder":7})"));

    CheckLegacyLoad<UIButton>(
        "UIButton", ExpectedDefaultUIButtonJson(),
        ParseJson(R"({"type":"UIButton","enabled":true,"interactable":false,
                      "normalColor":[0.0625,0.125,0.1875,0.25],
                      "hoverColor":[0.3125,0.375,0.4375,0.5],
                      "pressedColor":[0.5625,0.625,0.6875,0.75],
                      "disabledColor":[0.8125,0.875,0.9375,1.0],
                      "sortingOrder":9})"));

    // 위 표는 저장 payload로만 판정한다. 그 payload를 만드는 Serialize가
    // 통째로 잘못돼도 세 검사가 함께 틀어지면 통과할 수 있으므로, 옛 문서가
    // 실제로 컴포넌트 상태가 되었는지는 접근자로도 한 번 확인한다.
    RectTransform rect;
    rect.Deserialize(ParseJson(
        R"({"type":"RectTransform","enabled":true,
            "anchorMin":[0.0625,0.125],"anchorMax":[0.1875,0.25],
            "pivot":[0.3125,0.375],"anchoredPosition":[12.0,-34.0],
            "sizeDelta":[56.0,78.0]})"));
    CHECK(rect.GetAnchorMin() == Vector2(0.0625f, 0.125f));
    CHECK(rect.GetAnchorMax() == Vector2(0.1875f, 0.25f));
    CHECK(rect.GetPivot() == Vector2(0.3125f, 0.375f));
    CHECK(rect.GetAnchoredPosition() == Vector2(12.0f, -34.0f));
    CHECK(rect.GetSizeDelta() == Vector2(56.0f, 78.0f));

    UIImage image;
    image.Deserialize(ParseJson(
        R"({"type":"UIImage","enabled":true,
            "textureGuid":"11111111111111111111111111111111",
            "tint":[0.0625,0.125,0.1875,0.25],"sortingOrder":7})"));
    CHECK(image.GetTextureGuid() == "11111111111111111111111111111111");
    CHECK(image.GetTint() == Color{0.0625f, 0.125f, 0.1875f, 0.25f});
    CHECK(image.GetSortingOrder() == 7);

    UIButton button;
    button.Deserialize(ParseJson(
        R"({"type":"UIButton","enabled":true,"interactable":false,
            "normalColor":[0.0625,0.125,0.1875,0.25],
            "hoverColor":[0.3125,0.375,0.4375,0.5],
            "pressedColor":[0.5625,0.625,0.6875,0.75],
            "disabledColor":[0.8125,0.875,0.9375,1.0],
            "sortingOrder":9})"));
    CHECK_FALSE(button.IsInteractable());
    CHECK(button.GetNormalColor() == Color{0.0625f, 0.125f, 0.1875f, 0.25f});
    CHECK(button.GetHoverColor() == Color{0.3125f, 0.375f, 0.4375f, 0.5f});
    CHECK(button.GetPressedColor() == Color{0.5625f, 0.625f, 0.6875f, 0.75f});
    CHECK(button.GetDisabledColor() == Color{0.8125f, 0.875f, 0.9375f, 1.0f});
    CHECK(button.GetSortingOrder() == 9);
    // 상호작용 불가 버튼은 비활성 색을 쓴다. 네 색이 서로 다른 값이므로 이
    // 검사는 색 한 쌍이 뒤바뀐 구현을 걸러 낸다.
    CHECK(button.CurrentColor() == Color{0.8125f, 0.875f, 0.9375f, 1.0f});
}

// ── 정규 토큰 표 전체 ────────────────────────────────────────────────────────

namespace {

// 위 왕복 표는 열거마다 두 값(기본값과 저작값)만 지난다. 나머지 열거자는
// 철자가 틀려도 아무 시험이 밟지 않으므로, 여기서 값 하나하나를 문자 그대로의
// 기대 철자와 맞춘다. 디스크에 남는 문자열이 곧 영구 계약이다.
template <typename Enum, typename ParseFn>
void CheckEnumTokens(const char* name,
                     const std::vector<std::pair<Enum, std::string>>& rows,
                     ParseFn parse) {
    CAPTURE(name);
    for (const auto& row : rows) {
        CAPTURE(row.second);
        CHECK(std::string(ToCanonicalString(row.first)) == row.second);
        const auto parsed = parse(row.second);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == row.first);
    }
    // 실패 증인: 표에 없는 토큰은 조용히 어떤 값으로도 해석되지 않는다.
    CHECK_FALSE(parse("NotAToken").has_value());
}

} // namespace

TEST_CASE("every authored UI enum spells each enumerator exactly") {
    using molga::text::BaseDirection;
    using molga::text::TextHorizontalAlignment;
    using molga::text::TextOverflowMode;
    using molga::text::TextVerticalAlignment;
    using molga::text::TextWrapMode;

    CheckEnumTokens<UILayoutMode>(
        "UILayoutMode",
        {{UILayoutMode::Horizontal, "Horizontal"},
         {UILayoutMode::Vertical, "Vertical"},
         {UILayoutMode::Grid, "Grid"}},
        ParseUILayoutMode);
    CheckEnumTokens<UIGridConstraint>(
        "UIGridConstraint",
        {{UIGridConstraint::Flexible, "Flexible"},
         {UIGridConstraint::FixedColumns, "FixedColumns"},
         {UIGridConstraint::FixedRows, "FixedRows"}},
        ParseUIGridConstraint);
    CheckEnumTokens<UIFitMode>(
        "UIFitMode",
        {{UIFitMode::Unconstrained, "Unconstrained"},
         {UIFitMode::Min, "Min"},
         {UIFitMode::Preferred, "Preferred"}},
        ParseUIFitMode);
    CheckEnumTokens<UIScrollMovement>(
        "UIScrollMovement",
        {{UIScrollMovement::Clamped, "Clamped"},
         {UIScrollMovement::Elastic, "Elastic"}},
        ParseUIScrollMovement);
    CheckEnumTokens<UIGridStartCorner>(
        "UIGridStartCorner",
        {{UIGridStartCorner::UpperLeft, "UpperLeft"},
         {UIGridStartCorner::UpperRight, "UpperRight"},
         {UIGridStartCorner::LowerLeft, "LowerLeft"},
         {UIGridStartCorner::LowerRight, "LowerRight"}},
        ParseUIGridStartCorner);
    CheckEnumTokens<UIGridFillAxis>(
        "UIGridFillAxis",
        {{UIGridFillAxis::Horizontal, "Horizontal"},
         {UIGridFillAxis::Vertical, "Vertical"}},
        ParseUIGridFillAxis);
    CheckEnumTokens<UINavigationMode>(
        "UINavigationMode",
        {{UINavigationMode::None, "None"},
         {UINavigationMode::Auto, "Auto"},
         {UINavigationMode::Explicit, "Explicit"}},
        ParseUINavigationMode);
    CheckEnumTokens<UICanvasScaleMode>(
        "UICanvasScaleMode",
        {{UICanvasScaleMode::ConstantPixelSize, "ConstantPixelSize"},
         {UICanvasScaleMode::ScaleWithViewport, "ScaleWithViewport"}},
        ParseUICanvasScaleMode);
    CheckEnumTokens<UITextInputContentPolicy>(
        "UITextInputContentPolicy", {{UITextInputContentPolicy::Any, "Any"}},
        ParseUITextInputContentPolicy);
    CheckEnumTokens<UITextInputSubmitPolicy>(
        "UITextInputSubmitPolicy",
        {{UITextInputSubmitPolicy::OnEnter, "OnEnter"}},
        ParseUITextInputSubmitPolicy);
    CheckEnumTokens<BaseDirection>(
        "BaseDirection",
        {{BaseDirection::Auto, "Auto"},
         {BaseDirection::LeftToRight, "LTR"},
         {BaseDirection::RightToLeft, "RTL"}},
        ParseUIBaseDirection);
    CheckEnumTokens<TextWrapMode>(
        "TextWrapMode",
        {{TextWrapMode::NoWrap, "NoWrap"},
         {TextWrapMode::Word, "Word"},
         {TextWrapMode::Grapheme, "Grapheme"}},
        ParseUIWrapMode);
    CheckEnumTokens<TextOverflowMode>(
        "TextOverflowMode",
        {{TextOverflowMode::Overflow, "Overflow"},
         {TextOverflowMode::Clip, "Clip"},
         {TextOverflowMode::Ellipsis, "Ellipsis"}},
        ParseUIOverflowMode);
    CheckEnumTokens<TextHorizontalAlignment>(
        "TextHorizontalAlignment",
        {{TextHorizontalAlignment::Left, "Left"},
         {TextHorizontalAlignment::Center, "Center"},
         {TextHorizontalAlignment::Right, "Right"}},
        ParseUIHorizontalAlignment);
    CheckEnumTokens<TextVerticalAlignment>(
        "TextVerticalAlignment",
        {{TextVerticalAlignment::Top, "Top"},
         {TextVerticalAlignment::Middle, "Middle"},
         {TextVerticalAlignment::Bottom, "Bottom"}},
        ParseUIVerticalAlignment);
    CheckEnumTokens<UIAccessibilityRole>(
        "UIAccessibilityRole",
        {{UIAccessibilityRole::None, "None"},
         {UIAccessibilityRole::Panel, "Panel"},
         {UIAccessibilityRole::Label, "Label"},
         {UIAccessibilityRole::Button, "Button"},
         {UIAccessibilityRole::TextInput, "TextInput"},
         {UIAccessibilityRole::Image, "Image"},
         {UIAccessibilityRole::ScrollView, "ScrollView"}},
        ParseUIAccessibilityRole);
}

// ── 잘못된 JSON 타입도 typed 실패다 ──────────────────────────────────────────

TEST_CASE("a wrong-typed authored field is a typed deserialize failure") {
    // 타입이 어긋난 값을 조용히 무시하면 파일이 담은 값과 메모리가 갈라지고,
    // 다음 저장이 그 차이를 굳혀 저작자의 값을 지운다.
    struct Row {
        const char* what;
        std::function<void()> run;
    };
    const std::vector<Row> rows = {
        {"spacingX as a string",
         [] {
             UILayoutGroup group;
             nlohmann::json payload;
             group.Serialize(payload);
             payload["spacingX"] = "5";
             group.Deserialize(payload);
         }},
        {"clipsDescendants as a number",
         [] {
             UIMask mask;
             nlohmann::json payload;
             mask.Serialize(payload);
             payload["clipsDescendants"] = 1;
             mask.Deserialize(payload);
         }},
        {"constraintCount as a negative integer",
         [] {
             UILayoutGroup group;
             nlohmann::json payload;
             group.Serialize(payload);
             payload["constraintCount"] = -1;
             group.Deserialize(payload);
         }},
        {"accessibility name as a number",
         [] {
             UIAccessibility accessibility;
             nlohmann::json payload;
             accessibility.Serialize(payload);
             payload["name"] = 5;
             accessibility.Deserialize(payload);
         }},
        {"viewport as a bare id",
         [] {
             UIScrollView view;
             nlohmann::json payload;
             view.Serialize(payload);
             payload["viewport"] = 21;
             view.Deserialize(payload);
         }},
        {"axis record as a number",
         [] {
             UILayoutElement element;
             nlohmann::json payload;
             element.Serialize(payload);
             payload["horizontal"] = 3;
             element.Deserialize(payload);
         }},
        {"paragraphStyle as a string",
         [] {
             UITextInput input;
             nlohmann::json payload;
             input.Serialize(payload);
             payload["paragraphStyle"] = "x";
             input.Deserialize(payload);
         }},
    };
    for (const auto& row : rows) {
        CAPTURE(row.what);
        const auto code = ThrownSchemaCode(row.run);
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }

    // ── 부호 없는 필드는 저장 표현이 아니라 값으로 판정한다 ──────────────────
    // nlohmann은 C++ int를 대입하면 number_integer로, 텍스트에서 파싱한 양의
    // 정수는 number_unsigned로 담는다. is_number_unsigned()로 걸렀다면 인스펙터가
    // std::int64_t로 만들어 넣는 완전히 유효한 값이 전부 거부된다.
    {
        UILayoutGroup group;
        nlohmann::json payload;
        group.Serialize(payload);
        payload["constraintCount"] = 3;  // C++ int -> number_integer
        REQUIRE_FALSE(payload["constraintCount"].is_number_unsigned());
        const auto code = ThrownSchemaCode([&] { group.Deserialize(payload); });
        CHECK_FALSE(code.has_value());
        CHECK(group.ConstraintCount() == 3u);
    }
    {
        UITextInput input;
        nlohmann::json payload;
        input.Serialize(payload);
        payload["maxGraphemes"] = 7;
        payload["textViewport"]["targetId"] = 11;
        const auto code = ThrownSchemaCode([&] { input.Deserialize(payload); });
        CHECK_FALSE(code.has_value());
        CHECK(input.MaxGraphemes() == 7u);
        CHECK(input.TextViewport().targetId == 11u);
    }
    {
        // 범위 밖 정수를 조용히 자르면 maxGraphemes는 "제한 없음"이 되고
        // targetId는 "설정 안 됨"이 된다 — 둘 다 조용한 fail-open이다.
        UITextInput input;
        nlohmann::json payload;
        input.Serialize(payload);
        payload["maxGraphemes"] = 4294967296LL;
        const auto code = ThrownSchemaCode([&] { input.Deserialize(payload); });
        REQUIRE(code.has_value());
        CHECK(*code == TextDiagnosticCode::LayoutInvalid);
    }

    // 성공 증인: 손대지 않은 payload는 하나도 던지지 않는다.
    const auto clean = ThrownSchemaCode([] {
        UILayoutGroup group;
        UIMask mask;
        UIAccessibility accessibility;
        UIScrollView view;
        UILayoutElement element;
        UITextInput input;
        nlohmann::json payload;
        group.Serialize(payload);
        group.Deserialize(payload);
        payload = nlohmann::json::object();
        mask.Serialize(payload);
        mask.Deserialize(payload);
        payload = nlohmann::json::object();
        accessibility.Serialize(payload);
        accessibility.Deserialize(payload);
        payload = nlohmann::json::object();
        view.Serialize(payload);
        view.Deserialize(payload);
        payload = nlohmann::json::object();
        element.Serialize(payload);
        element.Deserialize(payload);
        payload = nlohmann::json::object();
        input.Serialize(payload);
        input.Deserialize(payload);
    });
    CHECK_FALSE(clean.has_value());
}

// ── 모든 저작 setter가 자기 비트로 무효화한다 ───────────────────────────────

namespace {

constexpr std::uint8_t kVisual = static_cast<std::uint8_t>(UIInvalidation::Visual);
constexpr std::uint8_t kIntrinsic =
    static_cast<std::uint8_t>(UIInvalidation::Intrinsic);
constexpr std::uint8_t kLayout = static_cast<std::uint8_t>(UIInvalidation::Layout);
constexpr std::uint8_t kHierarchy =
    static_cast<std::uint8_t>(UIInvalidation::Hierarchy);
constexpr std::uint8_t kInteraction =
    static_cast<std::uint8_t>(UIInvalidation::Interaction);

// setter 하나를 건다. 컴포넌트마다 한 setter만 증인이 있으면 나머지 setter에서
// Invalidate를 지운 구현이 그대로 통과하고, 그것은 스냅샷이 낡은 채로 남는
// 결함인데 뒤따르는 어느 마일스톤도 시험 실패로 드러내지 않는다.
//
// revisionDelta는 이유를 몇 번 올리는지다. Invalidate는 이유를 하나씩 받으므로
// (설계가 정한 서명), 두 축을 바꾸는 setter는 두 번 부른다.
template <typename ComponentT, typename MutateFn>
void CheckNarrowestBit(const char* what, MutateFn mutate,
                       std::uint8_t expectedBits,
                       std::uint64_t revisionDelta) {
    // CAPTURE(const char*)는 주소를 찍는다(doctest가
    // DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING 없이 빌드된다). 실패 메시지가
    // 어느 setter인지 말하려면 스트림으로 흘려야 한다.
    INFO("setter: " << std::string(what));
    ComponentT component;
    const std::uint64_t before = component.AuthoredRevision();
    REQUIRE(component.DirtyMask() == 0u);
    mutate(component);
    CHECK(component.AuthoredRevision() == before + revisionDelta);
    CHECK(component.DirtyMask() == expectedBits);
    CHECK(component.RevisionCacheable());

    // 같은 값을 다시 쓰는 것은 무효화가 아니다.
    component.ClearDirtyMask();
    mutate(component);
    CHECK(component.AuthoredRevision() == before + revisionDelta);
    CHECK(component.DirtyMask() == 0u);
}

} // namespace

TEST_CASE("re-deserializing an authored change invalidates like the setter") {
    // 되돌리기와 prefab override는 살아 있는 컴포넌트에 다시 Deserialize한다.
    // RectTransform과 UIImage의 Deserialize는 자기 세터를 건너뛰고 필드에 직접
    // 대입하고 있었으므로, 값은 바뀌는데 무효화가 일어나지 않았다. 같은 헬퍼를
    // 쓰므로 "같은 페이로드를 다시 넣는 것은 무효화가 아니다"도 함께 고정된다.
    // 키 하나짜리 페이로드를 쓰는 이유: 나머지 키는 현재 값으로 읽혀 세터가
    // 조기 반환하므로, revision 증가분이 정확히 1이어야 한다.
    CheckNarrowestBit<RectTransform>(
        "RectTransform::Deserialize(anchoredPosition)",
        [](RectTransform& c) {
            nlohmann::json payload;
            payload["anchoredPosition"] = {1.0f, 2.0f};
            c.Deserialize(payload);
        },
        kLayout, 1);
    CheckNarrowestBit<RectTransform>(
        "RectTransform::Deserialize(sizeDelta)",
        [](RectTransform& c) {
            nlohmann::json payload;
            payload["sizeDelta"] = {3.0f, 4.0f};
            c.Deserialize(payload);
        },
        kLayout, 1);
    CheckNarrowestBit<UIImage>(
        "UIImage::Deserialize(tint)",
        [](UIImage& c) {
            nlohmann::json payload;
            payload["tint"] = {0.25f, 0.5f, 0.75f, 1.0f};
            c.Deserialize(payload);
        },
        kVisual, 1);
    CheckNarrowestBit<UIImage>(
        "UIImage::Deserialize(sortingOrder)",
        [](UIImage& c) {
            nlohmann::json payload;
            payload["sortingOrder"] = 7;
            c.Deserialize(payload);
        },
        kVisual, 1);
}

TEST_CASE("every authored UI setter invalidates with its narrowest bit") {
    CheckNarrowestBit<RectTransform>(
        "RectTransform::SetAnchorMin",
        [](RectTransform& c) { c.SetAnchorMin({0.1f, 0.2f}); }, kLayout, 1);
    CheckNarrowestBit<RectTransform>(
        "RectTransform::SetAnchorMax",
        [](RectTransform& c) { c.SetAnchorMax({0.9f, 0.8f}); }, kLayout, 1);
    CheckNarrowestBit<RectTransform>(
        "RectTransform::SetAnchors",
        [](RectTransform& c) { c.SetAnchors({0.1f, 0.2f}, {0.9f, 0.8f}); },
        kLayout, 2);
    CheckNarrowestBit<RectTransform>(
        "RectTransform::SetPivot",
        [](RectTransform& c) { c.SetPivot({0.25f, 0.75f}); }, kLayout, 1);
    CheckNarrowestBit<RectTransform>(
        "RectTransform::SetAnchoredPosition",
        [](RectTransform& c) { c.SetAnchoredPosition({5.0f, 6.0f}); }, kLayout,
        1);
    CheckNarrowestBit<RectTransform>(
        "RectTransform::SetSizeDelta",
        [](RectTransform& c) { c.SetSizeDelta({12.0f, 34.0f}); }, kLayout, 1);

    CheckNarrowestBit<UIImage>(
        "UIImage::SetTextureGuid",
        [](UIImage& c) { c.SetTextureGuid("tex-a"); }, kVisual, 1);
    CheckNarrowestBit<UIImage>(
        "UIImage::SetTint",
        [](UIImage& c) { c.SetTint(Color{0.25f, 0.5f, 0.75f, 1.0f}); }, kVisual,
        1);
    CheckNarrowestBit<UIImage>(
        "UIImage::SetSortingOrder", [](UIImage& c) { c.SetSortingOrder(4); },
        kVisual, 1);

    CheckNarrowestBit<UIButton>(
        "UIButton::SetInteractable",
        [](UIButton& c) { c.SetInteractable(false); }, kInteraction, 1);
    CheckNarrowestBit<UIButton>(
        "UIButton::SetNormalColor",
        [](UIButton& c) { c.SetNormalColor(Color{0.1f, 0.2f, 0.3f, 1.0f}); },
        kVisual, 1);
    CheckNarrowestBit<UIButton>(
        "UIButton::SetHoverColor",
        [](UIButton& c) { c.SetHoverColor(Color{0.1f, 0.2f, 0.3f, 1.0f}); },
        kVisual, 1);
    CheckNarrowestBit<UIButton>(
        "UIButton::SetPressedColor",
        [](UIButton& c) { c.SetPressedColor(Color{0.1f, 0.2f, 0.3f, 1.0f}); },
        kVisual, 1);
    CheckNarrowestBit<UIButton>(
        "UIButton::SetDisabledColor",
        [](UIButton& c) { c.SetDisabledColor(Color{0.1f, 0.2f, 0.3f, 1.0f}); },
        kVisual, 1);
    CheckNarrowestBit<UIButton>(
        "UIButton::SetSortingOrder", [](UIButton& c) { c.SetSortingOrder(4); },
        kVisual, 1);

    CheckNarrowestBit<UICanvas>(
        "UICanvas::SetScaleMode",
        [](UICanvas& c) { c.SetScaleMode(UICanvasScaleMode::ConstantPixelSize); },
        kLayout, 1);
    CheckNarrowestBit<UICanvas>(
        "UICanvas::SetReferenceResolution",
        [](UICanvas& c) { c.SetReferenceResolution({1920.0f, 1080.0f}); },
        kLayout, 1);
    CheckNarrowestBit<UICanvas>(
        "UICanvas::SetMatchWidthOrHeight",
        [](UICanvas& c) { c.SetMatchWidthOrHeight(0.25f); }, kLayout, 1);
    CheckNarrowestBit<UICanvas>(
        "UICanvas::SetSortingOrder", [](UICanvas& c) { c.SetSortingOrder(4); },
        kVisual, 1);

    CheckNarrowestBit<UILabel>(
        "UILabel::SetText", [](UILabel& c) { c.SetText("t"); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetFontGuid", [](UILabel& c) { c.SetFontGuid("f"); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetFontFamilyGuid",
        [](UILabel& c) { c.SetFontFamilyGuid("fam"); }, kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetFontSizePx", [](UILabel& c) { c.SetFontSizePx(48.0f); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetLineSpacing", [](UILabel& c) { c.SetLineSpacing(2.0f); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetColor",
        [](UILabel& c) { c.SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f}); }, kVisual,
        1);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetLocale", [](UILabel& c) { c.SetLocale("ko-KR"); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetBaseDirection",
        [](UILabel& c) {
            c.SetBaseDirection(molga::text::BaseDirection::RightToLeft);
        },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetWrapMode",
        [](UILabel& c) { c.SetWrapMode(molga::text::TextWrapMode::Word); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetOverflowMode",
        [](UILabel& c) {
            c.SetOverflowMode(molga::text::TextOverflowMode::Clip);
        },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetMaxLines", [](UILabel& c) { c.SetMaxLines(3); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetHorizontalAlignment",
        [](UILabel& c) {
            c.SetHorizontalAlignment(UILabel::HorizontalAlignment::Right);
        },
        kVisual, 1);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetVerticalAlignment",
        [](UILabel& c) {
            c.SetVerticalAlignment(UILabel::VerticalAlignment::Bottom);
        },
        kVisual, 1);
    CheckNarrowestBit<UILabel>(
        "UILabel::SetSortingOrder", [](UILabel& c) { c.SetSortingOrder(4); },
        kVisual, 1);

    CheckNarrowestBit<UILayoutElement>(
        "UILayoutElement::SetHorizontal",
        [](UILayoutElement& c) { c.SetHorizontal({2.0f, 6.0f, 1.5f}); }, kLayout,
        1);
    CheckNarrowestBit<UILayoutElement>(
        "UILayoutElement::SetVertical",
        [](UILayoutElement& c) { c.SetVertical({3.0f, 4.0f, 0.5f}); }, kLayout,
        1);
    CheckNarrowestBit<UILayoutElement>(
        "UILayoutElement::SetIgnoreLayout",
        [](UILayoutElement& c) { c.SetIgnoreLayout(true); }, kLayout, 1);

    CheckNarrowestBit<UIContentSizeFitter>(
        "UIContentSizeFitter::SetHorizontalFit",
        [](UIContentSizeFitter& c) { c.SetHorizontalFit(UIFitMode::Min); },
        kLayout, 1);
    CheckNarrowestBit<UIContentSizeFitter>(
        "UIContentSizeFitter::SetVerticalFit",
        [](UIContentSizeFitter& c) { c.SetVerticalFit(UIFitMode::Preferred); },
        kLayout, 1);

    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetMode",
        [](UILayoutGroup& c) { c.SetMode(UILayoutMode::Grid); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetPadding",
        [](UILayoutGroup& c) { c.SetPadding(1.0f, 2.0f, 3.0f, 4.0f); }, kLayout,
        1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetSpacingX",
        [](UILayoutGroup& c) { c.SetSpacingX(5.0f); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetSpacingY",
        [](UILayoutGroup& c) { c.SetSpacingY(6.0f); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetChildHorizontalAlignment",
        [](UILayoutGroup& c) {
            c.SetChildHorizontalAlignment(
                molga::text::TextHorizontalAlignment::Right);
        },
        kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetChildVerticalAlignment",
        [](UILayoutGroup& c) {
            c.SetChildVerticalAlignment(
                molga::text::TextVerticalAlignment::Middle);
        },
        kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetControlChildWidth",
        [](UILayoutGroup& c) { c.SetControlChildWidth(true); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetControlChildHeight",
        [](UILayoutGroup& c) { c.SetControlChildHeight(true); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetChildForceExpandWidth",
        [](UILayoutGroup& c) { c.SetChildForceExpandWidth(true); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetChildForceExpandHeight",
        [](UILayoutGroup& c) { c.SetChildForceExpandHeight(true); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetCellSize",
        [](UILayoutGroup& c) { c.SetCellSize(24.0f, 12.0f); }, kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetStartCorner",
        [](UILayoutGroup& c) { c.SetStartCorner(UIGridStartCorner::LowerRight); },
        kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetFillAxis",
        [](UILayoutGroup& c) { c.SetFillAxis(UIGridFillAxis::Vertical); },
        kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetGridConstraint",
        [](UILayoutGroup& c) {
            c.SetGridConstraint(UIGridConstraint::FixedColumns);
        },
        kLayout, 1);
    CheckNarrowestBit<UILayoutGroup>(
        "UILayoutGroup::SetConstraintCount",
        [](UILayoutGroup& c) { c.SetConstraintCount(3); }, kLayout, 1);

    // 잘림은 배치와 히트 양쪽을 바꾸므로 이유를 두 번 올린다.
    CheckNarrowestBit<UIMask>(
        "UIMask::SetClipsDescendants",
        [](UIMask& c) { c.SetClipsDescendants(false); },
        kLayout | kInteraction, 2);

    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetViewport",
        [](UIScrollView& c) { c.SetViewport(SceneObjectRef{21}); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetContent",
        [](UIScrollView& c) { c.SetContent(SceneObjectRef{22}); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetHorizontal",
        [](UIScrollView& c) { c.SetHorizontal(false); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetVertical",
        [](UIScrollView& c) { c.SetVertical(false); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetMovement",
        [](UIScrollView& c) { c.SetMovement(UIScrollMovement::Elastic); },
        kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetElasticity",
        [](UIScrollView& c) { c.SetElasticity(0.25f); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetInertia",
        [](UIScrollView& c) { c.SetInertia(false); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetDecelerationRate",
        [](UIScrollView& c) { c.SetDecelerationRate(0.5f); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetScrollSensitivity",
        [](UIScrollView& c) { c.SetScrollSensitivity(2.5f); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetInitialNormalizedX",
        [](UIScrollView& c) { c.SetInitialNormalizedX(0.25f); }, kLayout, 1);
    CheckNarrowestBit<UIScrollView>(
        "UIScrollView::SetInitialNormalizedY",
        [](UIScrollView& c) { c.SetInitialNormalizedY(0.75f); }, kLayout, 1);

    CheckNarrowestBit<UISelectable>(
        "UISelectable::SetInteractable",
        [](UISelectable& c) { c.SetInteractable(false); }, kInteraction, 1);
    CheckNarrowestBit<UISelectable>(
        "UISelectable::SetNavigationMode",
        [](UISelectable& c) { c.SetNavigationMode(UINavigationMode::None); },
        kInteraction, 1);
    CheckNarrowestBit<UISelectable>(
        "UISelectable::SetNavigateUp",
        [](UISelectable& c) { c.SetNavigateUp(SceneObjectRef{31}); },
        kInteraction, 1);
    CheckNarrowestBit<UISelectable>(
        "UISelectable::SetNavigateDown",
        [](UISelectable& c) { c.SetNavigateDown(SceneObjectRef{32}); },
        kInteraction, 1);
    CheckNarrowestBit<UISelectable>(
        "UISelectable::SetNavigateLeft",
        [](UISelectable& c) { c.SetNavigateLeft(SceneObjectRef{33}); },
        kInteraction, 1);
    CheckNarrowestBit<UISelectable>(
        "UISelectable::SetNavigateRight",
        [](UISelectable& c) { c.SetNavigateRight(SceneObjectRef{34}); },
        kInteraction, 1);

    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetInitialText",
        [](UITextInput& c) { c.SetInitialText("t"); }, kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetReadOnly",
        [](UITextInput& c) { c.SetReadOnly(true); }, kInteraction, 1);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetMultiline",
        [](UITextInput& c) { c.SetMultiline(true); },
        kIntrinsic | kInteraction, 2);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetMaxGraphemes",
        [](UITextInput& c) { c.SetMaxGraphemes(17); }, kInteraction, 1);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetFontFamilyGuid",
        [](UITextInput& c) { c.SetFontFamilyGuid("fam"); },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetParagraphStyle",
        [](UITextInput& c) {
            UIAuthoredParagraphStyle style;
            style.fontSizePx = 32.0f;
            c.SetParagraphStyle(style);
        },
        kVisual | kIntrinsic, 2);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetTextViewport",
        [](UITextInput& c) { c.SetTextViewport(SceneObjectRef{11}); }, kLayout,
        1);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetRenderedLabel",
        [](UITextInput& c) { c.SetRenderedLabel(SceneObjectRef{12}); },
        kHierarchy, 1);
    CheckNarrowestBit<UITextInput>(
        "UITextInput::SetPlaceholderLabel",
        [](UITextInput& c) { c.SetPlaceholderLabel(SceneObjectRef{13}); },
        kHierarchy, 1);

    CheckNarrowestBit<UIAccessibility>(
        "UIAccessibility::SetRole",
        [](UIAccessibility& c) { c.SetRole(UIAccessibilityRole::Panel); },
        kInteraction, 1);
    CheckNarrowestBit<UIAccessibility>(
        "UIAccessibility::SetName",
        [](UIAccessibility& c) { c.SetName("n"); }, kInteraction, 1);
    CheckNarrowestBit<UIAccessibility>(
        "UIAccessibility::SetDescription",
        [](UIAccessibility& c) { c.SetDescription("d"); }, kInteraction, 1);
    CheckNarrowestBit<UIAccessibility>(
        "UIAccessibility::SetHidden",
        [](UIAccessibility& c) { c.SetHidden(true); }, kInteraction, 1);
}
