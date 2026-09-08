#include "doctest.h"

#include "Common/Fixed26_6.h"
#include "Core/World.h"
#include "ECS/ComponentFactory.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIAccessibility.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIContentSizeFitter.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Components/UILayoutElement.h"
#include "ECS/Components/UILayoutGroup.h"
#include "ECS/Components/UIMask.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/Components/UISelectable.h"
#include "ECS/Components/UITextInput.h"
#include "ECS/GameObject.h"
#include "Assets/FontArtifactStore.h"
#include "Common/Sha256.h"
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextShapingService.h"
#include "TextQualificationAssetTree.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutSystem.h"
#include "UI/UISystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIRuntimeInvalidation.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using molga::Fixed26_6;
using molga::FixedRect;
using molga::FixedSize;
using molga::text::TextDiagnosticCode;

namespace {

FixedSize RawSize(std::int32_t width, std::int32_t height) {
    return FixedSize{Fixed26_6::FromRaw(width), Fixed26_6::FromRaw(height)};
}

// 진단을 코드별로 셀 수 있는 sink. 억제도 중복 제거도 하지 않는다 — 그런
// 필터가 있으면 "정확히 하나"라는 주장이 sink 쪽 억제 덕에 통과할 수 있고,
// 그러면 rate-limit이 배치 시스템에 있는지 sink에 있는지 구분되지 않는다.
class CountingDiagnosticSink final : public molga::text::TextDiagnosticSink {
public:
    void Report(molga::text::TextDiagnostic diagnostic) override {
        records_.push_back(std::move(diagnostic));
    }
    std::size_t Count(TextDiagnosticCode code) const {
        std::size_t total = 0;
        for (const auto& record : records_) {
            if (record.code == code) ++total;
        }
        return total;
    }
    std::size_t Total() const noexcept { return records_.size(); }
    const std::vector<molga::text::TextDiagnostic>& Records() const noexcept {
        return records_;
    }
    void Clear() { records_.clear(); }

private:
    std::vector<molga::text::TextDiagnostic> records_;
};

// ── 저작 JSON에서 씬을 만든다 ────────────────────────────────────────────────
// 컴포넌트 값을 세터로 직접 밀어 넣지 않고 Deserialize를 통과시키는 이유는
// 두 가지다. 첫째, 케이스가 실제로 디스크에 저장되는 저작 형식을 시험한다.
// 둘째, 되돌리기/prefab override가 쓰는 그 경로이므로 세터를 우회한 회귀가
// 여기서도 드러난다.
Component* AddNamedComponent(GameObject& object, const std::string& typeName) {
    return ComponentFactory::Get().Create(typeName, &object);
}

void BuildAuthoredNode(World& world, const nlohmann::json& node,
                       GameObject* parent) {
    REQUIRE(node.contains("id"));
    auto object = std::make_shared<GameObject>(
        node.value("name", std::string("UI")));
    object->SetID(node["id"].get<unsigned int>());
    GameObject* raw = world.Add(object);
    REQUIRE(raw != nullptr);
    if (parent) REQUIRE(raw->SetParent(parent));
    if (node.contains("components")) {
        for (auto it = node["components"].begin();
             it != node["components"].end(); ++it) {
            Component* component = AddNamedComponent(*raw, it.key());
            REQUIRE(component != nullptr);
            component->Deserialize(it.value());
        }
    }
    // 활성 상태는 컴포넌트가 모두 붙은 뒤에 적용한다. 비활성 오브젝트에 자식을
    // 붙이는 것과 자식이 붙은 뒤 비활성으로 만드는 것은 다른 경로다.
    if (node.contains("children")) {
        for (const auto& child : node["children"]) {
            BuildAuthoredNode(world, child, raw);
        }
    }
    if (!node.value("active", true)) raw->SetActive(false);
}

// 저작된 rect 하나. 네 필드를 전부 비교하므로 x/y 또는 width/height를 뒤바꾼
// 구현이 통과할 수 없다.
struct ExpectedRect {
    unsigned int objectId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct LayoutCase {
    std::string name;
    nlohmann::json authoredJson;
    std::int32_t viewportWidthRaw = 0;
    std::int32_t viewportHeightRaw = 0;
    std::vector<ExpectedRect> expected;
};

// ── 저작 payload 조립기 ─────────────────────────────────────────────────────
// 컴포넌트가 실제로 디스크에 쓰는 키 이름과 값 모양 그대로다. 여기서 키를
// 하나 틀리면 Deserialize가 문서화된 기본값으로 되돌아가고, 그러면 케이스는
// 자기가 저작했다고 믿는 값이 아니라 기본값을 시험하게 된다 — 그래서 모든
// 키를 생략 없이 적는다.
nlohmann::json RectJson(float anchorMinX, float anchorMinY, float anchorMaxX,
                        float anchorMaxY, float pivotX, float pivotY,
                        float posX, float posY, float sizeX, float sizeY) {
    return nlohmann::json{{"schemaVersion", 1},
                          {"anchorMin", {anchorMinX, anchorMinY}},
                          {"anchorMax", {anchorMaxX, anchorMaxY}},
                          {"pivot", {pivotX, pivotY}},
                          {"anchoredPosition", {posX, posY}},
                          {"sizeDelta", {sizeX, sizeY}}};
}

nlohmann::json StretchRectJson() {
    return RectJson(0, 0, 1, 1, 0, 0, 0, 0, 0, 0);
}

nlohmann::json OffsetRectJson(float posX, float posY, float sizeX,
                              float sizeY) {
    return RectJson(0, 0, 0, 0, 0, 0, posX, posY, sizeX, sizeY);
}

nlohmann::json CanvasJson(const char* scaleMode, float referenceWidth,
                          float referenceHeight, float match,
                          int sortingOrder = 0) {
    return nlohmann::json{{"schemaVersion", 2},
                          {"scaleMode", scaleMode},
                          {"referenceResolution", {referenceWidth, referenceHeight}},
                          {"matchWidthOrHeight", match},
                          {"sortingOrder", sortingOrder}};
}

nlohmann::json ConstantCanvasJson() {
    return CanvasJson("ConstantPixelSize", 800.0f, 600.0f, 0.5f);
}

// 모든 키를 언제나 적는다. 기본값에 기대면 기본값이 바뀌는 순간 케이스가
// 조용히 다른 정책을 시험하게 된다.
nlohmann::json GroupJson(const char* mode, float padLeft, float padRight,
                         float padTop, float padBottom, float spacingX,
                         float spacingY, const char* horizontalAlignment,
                         const char* verticalAlignment, bool controlWidth,
                         bool controlHeight, bool expandWidth,
                         bool expandHeight, float cellX = 100.0f,
                         float cellY = 100.0f,
                         const char* startCorner = "UpperLeft",
                         const char* fillAxis = "Horizontal",
                         const char* gridConstraint = "Flexible",
                         std::uint32_t constraintCount = 1) {
    return nlohmann::json{{"schemaVersion", 1},
                          {"mode", mode},
                          {"paddingLeft", padLeft},
                          {"paddingRight", padRight},
                          {"paddingTop", padTop},
                          {"paddingBottom", padBottom},
                          {"spacingX", spacingX},
                          {"spacingY", spacingY},
                          {"childHorizontalAlignment", horizontalAlignment},
                          {"childVerticalAlignment", verticalAlignment},
                          {"controlChildWidth", controlWidth},
                          {"controlChildHeight", controlHeight},
                          {"childForceExpandWidth", expandWidth},
                          {"childForceExpandHeight", expandHeight},
                          {"cellSizeX", cellX},
                          {"cellSizeY", cellY},
                          {"startCorner", startCorner},
                          {"fillAxis", fillAxis},
                          {"gridConstraint", gridConstraint},
                          {"constraintCount", constraintCount}};
}

nlohmann::json AxisJson(float minimum, float preferred, float flexible) {
    return nlohmann::json{{"minimum", minimum},
                          {"preferred", preferred},
                          {"flexible", flexible}};
}

nlohmann::json ElementJson(nlohmann::json horizontal, nlohmann::json vertical,
                           bool ignoreLayout = false) {
    return nlohmann::json{{"schemaVersion", 1},
                          {"horizontal", std::move(horizontal)},
                          {"vertical", std::move(vertical)},
                          {"ignoreLayout", ignoreLayout}};
}

nlohmann::json FitterJson(const char* horizontal, const char* vertical) {
    return nlohmann::json{{"schemaVersion", 1},
                          {"horizontalFit", horizontal},
                          {"verticalFit", vertical}};
}

nlohmann::json Node(unsigned int id, nlohmann::json components,
                    nlohmann::json children = nlohmann::json::array(),
                    bool active = true) {
    return nlohmann::json{{"id", id},
                          {"active", active},
                          {"components", std::move(components)},
                          {"children", std::move(children)}};
}

} // namespace


namespace {

// 저작 JSON 하나를 실제 World로 만들고 한 번 배치하는 픽스처.
class AuthoredSceneFixture {
public:
    AuthoredSceneFixture(const nlohmann::json& root, FixedSize viewport)
        : viewport_(viewport) {
        BuildAuthoredNode(world_, root, nullptr);
    }

    molga::ui::UISnapshotPtr Build() {
        return system_.Build(world_, molga::WindowId{7}, viewport_, sink_);
    }

    World& GetWorld() noexcept { return world_; }
    molga::ui::UILayoutSystem& System() noexcept { return system_; }
    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }

private:
    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    FixedSize viewport_;
};

const std::vector<LayoutCase>& AllLayoutCases() {
    static const std::vector<LayoutCase> cases = [] {
        std::vector<LayoutCase> rows;

        // ── Step 1a ────────────────────────────────────────────────────────
        rows.push_back(
            {"horizontal-padding-spacing",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Horizontal", 10, 10, 5, 5, 4, 0, "Left", "Top",
                              false, false, false, false)}},
                  {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 30, 20)}}),
                   Node(3, {{"RectTransform", OffsetRectJson(0, 0, 30, 20)}}),
                   Node(4, {{"RectTransform", OffsetRectJson(0, 0, 30, 20)}})}),
             12800, 6400,
             {{1, 0, 0, 12800, 6400},
              {2, 640, 320, 1920, 1280},
              {3, 2816, 320, 1920, 1280},
              {4, 4992, 320, 1920, 1280}}});

        rows.push_back(
            {"vertical-cross-align",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Vertical", 0, 0, 0, 0, 0, 2, "Center", "Bottom",
                              false, false, false, false)}},
                  {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 30, 20)}}),
                   Node(3, {{"RectTransform", OffsetRectJson(0, 0, 50, 10)}})}),
             12800, 6400,
             {{1, 0, 0, 12800, 6400},
              {2, 5440, 4352, 1920, 1280},
              {3, 4800, 5760, 3200, 640}}});

        rows.push_back(
            {"authored-size-child",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Vertical", 1, 0, 2, 0, 0, 0, "Left", "Top",
                              false, false, false, false)}},
                  {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 30, 20)},
                            {"UILayoutElement",
                             ElementJson(AxisJson(50, 0, 0), AxisJson(0, 0, 0))}})}),
             12800, 6400,
             {{1, 0, 0, 12800, 6400}, {2, 64, 128, 3200, 1280}}});

        rows.push_back(
            {"control-expand",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Horizontal", 1, 0, 2, 0, 0, 0, "Left", "Top",
                              true, true, true, true)}},
                  {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 0, 0)},
                            {"UILayoutElement",
                             ElementJson(AxisJson(10, 20, 0), AxisJson(0, 0, 0))}}),
                   Node(3, {{"RectTransform", OffsetRectJson(0, 0, 0, 0)},
                            {"UILayoutElement",
                             ElementJson(AxisJson(10, 40, 1), AxisJson(0, 0, 0))}})}),
             12800, 6400,
             {{1, 0, 0, 12800, 6400},
              {2, 64, 128, 5728, 6272},
              {3, 5792, 128, 7008, 6272}}});

        rows.push_back(
            {"below-minimum-overflow",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Horizontal", 0, 0, 1, 0, 0, 0, "Left", "Top",
                              true, false, false, false)}},
                  {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 0, 5)},
                            {"UILayoutElement",
                             ElementJson(AxisJson(15, 15, 0), AxisJson(0, 0, 0))}}),
                   Node(3, {{"RectTransform", OffsetRectJson(0, 0, 0, 5)},
                            {"UILayoutElement",
                             ElementJson(AxisJson(15, 15, 0), AxisJson(0, 0, 0))}})}),
             1280, 640,
             {{1, 0, 0, 1280, 640},
              {2, 0, 64, 960, 320},
              {3, 960, 64, 960, 320}}});

        // ── Step 1b ────────────────────────────────────────────────────────
        auto gridChildren = [](unsigned int count) {
            nlohmann::json children = nlohmann::json::array();
            for (unsigned int i = 0; i < count; ++i) {
                children.push_back(
                    Node(2 + i, {{"RectTransform", OffsetRectJson(0, 0, 0, 0)}}));
            }
            return children;
        };

        rows.push_back(
            {"fixed-columns",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Grid", 1, 0, 2, 0, 1, 2, "Left", "Top", false,
                              false, false, false, 4, 3, "UpperLeft",
                              "Horizontal", "FixedColumns", 2)}},
                  gridChildren(5)),
             1280, 1024,
             {{1, 0, 0, 1280, 1024},
              {2, 64, 128, 256, 192},
              {3, 384, 128, 256, 192},
              {4, 64, 448, 256, 192},
              {5, 384, 448, 256, 192},
              {6, 64, 768, 256, 192}}});

        rows.push_back(
            {"fixed-rows",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Grid", 1, 0, 2, 0, 1, 2, "Left", "Top", false,
                              false, false, false, 4, 3, "UpperLeft",
                              "Vertical", "FixedRows", 2)}},
                  gridChildren(5)),
             1280, 1024,
             {{1, 0, 0, 1280, 1024},
              {2, 64, 128, 256, 192},
              {3, 64, 448, 256, 192},
              {4, 384, 128, 256, 192},
              {5, 384, 448, 256, 192},
              {6, 704, 128, 256, 192}}});

        rows.push_back(
            {"flexible-grid",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Grid", 1, 0, 2, 0, 1, 2, "Left", "Top", false,
                              false, false, false, 4, 3, "UpperLeft",
                              "Horizontal", "Flexible", 1)}},
                  gridChildren(6)),
             1280, 1024,
             {{1, 0, 0, 1280, 1024},
              {2, 64, 128, 256, 192},
              {3, 384, 128, 256, 192},
              {4, 704, 128, 256, 192},
              {5, 1024, 128, 256, 192},
              {6, 64, 448, 256, 192},
              {7, 384, 448, 256, 192}}});

        // 시작 모서리 두 방향을 따로 못 박는다. LowerRight 하나만 두면
        // 가로/세로 플래그를 서로 바꿔 쓴 구현이 통과한다.
        rows.push_back(
            {"grid-upper-right",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Grid", 1, 0, 2, 0, 1, 2, "Left", "Top", false,
                              false, false, false, 4, 3, "UpperRight",
                              "Horizontal", "FixedColumns", 2)}},
                  gridChildren(3)),
             1280, 1024,
             {{1, 0, 0, 1280, 1024},
              {2, 1024, 128, 256, 192},
              {3, 704, 128, 256, 192},
              {4, 1024, 448, 256, 192}}});

        rows.push_back(
            {"grid-lower-left",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Grid", 1, 0, 2, 0, 1, 2, "Left", "Top", false,
                              false, false, false, 4, 3, "LowerLeft",
                              "Horizontal", "FixedColumns", 2)}},
                  gridChildren(3)),
             1280, 1024,
             {{1, 0, 0, 1280, 1024},
              {2, 64, 832, 256, 192},
              {3, 384, 832, 256, 192},
              {4, 64, 512, 256, 192}}});

        // 형제 순서가 배치를 정한다. 오브젝트 id는 일부러 내림차순이 아니라
        // 뒤섞인 순서로 저작해 두었으므로, id로 정렬하는 구현은 여기서 죽는다.
        rows.push_back(
            {"sibling-order",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Horizontal", 0, 0, 1, 0, 0, 0, "Left", "Top",
                              false, false, false, false)}},
                  {Node(30, {{"RectTransform", OffsetRectJson(0, 0, 10, 5)}}),
                   Node(10, {{"RectTransform", OffsetRectJson(0, 0, 20, 5)}}),
                   Node(20, {{"RectTransform", OffsetRectJson(0, 0, 25, 5)}})}),
             4096, 1024,
             {{1, 0, 0, 4096, 1024},
              {30, 0, 64, 640, 320},
              {10, 640, 64, 1280, 320},
              {20, 1920, 64, 1600, 320}}});

        // ── Step 1c ────────────────────────────────────────────────────────
        rows.push_back(
            {"anchors",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()}},
                  {Node(2, {{"RectTransform",
                             RectJson(0.25f, 0.5f, 0.75f, 0.5f, 0.5f, 1.0f,
                                      10.0f, -5.0f, 8.0f, 4.0f)}})}),
             12800, 6400,
             {{1, 0, 0, 12800, 6400}, {2, 3584, 2624, 6912, 256}}});

        // 고정 픽셀 캔버스는 referenceResolution을 보지 않는다. 참조 해상도가
        // 뷰포트와 다르게 저작되어 있으므로, 그 값을 읽는 구현은 여기서 죽는다.
        rows.push_back(
            {"constant-pixel-canvas",
             Node(1,
                  {{"UICanvas",
                    CanvasJson("ConstantPixelSize", 800.0f, 600.0f, 0.5f)},
                   {"RectTransform", StretchRectJson()}},
                  {Node(2, {{"RectTransform",
                             RectJson(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f,
                                      0.0f, 10.0f, 5.0f)}})}),
             3200, 1600,
             {{1, 0, 0, 3200, 1600}, {2, 1280, 640, 640, 320}}});

        // match=0이면 폭이 기준이다: 1600x1200 픽셀 뷰포트에서 배율은 2이고
        // 논리 크기는 정확히 800x600이 된다.
        rows.push_back(
            {"scaled-viewport-canvas",
             Node(1,
                  {{"UICanvas",
                    CanvasJson("ScaleWithViewport", 800.0f, 600.0f, 0.0f)},
                   {"RectTransform", StretchRectJson()}},
                  {Node(2, {{"RectTransform",
                             OffsetRectJson(25.0f, 10.0f, 100.0f, 50.0f)}})}),
             102400, 76800,
             {{1, 0, 0, 51200, 38400}, {2, 1600, 640, 6400, 3200}}});

        // 드라이버 우선순위 Canvas > 부모 그룹 > 자기 fitter > 저작 rect.
        // id 2의 폭은 부모 그룹이 정하므로 fitter의 preferred(640)가 아니라
        // 2560이고, id 3의 높이는 부모에 그룹이 없으므로 fitter가 정해
        // 저작값(128)이 아니라 192다. 두 방향이 한 케이스에 함께 있다.
        rows.push_back(
            {"fitter-below-parent-driver",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Horizontal", 0, 0, 1, 0, 0, 0, "Left", "Top",
                              true, false, true, false)}},
                  {Node(2,
                        {{"RectTransform", OffsetRectJson(0, 0, 30, 5)},
                         {"UIContentSizeFitter",
                          FitterJson("Preferred", "Unconstrained")},
                         {"UILayoutElement",
                          ElementJson(AxisJson(0, 10, 0), AxisJson(0, 0, 0))}},
                        {Node(3,
                              {{"RectTransform", OffsetRectJson(1, 1, 5, 2)},
                               {"UIContentSizeFitter",
                                FitterJson("Unconstrained", "Preferred")},
                               {"UILayoutElement",
                                ElementJson(AxisJson(0, 0, 0),
                                            AxisJson(0, 3, 0))}})})}),
             2560, 640,
             {{1, 0, 0, 2560, 640},
              {2, 0, 64, 2560, 320},
              {3, 64, 128, 320, 192}}});

        // fitter가 크기를 바꾸면 원점도 pivot만큼 함께 움직여야 한다. 위
        // 케이스들은 pivot이 전부 (0,0)이라 이동량이 언제나 0이어서, 원점을
        // 그대로 두는 구현과 구분되지 않는다. 여기서만 pivot이 (0.5,0.5)다.
        rows.push_back(
            {"fitter-pivot-reanchor",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()}},
                  {Node(2, {{"RectTransform",
                             RectJson(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f,
                                      0.0f, 10.0f, 5.0f)},
                            {"UIContentSizeFitter",
                             FitterJson("Preferred", "Unconstrained")},
                            {"UILayoutElement",
                             ElementJson(AxisJson(0, 20, 0), AxisJson(0, 0, 0))}})}),
             3200, 1600,
             {{1, 0, 0, 3200, 1600}, {2, 960, 640, 1280, 320}}});

        // 비활성 조상은 자기 자신과 자손을 통째로 뺀다. 노드 수가 곧 증거다.
        rows.push_back(
            {"inactive-ancestor",
             Node(1,
                  {{"UICanvas", ConstantCanvasJson()},
                   {"RectTransform", StretchRectJson()},
                   {"UILayoutGroup",
                    GroupJson("Horizontal", 0, 0, 1, 0, 0, 0, "Left", "Top",
                              false, false, false, false)}},
                  {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 10, 5)}}),
                   Node(3, {{"RectTransform", OffsetRectJson(0, 0, 99, 9)}},
                        {Node(4, {{"RectTransform",
                                   OffsetRectJson(0, 0, 1, 1)}})},
                        false),
                   Node(5, {{"RectTransform", OffsetRectJson(0, 0, 20, 5)}})}),
             4096, 1024,
             {{1, 0, 0, 4096, 1024},
              {2, 0, 64, 640, 320},
              {5, 640, 64, 1280, 320}}});

        return rows;
    }();
    return cases;
}

} // namespace

TEST_CASE("authored UI layout resolves exact raw rectangles") {
    for (const auto& row : AllLayoutCases()) {
        CAPTURE(row.name);
        AuthoredSceneFixture fixture(
            row.authoredJson,
            RawSize(row.viewportWidthRaw, row.viewportHeightRaw));
        const auto snapshot = fixture.Build();
        REQUIRE(snapshot);
        CHECK(fixture.Diagnostics().Total() == 0);
        REQUIRE(snapshot->nodes.size() == row.expected.size());
        for (std::size_t i = 0; i < row.expected.size(); ++i) {
            CAPTURE(i);
            const auto& node = snapshot->nodes[i];
            const auto& want = row.expected[i];
            CHECK(node.rectTransform.objectId == want.objectId);
            CHECK(node.logicalRect.x.Raw() == want.x);
            CHECK(node.logicalRect.y.Raw() == want.y);
            CHECK(node.logicalRect.width.Raw() == want.width);
            CHECK(node.logicalRect.height.Raw() == want.height);
        }
    }
}

namespace {

// 남는 raw 한 칸이 어디로 가는가만 시험하는 최소 픽스처. 뷰포트를 raw로 직접
// 받는 이유는 193처럼 64로 나누어떨어지지 않는 폭이 있어야 나머지가 생기기
// 때문이다 — 논리 단위로 저작하면 언제나 64의 배수라 나머지가 없다.
class LayoutFixture {
public:
    class GroupBuilder {
    public:
        explicit GroupBuilder(UILayoutGroup& group) : group_(&group) {}
        GroupBuilder& ControlWidth(bool value) {
            group_->SetControlChildWidth(value);
            return *this;
        }
        GroupBuilder& ControlHeight(bool value) {
            group_->SetControlChildHeight(value);
            return *this;
        }
        GroupBuilder& ExpandWidth(bool value) {
            group_->SetChildForceExpandWidth(value);
            return *this;
        }
        GroupBuilder& ExpandHeight(bool value) {
            group_->SetChildForceExpandHeight(value);
            return *this;
        }

    private:
        UILayoutGroup* group_;
    };

    explicit LayoutFixture(FixedSize viewport) : viewport_(viewport) {
        auto rootObject = std::make_shared<GameObject>("Canvas");
        rootObject->SetID(1);
        root_ = world_.Add(rootObject);
        REQUIRE(root_ != nullptr);
        auto* canvas = root_->AddComponent<UICanvas>();
        canvas->SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
        auto* rect = root_->AddComponent<RectTransform>();
        rect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        rect->SetPivot({0.0f, 0.0f});
        rect->SetAnchoredPosition({0.0f, 0.0f});
        rect->SetSizeDelta({0.0f, 0.0f});
    }

    GroupBuilder HorizontalGroup() {
        auto* group = root_->AddComponent<UILayoutGroup>();
        group->SetMode(UILayoutMode::Horizontal);
        group->SetPadding(0.0f, 0.0f, 0.0f, 0.0f);
        group->SetSpacingX(0.0f);
        group->SetSpacingY(0.0f);
        return GroupBuilder(*group);
    }

    std::vector<unsigned int> AddEqualChildren(unsigned int count) {
        std::vector<unsigned int> ids;
        for (unsigned int i = 0; i < count; ++i) {
            auto child = std::make_shared<GameObject>("Child");
            child->SetID(100 + i);
            GameObject* raw = world_.Add(child);
            REQUIRE(raw != nullptr);
            REQUIRE(raw->SetParent(root_));
            auto* rect = raw->AddComponent<RectTransform>();
            rect->SetAnchors({0.0f, 0.0f}, {0.0f, 0.0f});
            rect->SetPivot({0.0f, 0.0f});
            rect->SetAnchoredPosition({0.0f, 0.0f});
            rect->SetSizeDelta({0.0f, 0.0f});
            ids.push_back(raw->GetID());
        }
        return ids;
    }

    molga::ui::UISnapshotPtr Build() {
        return system_.Build(world_, molga::WindowId{7}, viewport_, sink_);
    }

    // 노드가 없으면 값을 돌려주지 않고 그 자리에서 실패한다. 0을 돌려주면
    // 폭이 실제로 0인 정상 결과와 구분되지 않는다.
    std::int32_t WidthRaw(const molga::ui::UISnapshotPtr& snapshot,
                          unsigned int objectId) const {
        REQUIRE(snapshot);
        for (const auto& node : snapshot->nodes) {
            if (node.rectTransform.objectId == objectId) {
                return node.logicalRect.width.Raw();
            }
        }
        REQUIRE_MESSAGE(false, "no snapshot node for the requested object");
        return 0;
    }

    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }

private:
    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    FixedSize viewport_;
    GameObject* root_ = nullptr;
};

} // namespace

TEST_CASE("layout remainder follows sibling order") {
    LayoutFixture f({molga::Fixed26_6::FromRaw(193),
                     molga::Fixed26_6::FromRaw(64)});
    f.HorizontalGroup().ControlWidth(true).ExpandWidth(true);
    const auto ids = f.AddEqualChildren(3);
    const auto snapshot = f.Build();
    CHECK(f.WidthRaw(snapshot, ids[0]) == 65);
    CHECK(f.WidthRaw(snapshot, ids[1]) == 64);
    CHECK(f.WidthRaw(snapshot, ids[2]) == 64);
}

// ── Step 2a: SCC 저작 축 폴백 ────────────────────────────────────────────────
namespace {

nlohmann::json LabelJson(const char* text, const char* wrap) {
    return nlohmann::json{{"schemaVersion", 2},
                          {"text", text},
                          {"fontGuid", ""},
                          {"fontFamilyGuid", ""},
                          {"fontSizePx", 24.0f},
                          {"lineSpacing", 1.2f},
                          {"color", {1.0f, 1.0f, 1.0f, 1.0f}},
                          {"locale", "und"},
                          {"baseDirection", "Auto"},
                          {"wrap", wrap},
                          {"overflow", "Overflow"},
                          {"maxLines", 0},
                          {"horizontalAlignment", 0},
                          {"verticalAlignment", 0},
                          {"sortingOrder", 1}};
}

// 부모 그룹 -> 자식 고유 텍스트 -> content fitter -> 부모 크기 순환.
nlohmann::json TwoNodeCycleScene() {
    return Node(1,
                {{"UICanvas", ConstantCanvasJson()},
                 {"RectTransform", StretchRectJson()}},
                {Node(2,
                      {{"RectTransform", OffsetRectJson(1, 2, 30, 10)},
                       {"UILayoutGroup",
                        GroupJson("Horizontal", 0, 0, 0, 0, 0, 0, "Left", "Top",
                                  true, false, true, false)},
                       {"UIContentSizeFitter",
                        FitterJson("Preferred", "Unconstrained")}},
                      {Node(3,
                            {{"RectTransform", OffsetRectJson(2, 3, 10, 7)},
                             {"UILabel", LabelJson("cycle", "Word")}})})});
}

void PublishIntrinsic(World& world, unsigned int objectId,
                      const std::string& contentIdentity, FixedSize size) {
    GameObject* object = world.FindById(objectId);
    REQUIRE(object != nullptr);
    auto* label = object->GetComponent<UILabel>();
    REQUIRE(label != nullptr);
    const auto identity = molga::ui::CaptureTarget(world, *label);
    REQUIRE(static_cast<bool>(identity));
    molga::ui::UIIntrinsicLayoutRegistry::Get().Publish(identity,
                                                       contentIdentity, size);
}

} // namespace

TEST_CASE("a two-node layout cycle falls back to authored rectangles") {
    AuthoredSceneFixture fixture(TwoNodeCycleScene(), RawSize(12800, 6400));
    // 순환을 감지하지 못한 구현은 고유 크기 1280을 쓰게 되므로, 저작값 1920과
    // 확실히 다르다. 두 값이 같으면 이 케이스는 아무것도 증명하지 못한다.
    PublishIntrinsic(fixture.GetWorld(), 3, "cycle-content", RawSize(1280, 448));

    const auto snapshot = fixture.Build();
    REQUIRE(snapshot);
    REQUIRE(snapshot->nodes.size() == 3);
    CHECK(snapshot->nodes[0].logicalRect == FixedRect{
              Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(0),
              Fixed26_6::FromRaw(12800), Fixed26_6::FromRaw(6400)});
    CHECK(snapshot->nodes[1].rectTransform.objectId == 2);
    CHECK(snapshot->nodes[1].logicalRect == FixedRect{
              Fixed26_6::FromRaw(64), Fixed26_6::FromRaw(128),
              Fixed26_6::FromRaw(1920), Fixed26_6::FromRaw(640)});
    CHECK(snapshot->nodes[2].rectTransform.objectId == 3);
    // 순환은 폭 축에만 있다. 그래서 x/폭은 저작 rect로 돌아가지만(192/640),
    // y/높이는 여전히 부모 그룹이 놓는다(128/448) — 축별 폴백이 아니라 노드
    // 전체를 저작으로 되돌리는 구현은 여기서 죽는다.
    CHECK(snapshot->nodes[2].logicalRect == FixedRect{
              Fixed26_6::FromRaw(192), Fixed26_6::FromRaw(128),
              Fixed26_6::FromRaw(640), Fixed26_6::FromRaw(448)});
    CHECK(fixture.Diagnostics().Count(TextDiagnosticCode::LayoutCycle) == 1);

    // cold / warm / 다른 편집 이력이 모두 같은 바이트를 내야 한다. warm 쪽은
    // 값을 바꿨다가 되돌린 뒤 다시 만든 것이라, 프레임마다 같은 포인터를
    // 돌려주는 빠른 경로가 아니라 실제 재계산을 거친다.
    const std::string cold = molga::ui::StableLayoutSnapshotJson(*snapshot);

    GameObject* child = fixture.GetWorld().FindById(3);
    REQUIRE(child != nullptr);
    auto* childRect = child->GetComponent<RectTransform>();
    REQUIRE(childRect != nullptr);
    childRect->SetSizeDelta({99.0f, 77.0f});
    const auto disturbed = fixture.Build();
    REQUIRE(disturbed);
    REQUIRE(disturbed.get() != snapshot.get());
    childRect->SetSizeDelta({10.0f, 7.0f});
    const auto warm = fixture.Build();
    REQUIRE(warm);
    CHECK(molga::ui::StableLayoutSnapshotJson(*warm) == cold);

    AuthoredSceneFixture other(TwoNodeCycleScene(), RawSize(12800, 6400));
    PublishIntrinsic(other.GetWorld(), 3, "cycle-content", RawSize(1280, 448));
    GameObject* otherChild = other.GetWorld().FindById(3);
    REQUIRE(otherChild != nullptr);
    auto* otherLabel = otherChild->GetComponent<UILabel>();
    REQUIRE(otherLabel != nullptr);
    otherLabel->SetText("a different edit history");
    otherLabel->SetText("cycle");
    const auto otherSnapshot = other.Build();
    REQUIRE(otherSnapshot);
    CHECK(molga::ui::StableLayoutSnapshotJson(*otherSnapshot) == cold);
}

// 길이 1(자기 참조) 순환. 폭을 스스로의 폭에 의존하는 내용(줄바꿈 텍스트)에서
// 가져오는 fitter가 만든다. Tarjan에서 자기 간선은 size > 1 조건에 걸리지
// 않으므로, 이 케이스는 size > 1만 보는 구현에서만 죽는다.
TEST_CASE("a self-referential layout cycle falls back to authored rectangles") {
    AuthoredSceneFixture fixture(
        Node(1,
             {{"UICanvas", ConstantCanvasJson()},
              {"RectTransform", StretchRectJson()}},
             {Node(2, {{"RectTransform", OffsetRectJson(1, 2, 10, 5)},
                       {"UILabel", LabelJson("wrapping", "Word")},
                       {"UIContentSizeFitter",
                        FitterJson("Preferred", "Unconstrained")}})}),
        RawSize(12800, 6400));
    PublishIntrinsic(fixture.GetWorld(), 2, "self-content", RawSize(1280, 448));

    const auto snapshot = fixture.Build();
    REQUIRE(snapshot);
    REQUIRE(snapshot->nodes.size() == 2);
    CHECK(snapshot->nodes[1].rectTransform.objectId == 2);
    CHECK(snapshot->nodes[1].logicalRect == FixedRect{
              Fixed26_6::FromRaw(64), Fixed26_6::FromRaw(128),
              Fixed26_6::FromRaw(640), Fixed26_6::FromRaw(320)});
    CHECK(fixture.Diagnostics().Count(TextDiagnosticCode::LayoutCycle) == 1);
}

// 길이 3 순환. 2-순환만 다루는 구현(예: 부모/자식 쌍만 검사)이 여기서 죽는다.
TEST_CASE("a three-node layout cycle falls back to authored rectangles") {
    AuthoredSceneFixture fixture(
        Node(1,
             {{"UICanvas", ConstantCanvasJson()},
              {"RectTransform", StretchRectJson()}},
             {Node(2,
                   {{"RectTransform", OffsetRectJson(1, 2, 40, 10)},
                    {"UILayoutGroup",
                     GroupJson("Horizontal", 0, 0, 0, 0, 0, 0, "Left", "Top",
                               true, false, true, false)},
                    {"UIContentSizeFitter",
                     FitterJson("Preferred", "Unconstrained")}},
                   {Node(3,
                         {{"RectTransform", OffsetRectJson(2, 3, 20, 7)},
                          {"UILayoutGroup",
                           GroupJson("Horizontal", 0, 0, 0, 0, 0, 0, "Left",
                                     "Top", true, false, true, false)},
                          {"UIContentSizeFitter",
                           FitterJson("Preferred", "Unconstrained")}},
                         {Node(4,
                               {{"RectTransform", OffsetRectJson(1, 1, 5, 3)},
                                {"UILayoutElement",
                                 ElementJson(AxisJson(0, 3, 0),
                                             AxisJson(0, 0, 0))}})})})}),
        RawSize(12800, 6400));

    const auto snapshot = fixture.Build();
    REQUIRE(snapshot);
    REQUIRE(snapshot->nodes.size() == 4);
    CHECK(snapshot->nodes[1].logicalRect == FixedRect{
              Fixed26_6::FromRaw(64), Fixed26_6::FromRaw(128),
              Fixed26_6::FromRaw(2560), Fixed26_6::FromRaw(640)});
    CHECK(snapshot->nodes[2].logicalRect == FixedRect{
              Fixed26_6::FromRaw(192), Fixed26_6::FromRaw(128),
              Fixed26_6::FromRaw(1280), Fixed26_6::FromRaw(448)});
    CHECK(snapshot->nodes[3].logicalRect == FixedRect{
              Fixed26_6::FromRaw(256), Fixed26_6::FromRaw(128),
              Fixed26_6::FromRaw(320), Fixed26_6::FromRaw(192)});
    CHECK(fixture.Diagnostics().Count(TextDiagnosticCode::LayoutCycle) == 1);
}

// 순환이 없는 그래프에서는 진단이 하나도 나오지 않아야 한다. 이 증인이 없으면
// 위 세 케이스는 "언제나 순환이라고 답하는" 구현에서도 전부 통과한다.
TEST_CASE("an acyclic driver graph reports no layout cycle") {
    AuthoredSceneFixture fixture(
        Node(1,
             {{"UICanvas", ConstantCanvasJson()},
              {"RectTransform", StretchRectJson()},
              {"UILayoutGroup",
               GroupJson("Horizontal", 0, 0, 0, 0, 0, 0, "Left", "Top", true,
                         false, true, false)}},
             {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 10, 5)},
                       {"UILayoutElement",
                        ElementJson(AxisJson(0, 10, 0), AxisJson(0, 0, 0))}})}),
        RawSize(2560, 640));
    const auto snapshot = fixture.Build();
    REQUIRE(snapshot);
    CHECK(fixture.Diagnostics().Count(TextDiagnosticCode::LayoutCycle) == 0);
    REQUIRE(snapshot->nodes.size() == 2);
    // 부모 그룹이 실제로 폭을 정했다는 증인. 저작값 640이 그대로 남으면
    // 그래프가 아니라 저작 rect가 답한 것이다.
    CHECK(snapshot->nodes[1].logicalRect.width.Raw() == 2560);
}

// ── Step 2b-2j: 캐시 ────────────────────────────────────────────────────────
namespace {

// 캐시 시험용 씬 하나. 라벨/이미지/선택/마스크가 한 오브젝트에 모여 있어
// "시각·상호작용 편집은 기하를 재사용하고 전체 스냅샷만 놓친다"를 한 픽스처에서
// 양쪽으로 관찰할 수 있다.
nlohmann::json CacheSceneJson() {
    return Node(1,
                 {{"UICanvas", ConstantCanvasJson()},
                  {"RectTransform", StretchRectJson()}},
                 {Node(2,
                       {{"RectTransform", OffsetRectJson(1, 2, 10, 5)},
                        {"UILabel", LabelJson("cache", "NoWrap")},
                        {"UIImage",
                         {{"schemaVersion", 1},
                          {"textureGuid", "texture-a"},
                          {"tint", {1.0f, 1.0f, 1.0f, 1.0f}},
                          {"sortingOrder", 0}}},
                        {"UISelectable",
                         {{"schemaVersion", 1},
                          {"interactable", true},
                          {"navigationMode", "Auto"},
                          {"navigateUp", {{"targetId", 0}}},
                          {"navigateDown", {{"targetId", 0}}},
                          {"navigateLeft", {{"targetId", 0}}},
                          {"navigateRight", {{"targetId", 0}}}}},
                        {"UIMask",
                         {{"schemaVersion", 1}, {"clipsDescendants", false}}}},
                       {Node(3, {{"RectTransform",
                                  OffsetRectJson(1, 1, 5, 2)}})})});
}

class UILayoutCacheFixture {
public:
    UILayoutCacheFixture() {
        BuildAuthoredNode(world_, CacheSceneJson(), nullptr);
    }

    molga::ui::UISnapshotPtr Build() {
        auto snapshot = system_.Build(world_, window_, viewport_, sink_);
        if (snapshot && !oldestGeometryKey_) {
            oldestGeometryKey_ = system_.LastGeometryKey();
        }
        return snapshot;
    }

    World& GetWorld() noexcept { return world_; }
    molga::ui::UILayoutSystem& System() noexcept { return system_; }
    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }
    FixedSize Viewport() const noexcept { return viewport_; }

    UILabel& Label() { return *Component<UILabel>(2); }
    UIImage& Image() { return *Component<UIImage>(2); }
    UISelectable& Selectable() { return *Component<UISelectable>(2); }
    UIMask& Mask() { return *Component<UIMask>(2); }

    std::uint64_t GeometryBuildCount() const noexcept {
        return system_.GeometryBuildCount();
    }
    std::uint64_t SnapshotKeyBuildCount() const noexcept {
        return system_.SnapshotKeyBuildCount();
    }
    std::uint64_t SnapshotKeyAllocationCount() const noexcept {
        return system_.SnapshotKeyAllocationCount();
    }
    std::size_t GeometryCacheEntryCountForWorld() const noexcept {
        return system_.GeometryCacheEntryCountForWorld(world_.Generation());
    }
    std::size_t FullSnapshotCacheEntryCountForWorldDevice() const noexcept {
        molga::ui::UISnapshotWorldDeviceSlotKey slot;
        slot.worldGeneration = world_.Generation();
        slot.deviceGeneration =
            molga::ui::UIRuntimeInvalidationClock::Current().deviceGeneration;
        return system_.FullSnapshotCacheEntryCountForWorldDevice(slot);
    }
    bool GeometryCacheContainsOldestFixtureKey() const {
        REQUIRE(oldestGeometryKey_.has_value());
        return system_.GeometryCacheContains(*oldestGeometryKey_);
    }

    // 스크롤/커서 깜빡임처럼 기하를 바꾸지 않고 게시된 스냅샷만 바꾸는 편집.
    // 값마다 정확히 표현되는 이분수를 골라, 부동소수 반올림 때문에 두 번째
    // 값이 첫 번째와 같아지는 일이 없게 한다.
    void SetRuntimeVisualRevision(std::uint32_t revision) {
        const float red = static_cast<float>(revision & 0x3Fu) / 64.0f;
        const float green = static_cast<float>((revision >> 6) & 0x3Fu) / 64.0f;
        Label().SetColor(Color{red, green, 0.5f, 1.0f});
    }

    void SetDistinctLayoutConstraintRaw(int widthRaw) {
        viewport_ = FixedSize{Fixed26_6::FromRaw(widthRaw), viewport_.height};
    }

    void SetSurfaceWindow(molga::WindowId window) { window_ = window; }

private:
    template <typename T>
    T* Component(unsigned int objectId) {
        GameObject* object = world_.FindById(objectId);
        REQUIRE(object != nullptr);
        T* component = object->GetComponent<T>();
        REQUIRE(component != nullptr);
        return component;
    }

    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    FixedSize viewport_ = RawSize(640, 384);
    molga::WindowId window_ = 7;
    std::optional<molga::ui::UILayoutGeometryCacheKey> oldestGeometryKey_;
};

} // namespace

TEST_CASE("an unchanged frame returns the identical snapshot") {
    UILayoutCacheFixture fixture;
    auto& layout = fixture.System();
    auto& world = fixture.GetWorld();
    const auto viewport = fixture.Viewport();
    auto& diagnostics = fixture.Diagnostics();

    const auto first = layout.Build(world, molga::WindowId{7}, viewport,
                                    diagnostics);
    const auto second = layout.Build(world, molga::WindowId{7}, viewport,
                                     diagnostics);
    REQUIRE(first);
    CHECK(first.get() == second.get());

    // 다른 창에 같은 world/viewport를 그리면 전체 스냅샷은 놓쳐야 하지만
    // 기하는 그대로 재사용된다. 그리고 정규 바이트는 창과 무관해야 한다.
    const auto geometryBuilds = layout.GeometryBuildCount();
    const auto onWindowEight = layout.Build(world, molga::WindowId{8}, viewport,
                                            diagnostics);
    REQUIRE(onWindowEight);
    CHECK(onWindowEight.get() != second.get());
    CHECK(layout.GeometryBuildCount() == geometryBuilds);
    CHECK(onWindowEight->surfaceWindowId == 8);
    CHECK(molga::ui::StableLayoutSnapshotJson(*onWindowEight) ==
          molga::ui::StableLayoutSnapshotJson(*first));

    const auto backOnSeven = layout.Build(world, molga::WindowId{7}, viewport,
                                          diagnostics);
    REQUIRE(backOnSeven);
    CHECK(backOnSeven.get() != onWindowEight.get());
    CHECK(backOnSeven->surfaceWindowId == 7);
    CHECK(layout.GeometryBuildCount() == geometryBuilds);

    // 창을 오갈 때마다 항목이 쌓이면 안 된다. 한 world/device 조합에는 언제나
    // 최신 하나뿐이다.
    molga::ui::UISnapshotWorldDeviceSlotKey slot;
    slot.worldGeneration = world.Generation();
    slot.deviceGeneration =
        molga::ui::UIRuntimeInvalidationClock::Current().deviceGeneration;
    CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slot) == 1);
}

TEST_CASE("payload-only edits miss snapshot cache but reuse geometry") {
    UILayoutCacheFixture f;
    const auto first = f.Build();
    const auto geometryBuilds = f.GeometryBuildCount();
    f.Label().SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
    const auto recolored = f.Build();
    REQUIRE(first);
    REQUIRE(recolored);
    CHECK(first.get() != recolored.get());
    CHECK(f.GeometryBuildCount() == geometryBuilds);
    f.Selectable().SetInteractable(false);
    const auto disabled = f.Build();
    CHECK(recolored.get() != disabled.get());
    CHECK(f.GeometryBuildCount() == geometryBuilds);
}

TEST_CASE("exhausted UI revision bypasses every snapshot cache") {
    UILayoutCacheFixture f;
    f.Label().SetAuthoredRevisionForTesting(UINT64_MAX);
    f.Label().SetColor(Color{0.2f, 0.3f, 0.4f, 1.0f});
    const auto first = f.Build();
    const auto second = f.Build();
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.get() != second.get());
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
}

// 편집 한 가지마다 새 픽스처 하나. 전부 전체 스냅샷을 놓쳐야 하고, 순수한
// 시각/상호작용 편집만 기하 항목을 그대로 재사용한다. 두 기대값이 한 표에
// 함께 있어야 "언제나 다시 만든다"와 "언제나 재사용한다"가 모두 죽는다.
TEST_CASE("each authored edit misses the snapshot cache") {
    struct EditRow {
        const char* name;
        void (*apply)(UILayoutCacheFixture&);
        bool retainsGeometry;
    };

    const EditRow rows[] = {
        {"label-text",
         [](UILayoutCacheFixture& f) { f.Label().SetText("changed"); }, true},
        {"label-color",
         [](UILayoutCacheFixture& f) {
             f.Label().SetColor(Color{0.25f, 0.5f, 0.75f, 1.0f});
         },
         true},
        {"immutable-layout-identity",
         [](UILayoutCacheFixture& f) {
             PublishIntrinsic(f.GetWorld(), 2, "next-content",
                              RawSize(1024, 256));
         },
         false},
        {"image-tint",
         [](UILayoutCacheFixture& f) {
             f.Image().SetTint(Color{0.5f, 0.25f, 0.125f, 1.0f});
         },
         true},
        {"texture-content-sha",
         [](UILayoutCacheFixture& f) {
             molga::ui::UITextureContentIdentity identity;
             identity.contentSha256 = std::string(64, 'b');
             identity.contentStableId = 4242;
             CHECK(molga::ui::UITextureContentRegistry::Get().Publish(
                 "texture-a", identity));
         },
         true},
        {"selectable-interactable",
         [](UILayoutCacheFixture& f) { f.Selectable().SetInteractable(false); },
         true},
        {"explicit-navigation-ref",
         [](UILayoutCacheFixture& f) {
             SceneObjectRef ref;
             ref.targetId = 3;
             f.Selectable().SetNavigateUp(ref);
         },
         true},
        {"mask-enabled",
         [](UILayoutCacheFixture& f) { f.Mask().SetClipsDescendants(true); },
         false},
    };

    for (const auto& row : rows) {
        CAPTURE(row.name);
        UILayoutCacheFixture f;
        const auto before = f.Build();
        REQUIRE(before);
        const auto geometryBuilds = f.GeometryBuildCount();
        row.apply(f);
        const auto after = f.Build();
        REQUIRE(after);
        CHECK(before.get() != after.get());
        if (row.retainsGeometry) {
            CHECK(f.GeometryBuildCount() == geometryBuilds);
        } else {
            CHECK(f.GeometryBuildCount() == geometryBuilds + 1);
        }
    }
}

TEST_CASE("warm static UI bypasses snapshot key construction and allocation") {
    UILayoutCacheFixture f;
    const auto warm = f.Build();
    REQUIRE(warm);
    const auto keyAllocations = f.SnapshotKeyAllocationCount();
    const auto keyBuilds = f.SnapshotKeyBuildCount();
    for (int frame = 0; frame < 600; ++frame) {
        CHECK(f.Build().get() == warm.get());
    }
    CHECK(f.SnapshotKeyBuildCount() == keyBuilds);
    CHECK(f.SnapshotKeyAllocationCount() == keyAllocations);

    // 증인: 계수기가 상수로 굳어 있으면 위 등식은 공허하다. 실제로 달라진
    // 프레임은 키를 다시 만든다.
    f.Label().SetText("changed");
    REQUIRE(f.Build());
    CHECK(f.SnapshotKeyBuildCount() > keyBuilds);
}

TEST_CASE("UI snapshot and geometry caches remain bounded under churn") {
    UILayoutCacheFixture f;
    for (std::uint32_t i = 0; i < 4096; ++i) {
        f.SetRuntimeVisualRevision(i + 1); // scroll/blink-style full miss
        REQUIRE(f.Build());
        CHECK(f.FullSnapshotCacheEntryCountForWorldDevice() <= 1);
    }
    for (std::uint32_t i = 0; i < 2048; ++i) {
        f.SetDistinctLayoutConstraintRaw(640 + static_cast<int>(i));
        REQUIRE(f.Build());
        CHECK(f.GeometryCacheEntryCountForWorld() <= 256);
    }
    CHECK(f.GeometryCacheEntryCountForWorld() == 256);
    CHECK_FALSE(f.GeometryCacheContainsOldestFixtureKey());
}

// 뷰포트 세대가 단순 카운터이면 서로 다른 두 표면이 번갈아 그릴 때 이름이
// 1,2,3,4…로 올라가고, 같은 표면이 같은 뷰포트로 돌아와도 예전 기하를 되찾지
// 못한다. 에디터가 정확히 그 모양이다 — Scene View는 패널 픽셀, Game View는
// 게임 논리 크기, 같은 프레임, 같은 월드. 그러면 기하 LRU는 100% 미스가 되어
// 프레임마다 두 번의 완전한 배치를 새로 한다.
// 클립으로 떨어진 노드는 기하의 일부다. 기하 캐시가 적중하면 ComputeClips가
// 돌지 않으므로, 그 사실을 항목과 함께 복원하지 않으면 모든 dropped가 false로
// 남고 잘려 나갔어야 할 노드가 시각 키에 들어온다. 같은 상태가 캐시 적중 여부에
// 따라 서로 다른 키를 만드는 셈이다. 스냅샷 내용은 캐시된 노드 벡터에서 오므로
// 겉으로는 같아 보이고, 전역 의미 세대가 모든 편집에서 오르기 때문에 포인터
// 동일성으로도 잡히지 않는다 — 키 항목 수가 이 차이를 보는 유일한 자리다.
// 줄바꿈 모드는 기하를 결정한다 — HasSizeDependentIntrinsicWidth가 그것을 읽어
// SCC 자기 간선을 세우기 때문이다. 라벨 revision이 기하 키에 없으면 줄바꿈을
// 바꿔도 같은 키가 나와 낡은 기하가 그대로 적중한다.
TEST_CASE("toggling label wrap mode invalidates cached geometry") {
    molga::ui::UILayoutSystem layout;
    CountingDiagnosticSink sink;
    World world;
    const auto viewport = RawSize(12800, 6400);
    BuildAuthoredNode(
        world,
        Node(1,
             {{"UICanvas", ConstantCanvasJson()},
              {"RectTransform", StretchRectJson()}},
             {Node(2, {{"RectTransform", OffsetRectJson(0, 0, 40, 20)},
                       {"UILabel", LabelJson("wrap me please", "Word")}})}),
        nullptr);

    REQUIRE(layout.Build(world, molga::WindowId{5}, viewport, sink));
    const auto warmBuilds = layout.GeometryBuildCount();
    // 아무것도 바꾸지 않으면 기하는 다시 만들어지지 않는다.
    REQUIRE(layout.Build(world, molga::WindowId{5}, viewport, sink));
    CHECK(layout.GeometryBuildCount() == warmBuilds);

    auto* label = world.FindById(2)->GetComponent<UILabel>();
    REQUIRE(label != nullptr);
    label->SetWrapMode(molga::text::TextWrapMode::NoWrap);
    REQUIRE(layout.Build(world, molga::WindowId{5}, viewport, sink));
    // 줄바꿈이 달라졌으니 기하를 다시 만들어야 한다.
    CHECK(layout.GeometryBuildCount() > warmBuilds);
}

TEST_CASE("a geometry hit keeps clipped-out nodes out of the visual key") {
    molga::ui::UILayoutSystem layout;
    CountingDiagnosticSink sink;
    World world;
    const auto viewport = RawSize(12800, 6400);
    BuildAuthoredNode(
        world,
        Node(1,
             {{"UICanvas", ConstantCanvasJson()},
              {"RectTransform", StretchRectJson()}},
             {Node(2,
                   {{"RectTransform", OffsetRectJson(0, 0, 40, 40)},
                    {"UIMask",
                     {{"schemaVersion", 1}, {"clipsDescendants", true}}}},
                   {// 마스크 안쪽: 키에 남아야 한다.
                    Node(3, {{"RectTransform", OffsetRectJson(0, 0, 10, 10)},
                             {"UIImage", {{"schemaVersion", 1},
                              {"textureGuid", "texture-inside"},
                              {"tint", {1.0f, 1.0f, 1.0f, 1.0f}},
                              {"sortingOrder", 0}}}}),
                    // 마스크와 전혀 겹치지 않는다: 떨어져야 한다.
                    Node(4, {{"RectTransform", OffsetRectJson(900, 900, 10, 10)},
                             {"UIImage", {{"schemaVersion", 1},
                              {"textureGuid", "texture-outside"},
                              {"tint", {1.0f, 1.0f, 1.0f, 1.0f}},
                              {"sortingOrder", 0}}}})})}),
        nullptr);

    const auto cold = layout.Build(world, molga::WindowId{3}, viewport, sink);
    REQUIRE(cold);
    const auto coldEntries = layout.LastVisualKeyEntryCount();
    // 안쪽 하나만 세어야 한다. 둘이면 클립이 키에 반영되지 않은 것이다.
    CHECK(coldEntries == 1);
    const auto coldGeometryBuilds = layout.GeometryBuildCount();

    // 기하에 영향이 없는 편집이라야 기하는 적중하고 키는 다시 만들어진다.
    world.FindById(3)->GetComponent<UIImage>()->SetTint({0.5f, 0.5f, 0.5f, 1.0f});
    const auto warm = layout.Build(world, molga::WindowId{3}, viewport, sink);
    REQUIRE(warm);
    CHECK(layout.GeometryBuildCount() == coldGeometryBuilds);
    // 적중한 빌드도 차가운 빌드와 같은 키를 만들어야 한다.
    CHECK(layout.LastVisualKeyEntryCount() == coldEntries);
}

TEST_CASE("alternating viewports still hit geometry when one returns") {
    molga::ui::UILayoutSystem layout;
    CountingDiagnosticSink sink;
    World world;
    BuildAuthoredNode(world, CacheSceneJson(), nullptr);
    const auto panelViewport = RawSize(640, 384);
    const auto gameViewport = RawSize(1280, 720);
    REQUIRE(panelViewport != gameViewport);

    // 두 뷰포트를 한 번씩 지나 각자의 기하를 만든다.
    REQUIRE(layout.Build(world, molga::WindowId{7}, panelViewport, sink));
    REQUIRE(layout.Build(world, molga::WindowId{7}, gameViewport, sink));
    const auto warmedBuilds = layout.GeometryBuildCount();

    // 이제 여섯 프레임을 번갈아 그린다. 값마다 이름이 고정돼 있으면 두 항목이
    // 계속 적중하므로 새 기하는 하나도 만들어지지 않는다.
    for (int frame = 0; frame < 3; ++frame) {
        REQUIRE(layout.Build(world, molga::WindowId{7}, panelViewport, sink));
        REQUIRE(layout.Build(world, molga::WindowId{7}, gameViewport, sink));
    }
    CHECK(layout.GeometryBuildCount() == warmedBuilds);
    // 서로 다른 두 뷰포트는 서로 다른 항목으로 남는다 — 하나로 합쳐지면
    // 뷰포트가 기하에 영향을 주지 않는다는 잘못된 계약이 된다.
    CHECK(layout.GeometryCacheEntryCountForWorld(world.Generation()) == 2);
}

TEST_CASE("interleaved worlds and devices keep per-slot cache occupancy") {
    molga::ui::UILayoutSystem layout;
    CountingDiagnosticSink sink;
    World worldA;
    World worldB;
    BuildAuthoredNode(worldA, CacheSceneJson(), nullptr);
    BuildAuthoredNode(worldB, CacheSceneJson(), nullptr);
    const auto viewport = RawSize(640, 384);
    const auto generationA = worldA.Generation();
    const auto generationB = worldB.Generation();
    REQUIRE(generationA != generationB);

    const auto device =
        molga::ui::UIRuntimeInvalidationClock::Current().deviceGeneration;
    molga::ui::UISnapshotWorldDeviceSlotKey slotA{generationA, device};
    molga::ui::UISnapshotWorldDeviceSlotKey slotB{generationB, device};

    for (int i = 0; i < 8; ++i) {
        REQUIRE(layout.Build(worldA, molga::WindowId{7}, viewport, sink));
        REQUIRE(layout.Build(worldB, molga::WindowId{8}, viewport, sink));
    }
    CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slotA) == 1);
    CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slotB) == 1);
    CHECK(layout.GeometryCacheEntryCountForWorld(generationA) == 1);
    CHECK(layout.GeometryCacheEntryCountForWorld(generationB) == 1);
    CHECK(layout.GeometryCacheEntryCountForWorld(generationA) <=
          molga::ui::kUILayoutGeometryEntriesPerWorld);

    const auto geometryBuilds = layout.GeometryBuildCount();
    {
        // 장치 세대가 바뀌면 옛 장치에 묶인 전체 스냅샷은 전부 사라져야 한다.
        // 반대로 기하는 장치와 무관하므로 그대로 재사용된다 — 그 두 방향이
        // 이 블록의 요점이다.
        molga::ui::ScopedUIRuntimeGenerationForTesting nextDevice(
            molga::ui::UIRuntimeGenerationKind::Device, device + 1);
        molga::ui::UISnapshotWorldDeviceSlotKey nextSlotA{generationA,
                                                          device + 1};
        REQUIRE(layout.Build(worldA, molga::WindowId{7}, viewport, sink));
        CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slotA) == 0);
        CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slotB) == 0);
        CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(nextSlotA) == 1);
        CHECK(layout.GeometryCacheEntryCountForWorld(generationA) == 1);
        CHECK(layout.GeometryCacheEntryCountForWorld(generationB) == 1);
        CHECK(layout.GeometryBuildCount() == geometryBuilds);
    }

    // 월드를 놓으면 그 월드의 LRU와 모든 전체 슬롯이 함께 사라지고, 다른
    // 월드의 항목은 하나도 건드리지 않는다.
    REQUIRE(layout.Build(worldB, molga::WindowId{8}, viewport, sink));
    CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slotB) == 1);
    layout.OnWorldReleased(generationA);
    CHECK(layout.GeometryCacheEntryCountForWorld(generationA) == 0);
    CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slotA) == 0);
    CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(
              {generationA, device + 1}) == 0);
    CHECK(layout.GeometryCacheEntryCountForWorld(generationB) == 1);
    CHECK(layout.FullSnapshotCacheEntryCountForWorldDevice(slotB) == 1);
}

// ── Step 2d-2h: 세터/계층/내용 변경이 집계 세대를 올린다 ─────────────────────
namespace {

enum class UIFieldMutation {
    // Step 2d
    AnchorMin, AnchorMax, Pivot, AnchoredPosition, SizeDelta,
    CanvasScaleMode, CanvasReferenceResolution, CanvasMatchWidthOrHeight,
    CanvasSortingOrder,
    // Step 2e
    HorizontalMinimum, HorizontalPreferred, HorizontalFlexible,
    VerticalMinimum, VerticalPreferred, VerticalFlexible, IgnoreLayout,
    FitterHorizontal, FitterVertical, GroupMode,
    PaddingLeft, PaddingTop, PaddingRight, PaddingBottom,
    SpacingX, SpacingY, ChildAlignment,
    ControlWidth, ControlHeight, ExpandWidth, ExpandHeight,
    CellWidth, CellHeight, StartCorner, FillAxis, GridConstraint,
    ConstraintCount,
    // Step 2f
    MaskEnabled, ScrollViewport, ScrollContent, ScrollHorizontal,
    ScrollVertical, ScrollMovement, ScrollElasticity, ScrollInertia,
    ScrollDeceleration, ScrollSensitivity, ScrollInitialNormalizedX,
    ScrollInitialNormalizedY, SelectableInteractable, NavigationMode,
    NavigationUp, NavigationDown, NavigationLeft, NavigationRight,
    // Step 2g
    ImageTextureGuid, ImageTint, ImageSortingOrder,
    LabelText, LabelFontFamilyGuid, LabelFontSize, LabelLineSpacing,
    LabelColor, LabelHorizontalAlignment, LabelVerticalAlignment,
    LabelSortingOrder,
    ButtonInteractable, ButtonNormalColor, ButtonHoverColor,
    ButtonPressedColor, ButtonDisabledColor, ButtonSortingOrder,
    InputInitialText, InputReadOnly, InputMultiline, InputMaxGraphemes,
    InputContentPolicy, InputSubmitPolicy, InputTextViewport,
    InputRenderedLabel, InputPlaceholderLabel, InputFontFamilyGuid,
    InputParagraphStyle,
    AccessibilityRole, AccessibilityName, AccessibilityDescription,
    AccessibilityHidden,
    // Step 2h
    ObjectActive, ChildAdded, ChildRemoved, ChildReparented, SiblingOrder,
    IntrinsicLayoutIdentity, FontFaceBytes, FontFamilyFallbackOrder,
    FontFamilyStyleMap, TextureContentSha,
};

// 활성 UICanvas 서브트리 하나와, 저작 컴포넌트 열셋이 전부 붙은 대상 하나.
// 컴포넌트를 미리 붙여 두는 이유는 세대 기준선을 읽은 뒤에는 어떤 부수 효과도
// 있으면 안 되기 때문이다 — 부착 자체가 세대를 올린다면 모든 행이 통과한다.
class MutationHarness {
public:
    MutationHarness() {
        root_ = MakeObject(1);
        root_->AddComponent<UICanvas>();
        root_->AddComponent<RectTransform>();

        target_ = MakeObject(2);
        REQUIRE(target_->SetParent(root_));
        target_->AddComponent<RectTransform>();
        target_->AddComponent<UICanvas>();
        target_->AddComponent<UILayoutElement>();
        target_->AddComponent<UILayoutGroup>();
        target_->AddComponent<UIContentSizeFitter>();
        target_->AddComponent<UIMask>();
        target_->AddComponent<UIScrollView>();
        target_->AddComponent<UISelectable>();
        target_->AddComponent<UIImage>();
        target_->AddComponent<UILabel>();
        target_->AddComponent<UIButton>();
        target_->AddComponent<UITextInput>();
        target_->AddComponent<UIAccessibility>();

        sibling_ = MakeObject(3);
        REQUIRE(sibling_->SetParent(root_));
        sibling_->AddComponent<RectTransform>();

        detached_ = MakeObject(4);
        detached_->AddComponent<RectTransform>();
    }

    World& GetWorld() noexcept { return world_; }
    GameObject& Root() noexcept { return *root_; }
    GameObject& Target() noexcept { return *target_; }
    GameObject& Sibling() noexcept { return *sibling_; }
    GameObject& Detached() noexcept { return *detached_; }

    template <typename T>
    T& Of() {
        T* component = target_->GetComponent<T>();
        REQUIRE(component != nullptr);
        return *component;
    }

private:
    GameObject* MakeObject(unsigned int id) {
        auto object = std::make_shared<GameObject>("Node");
        object->SetID(id);
        GameObject* raw = world_.Add(object);
        REQUIRE(raw != nullptr);
        return raw;
    }

    World world_;
    GameObject* root_ = nullptr;
    GameObject* target_ = nullptr;
    GameObject* sibling_ = nullptr;
    GameObject* detached_ = nullptr;
};

SceneObjectRef Ref(unsigned int targetId) {
    SceneObjectRef ref;
    ref.targetId = targetId;
    return ref;
}

// default 없는 exhaustive switch. 열거에 값을 하나 더하면 여기가 컴파일에서
// 막히므로, 새 저작 필드가 표에 들어오지 않은 채로 지나갈 수 없다.
void ApplyMutation(MutationHarness& harness, UIFieldMutation mutation) {
    switch (mutation) {
        case UIFieldMutation::AnchorMin:
            harness.Of<RectTransform>().SetAnchorMin({0.25f, 0.125f});
            return;
        case UIFieldMutation::AnchorMax:
            harness.Of<RectTransform>().SetAnchorMax({0.75f, 0.875f});
            return;
        case UIFieldMutation::Pivot:
            harness.Of<RectTransform>().SetPivot({0.125f, 0.375f});
            return;
        case UIFieldMutation::AnchoredPosition:
            harness.Of<RectTransform>().SetAnchoredPosition({12.5f, -7.25f});
            return;
        case UIFieldMutation::SizeDelta:
            harness.Of<RectTransform>().SetSizeDelta({33.5f, 17.25f});
            return;
        case UIFieldMutation::CanvasScaleMode:
            harness.Of<UICanvas>().SetScaleMode(
                UICanvasScaleMode::ConstantPixelSize);
            return;
        case UIFieldMutation::CanvasReferenceResolution:
            harness.Of<UICanvas>().SetReferenceResolution({1280.0f, 720.0f});
            return;
        case UIFieldMutation::CanvasMatchWidthOrHeight:
            harness.Of<UICanvas>().SetMatchWidthOrHeight(0.25f);
            return;
        case UIFieldMutation::CanvasSortingOrder:
            harness.Of<UICanvas>().SetSortingOrder(17);
            return;
        case UIFieldMutation::HorizontalMinimum:
            harness.Of<UILayoutElement>().SetHorizontal({11.0f, 0.0f, 0.0f});
            return;
        case UIFieldMutation::HorizontalPreferred:
            harness.Of<UILayoutElement>().SetHorizontal({0.0f, 13.0f, 0.0f});
            return;
        case UIFieldMutation::HorizontalFlexible:
            harness.Of<UILayoutElement>().SetHorizontal({0.0f, 0.0f, 3.0f});
            return;
        case UIFieldMutation::VerticalMinimum:
            harness.Of<UILayoutElement>().SetVertical({19.0f, 0.0f, 0.0f});
            return;
        case UIFieldMutation::VerticalPreferred:
            harness.Of<UILayoutElement>().SetVertical({0.0f, 23.0f, 0.0f});
            return;
        case UIFieldMutation::VerticalFlexible:
            harness.Of<UILayoutElement>().SetVertical({0.0f, 0.0f, 5.0f});
            return;
        case UIFieldMutation::IgnoreLayout:
            harness.Of<UILayoutElement>().SetIgnoreLayout(true);
            return;
        case UIFieldMutation::FitterHorizontal:
            harness.Of<UIContentSizeFitter>().SetHorizontalFit(
                UIFitMode::Preferred);
            return;
        case UIFieldMutation::FitterVertical:
            harness.Of<UIContentSizeFitter>().SetVerticalFit(UIFitMode::Min);
            return;
        case UIFieldMutation::GroupMode:
            harness.Of<UILayoutGroup>().SetMode(UILayoutMode::Grid);
            return;
        case UIFieldMutation::PaddingLeft:
            harness.Of<UILayoutGroup>().SetPadding(7.0f, 0.0f, 0.0f, 0.0f);
            return;
        case UIFieldMutation::PaddingTop:
            harness.Of<UILayoutGroup>().SetPadding(0.0f, 0.0f, 9.0f, 0.0f);
            return;
        case UIFieldMutation::PaddingRight:
            harness.Of<UILayoutGroup>().SetPadding(0.0f, 11.0f, 0.0f, 0.0f);
            return;
        case UIFieldMutation::PaddingBottom:
            harness.Of<UILayoutGroup>().SetPadding(0.0f, 0.0f, 0.0f, 13.0f);
            return;
        case UIFieldMutation::SpacingX:
            harness.Of<UILayoutGroup>().SetSpacingX(6.5f);
            return;
        case UIFieldMutation::SpacingY:
            harness.Of<UILayoutGroup>().SetSpacingY(8.25f);
            return;
        case UIFieldMutation::ChildAlignment:
            harness.Of<UILayoutGroup>().SetChildHorizontalAlignment(
                molga::text::TextHorizontalAlignment::Right);
            return;
        case UIFieldMutation::ControlWidth:
            harness.Of<UILayoutGroup>().SetControlChildWidth(true);
            return;
        case UIFieldMutation::ControlHeight:
            harness.Of<UILayoutGroup>().SetControlChildHeight(true);
            return;
        case UIFieldMutation::ExpandWidth:
            harness.Of<UILayoutGroup>().SetChildForceExpandWidth(true);
            return;
        case UIFieldMutation::ExpandHeight:
            harness.Of<UILayoutGroup>().SetChildForceExpandHeight(true);
            return;
        case UIFieldMutation::CellWidth:
            harness.Of<UILayoutGroup>().SetCellSize(37.0f, 100.0f);
            return;
        case UIFieldMutation::CellHeight:
            harness.Of<UILayoutGroup>().SetCellSize(100.0f, 41.0f);
            return;
        case UIFieldMutation::StartCorner:
            harness.Of<UILayoutGroup>().SetStartCorner(
                UIGridStartCorner::LowerRight);
            return;
        case UIFieldMutation::FillAxis:
            harness.Of<UILayoutGroup>().SetFillAxis(UIGridFillAxis::Vertical);
            return;
        case UIFieldMutation::GridConstraint:
            harness.Of<UILayoutGroup>().SetGridConstraint(
                UIGridConstraint::FixedRows);
            return;
        case UIFieldMutation::ConstraintCount:
            harness.Of<UILayoutGroup>().SetConstraintCount(4);
            return;
        case UIFieldMutation::MaskEnabled:
            harness.Of<UIMask>().SetClipsDescendants(false);
            return;
        case UIFieldMutation::ScrollViewport:
            harness.Of<UIScrollView>().SetViewport(Ref(3));
            return;
        case UIFieldMutation::ScrollContent:
            harness.Of<UIScrollView>().SetContent(Ref(4));
            return;
        case UIFieldMutation::ScrollHorizontal:
            harness.Of<UIScrollView>().SetHorizontal(false);
            return;
        case UIFieldMutation::ScrollVertical:
            harness.Of<UIScrollView>().SetVertical(false);
            return;
        case UIFieldMutation::ScrollMovement:
            harness.Of<UIScrollView>().SetMovement(UIScrollMovement::Elastic);
            return;
        case UIFieldMutation::ScrollElasticity:
            harness.Of<UIScrollView>().SetElasticity(0.375f);
            return;
        case UIFieldMutation::ScrollInertia:
            harness.Of<UIScrollView>().SetInertia(false);
            return;
        case UIFieldMutation::ScrollDeceleration:
            harness.Of<UIScrollView>().SetDecelerationRate(0.625f);
            return;
        case UIFieldMutation::ScrollSensitivity:
            harness.Of<UIScrollView>().SetScrollSensitivity(2.5f);
            return;
        case UIFieldMutation::ScrollInitialNormalizedX:
            harness.Of<UIScrollView>().SetInitialNormalizedX(0.25f);
            return;
        case UIFieldMutation::ScrollInitialNormalizedY:
            harness.Of<UIScrollView>().SetInitialNormalizedY(0.75f);
            return;
        case UIFieldMutation::SelectableInteractable:
            harness.Of<UISelectable>().SetInteractable(false);
            return;
        case UIFieldMutation::NavigationMode:
            harness.Of<UISelectable>().SetNavigationMode(
                UINavigationMode::Explicit);
            return;
        case UIFieldMutation::NavigationUp:
            harness.Of<UISelectable>().SetNavigateUp(Ref(3));
            return;
        case UIFieldMutation::NavigationDown:
            harness.Of<UISelectable>().SetNavigateDown(Ref(4));
            return;
        case UIFieldMutation::NavigationLeft:
            harness.Of<UISelectable>().SetNavigateLeft(Ref(1));
            return;
        case UIFieldMutation::NavigationRight:
            harness.Of<UISelectable>().SetNavigateRight(Ref(2));
            return;
        case UIFieldMutation::ImageTextureGuid:
            harness.Of<UIImage>().SetTextureGuid("guid-image");
            return;
        case UIFieldMutation::ImageTint:
            harness.Of<UIImage>().SetTint(Color{0.25f, 0.5f, 0.75f, 0.5f});
            return;
        case UIFieldMutation::ImageSortingOrder:
            harness.Of<UIImage>().SetSortingOrder(21);
            return;
        case UIFieldMutation::LabelText:
            harness.Of<UILabel>().SetText("mutated");
            return;
        case UIFieldMutation::LabelFontFamilyGuid:
            harness.Of<UILabel>().SetFontFamilyGuid("guid-family");
            return;
        case UIFieldMutation::LabelFontSize:
            harness.Of<UILabel>().SetFontSizePx(31.5f);
            return;
        case UIFieldMutation::LabelLineSpacing:
            harness.Of<UILabel>().SetLineSpacing(1.75f);
            return;
        case UIFieldMutation::LabelColor:
            harness.Of<UILabel>().SetColor(Color{0.125f, 0.25f, 0.5f, 0.75f});
            return;
        case UIFieldMutation::LabelHorizontalAlignment:
            harness.Of<UILabel>().SetHorizontalAlignment(
                UILabel::HorizontalAlignment::Left);
            return;
        case UIFieldMutation::LabelVerticalAlignment:
            harness.Of<UILabel>().SetVerticalAlignment(
                UILabel::VerticalAlignment::Bottom);
            return;
        case UIFieldMutation::LabelSortingOrder:
            harness.Of<UILabel>().SetSortingOrder(23);
            return;
        case UIFieldMutation::ButtonInteractable:
            harness.Of<UIButton>().SetInteractable(false);
            return;
        case UIFieldMutation::ButtonNormalColor:
            harness.Of<UIButton>().SetNormalColor(
                Color{0.5f, 0.25f, 0.125f, 1.0f});
            return;
        case UIFieldMutation::ButtonHoverColor:
            harness.Of<UIButton>().SetHoverColor(
                Color{0.125f, 0.5f, 0.25f, 1.0f});
            return;
        case UIFieldMutation::ButtonPressedColor:
            harness.Of<UIButton>().SetPressedColor(
                Color{0.25f, 0.125f, 0.5f, 1.0f});
            return;
        case UIFieldMutation::ButtonDisabledColor:
            harness.Of<UIButton>().SetDisabledColor(
                Color{0.75f, 0.75f, 0.125f, 1.0f});
            return;
        case UIFieldMutation::ButtonSortingOrder:
            harness.Of<UIButton>().SetSortingOrder(27);
            return;
        case UIFieldMutation::InputInitialText:
            harness.Of<UITextInput>().SetInitialText("typed");
            return;
        case UIFieldMutation::InputReadOnly:
            harness.Of<UITextInput>().SetReadOnly(
                !harness.Of<UITextInput>().ReadOnly());
            return;
        case UIFieldMutation::InputMultiline:
            harness.Of<UITextInput>().SetMultiline(
                !harness.Of<UITextInput>().Multiline());
            return;
        case UIFieldMutation::InputMaxGraphemes:
            harness.Of<UITextInput>().SetMaxGraphemes(64);
            return;
        // UITextInputContentPolicy와 UITextInputSubmitPolicy는 아직 정규 값이
        // 하나뿐이라(Any / OnEnter) "다른 값"을 넣을 방법이 없다. 그래서 이 두
        // 행만은 반대 방향을 못 박는다: 유일한 값을 다시 넣는 것은 변경이
        // 아니므로 세대가 움직이면 안 된다. 두 번째 정규 값이 생기면
        // MutationHasNoAlternativeCanonicalValue에서 빼고 보통 행으로 돌린다.
        case UIFieldMutation::InputContentPolicy:
            harness.Of<UITextInput>().SetContentPolicy(
                UITextInputContentPolicy::Any);
            return;
        case UIFieldMutation::InputSubmitPolicy:
            harness.Of<UITextInput>().SetSubmitPolicy(
                UITextInputSubmitPolicy::OnEnter);
            return;
        case UIFieldMutation::InputTextViewport:
            harness.Of<UITextInput>().SetTextViewport(Ref(3));
            return;
        case UIFieldMutation::InputRenderedLabel:
            harness.Of<UITextInput>().SetRenderedLabel(Ref(4));
            return;
        case UIFieldMutation::InputPlaceholderLabel:
            harness.Of<UITextInput>().SetPlaceholderLabel(Ref(1));
            return;
        case UIFieldMutation::InputFontFamilyGuid:
            harness.Of<UITextInput>().SetFontFamilyGuid("guid-input-family");
            return;
        case UIFieldMutation::InputParagraphStyle: {
            UIAuthoredParagraphStyle style =
                harness.Of<UITextInput>().ParagraphStyle();
            style.fontSizePx = 41.5f;
            harness.Of<UITextInput>().SetParagraphStyle(style);
            return;
        }
        case UIFieldMutation::AccessibilityRole:
            harness.Of<UIAccessibility>().SetRole(UIAccessibilityRole::Button);
            return;
        case UIFieldMutation::AccessibilityName:
            harness.Of<UIAccessibility>().SetName("accessible name");
            return;
        case UIFieldMutation::AccessibilityDescription:
            harness.Of<UIAccessibility>().SetDescription("accessible detail");
            return;
        case UIFieldMutation::AccessibilityHidden:
            harness.Of<UIAccessibility>().SetHidden(true);
            return;
        case UIFieldMutation::ObjectActive:
            harness.Target().SetActive(false);
            return;
        case UIFieldMutation::ChildAdded:
            harness.Root().AddChild(&harness.Detached());
            return;
        case UIFieldMutation::ChildRemoved:
            harness.Root().RemoveChild(&harness.Sibling());
            return;
        case UIFieldMutation::ChildReparented:
            REQUIRE(harness.Sibling().SetParent(&harness.Target()));
            return;
        case UIFieldMutation::SiblingOrder:
            REQUIRE(harness.Sibling().SetSiblingIndex(0));
            return;
        case UIFieldMutation::IntrinsicLayoutIdentity: {
            const auto identity = molga::ui::CaptureTarget(
                harness.GetWorld(), harness.Of<UILabel>());
            REQUIRE(static_cast<bool>(identity));
            CHECK(molga::ui::UIIntrinsicLayoutRegistry::Get().Publish(
                identity, "published-identity", RawSize(320, 128)));
            return;
        }
        // 이 네 행은 열거값이 빠짐없이 다뤄졌다는 것만 증언한다. 게시 경계
        // 자체를 부르는 것이지 프로덕션 훅을 부르는 것이 아니므로, 이 행들만
        // 있으면 AssetDatabase/FontRepository/TextLayoutService의 훅을 통째로
        // 지워도 스위트가 초록이다. 그 훅들의 진짜 증인은 아래
        // "Step 4j" 절의 네 케이스다 — 실제 카탈로그 재import, 실제 face
        // 발행, 실제 배치 확정, 실제 텍스처 내용 SHA 교체를 지난다.
        case UIFieldMutation::FontFaceBytes:
        case UIFieldMutation::FontFamilyFallbackOrder:
        case UIFieldMutation::FontFamilyStyleMap:
            CHECK(molga::ui::NotifyUISemanticMutation());
            return;
        case UIFieldMutation::TextureContentSha: {
            molga::ui::UITextureContentIdentity content;
            content.contentSha256 = std::string(64, 'c');
            content.contentStableId = 991;
            CHECK(molga::ui::UITextureContentRegistry::Get().Publish(
                "guid-mutation-texture", content));
            return;
        }
    }
    REQUIRE_MESSAGE(false, "unhandled UIFieldMutation");
}

// 정규 값이 하나뿐이라 "다른 값"이 존재하지 않는 저작 필드. 목록이 비어 있는
// 것이 정상이며, 여기 들어 있는 동안 그 행은 양방향으로 시험될 수 없다.
bool MutationHasNoAlternativeCanonicalValue(UIFieldMutation mutation) {
    return mutation == UIFieldMutation::InputContentPolicy ||
           mutation == UIFieldMutation::InputSubmitPolicy;
}

template <std::size_t N>
void CheckEachMutationAdvancesSemanticEpoch(
    const std::array<UIFieldMutation, N>& mutations) {
    for (const auto mutation : mutations) {
        CAPTURE(static_cast<int>(mutation));
        MutationHarness harness;
        const auto before = molga::ui::UIRuntimeInvalidationClock::Current()
                                .semanticDirtyGeneration;
        ApplyMutation(harness, mutation);
        const auto after = molga::ui::UIRuntimeInvalidationClock::Current()
                               .semanticDirtyGeneration;
        CHECK(after != 0);
        if (MutationHasNoAlternativeCanonicalValue(mutation)) {
            CHECK(after == before);
        } else {
            CHECK(after > before);
        }
    }
}

} // namespace

TEST_CASE("RectTransform and Canvas setters advance the semantic epoch") {
    const std::array rectCanvasMutations{
        UIFieldMutation::AnchorMin, UIFieldMutation::AnchorMax,
        UIFieldMutation::Pivot, UIFieldMutation::AnchoredPosition,
        UIFieldMutation::SizeDelta, UIFieldMutation::CanvasScaleMode,
        UIFieldMutation::CanvasReferenceResolution,
        UIFieldMutation::CanvasMatchWidthOrHeight,
        UIFieldMutation::CanvasSortingOrder};
    CheckEachMutationAdvancesSemanticEpoch(rectCanvasMutations);
}

TEST_CASE("layout, fitter and group setters advance the semantic epoch") {
    const std::array layoutMutations{
        UIFieldMutation::HorizontalMinimum,
        UIFieldMutation::HorizontalPreferred,
        UIFieldMutation::HorizontalFlexible,
        UIFieldMutation::VerticalMinimum,
        UIFieldMutation::VerticalPreferred,
        UIFieldMutation::VerticalFlexible,
        UIFieldMutation::IgnoreLayout,
        UIFieldMutation::FitterHorizontal,
        UIFieldMutation::FitterVertical,
        UIFieldMutation::GroupMode,
        UIFieldMutation::PaddingLeft, UIFieldMutation::PaddingTop,
        UIFieldMutation::PaddingRight, UIFieldMutation::PaddingBottom,
        UIFieldMutation::SpacingX, UIFieldMutation::SpacingY,
        UIFieldMutation::ChildAlignment,
        UIFieldMutation::ControlWidth, UIFieldMutation::ControlHeight,
        UIFieldMutation::ExpandWidth, UIFieldMutation::ExpandHeight,
        UIFieldMutation::CellWidth, UIFieldMutation::CellHeight,
        UIFieldMutation::StartCorner, UIFieldMutation::FillAxis,
        UIFieldMutation::GridConstraint,
        UIFieldMutation::ConstraintCount};
    CheckEachMutationAdvancesSemanticEpoch(layoutMutations);
}

TEST_CASE("mask, scroll and selectable setters advance the semantic epoch") {
    const std::array interactionMutations{
        UIFieldMutation::MaskEnabled,
        UIFieldMutation::ScrollViewport, UIFieldMutation::ScrollContent,
        UIFieldMutation::ScrollHorizontal, UIFieldMutation::ScrollVertical,
        UIFieldMutation::ScrollMovement, UIFieldMutation::ScrollElasticity,
        UIFieldMutation::ScrollInertia, UIFieldMutation::ScrollDeceleration,
        UIFieldMutation::ScrollSensitivity,
        UIFieldMutation::ScrollInitialNormalizedX,
        UIFieldMutation::ScrollInitialNormalizedY,
        UIFieldMutation::SelectableInteractable,
        UIFieldMutation::NavigationMode, UIFieldMutation::NavigationUp,
        UIFieldMutation::NavigationDown, UIFieldMutation::NavigationLeft,
        UIFieldMutation::NavigationRight};
    CheckEachMutationAdvancesSemanticEpoch(interactionMutations);
}

TEST_CASE("visual, text and semantic setters advance the semantic epoch") {
    const std::array payloadMutations{
        UIFieldMutation::ImageTextureGuid, UIFieldMutation::ImageTint,
        UIFieldMutation::ImageSortingOrder,
        UIFieldMutation::LabelText, UIFieldMutation::LabelFontFamilyGuid,
        UIFieldMutation::LabelFontSize, UIFieldMutation::LabelLineSpacing,
        UIFieldMutation::LabelColor,
        UIFieldMutation::LabelHorizontalAlignment,
        UIFieldMutation::LabelVerticalAlignment,
        UIFieldMutation::LabelSortingOrder,
        UIFieldMutation::ButtonInteractable,
        UIFieldMutation::ButtonNormalColor, UIFieldMutation::ButtonHoverColor,
        UIFieldMutation::ButtonPressedColor,
        UIFieldMutation::ButtonDisabledColor,
        UIFieldMutation::ButtonSortingOrder,
        UIFieldMutation::InputInitialText, UIFieldMutation::InputReadOnly,
        UIFieldMutation::InputMultiline, UIFieldMutation::InputMaxGraphemes,
        UIFieldMutation::InputContentPolicy,
        UIFieldMutation::InputSubmitPolicy,
        UIFieldMutation::InputTextViewport,
        UIFieldMutation::InputRenderedLabel,
        UIFieldMutation::InputPlaceholderLabel,
        UIFieldMutation::InputFontFamilyGuid,
        UIFieldMutation::InputParagraphStyle,
        UIFieldMutation::AccessibilityRole,
        UIFieldMutation::AccessibilityName,
        UIFieldMutation::AccessibilityDescription,
        UIFieldMutation::AccessibilityHidden};
    CheckEachMutationAdvancesSemanticEpoch(payloadMutations);
}

TEST_CASE("hierarchy, intrinsic, font and content changes advance the epoch") {
    const std::array externalMutations{
        UIFieldMutation::ObjectActive,
        UIFieldMutation::ChildAdded, UIFieldMutation::ChildRemoved,
        UIFieldMutation::ChildReparented,
        UIFieldMutation::SiblingOrder,
        UIFieldMutation::IntrinsicLayoutIdentity,
        UIFieldMutation::FontFaceBytes,
        UIFieldMutation::FontFamilyFallbackOrder,
        UIFieldMutation::FontFamilyStyleMap,
        UIFieldMutation::TextureContentSha};
    CheckEachMutationAdvancesSemanticEpoch(externalMutations);
}

// 반대편. 위 표들만 있으면 "무엇을 하든 세대를 올린다"는 구현이 전부 통과한다.
// 이미 정규 상태와 같은 값을 다시 넣는 것은 변경이 아니므로 컴포넌트 revision도
// 집계 세대도 움직이면 안 된다 — 움직이면 인스펙터가 값을 다시 확인만 해도
// 캐시가 통째로 무효화된다.
TEST_CASE("setting an identical canonical value changes nothing") {
    struct SameValueRow {
        const char* name;
        void (*apply)(MutationHarness&);
        std::uint64_t (*revision)(MutationHarness&);
    };

    const SameValueRow rows[] = {
        {"RectTransform",
         [](MutationHarness& h) {
             h.Of<RectTransform>().SetSizeDelta(
                 h.Of<RectTransform>().GetSizeDelta());
         },
         [](MutationHarness& h) {
             return h.Of<RectTransform>().AuthoredRevision();
         }},
        {"UICanvas",
         [](MutationHarness& h) {
             h.Of<UICanvas>().SetSortingOrder(h.Of<UICanvas>().GetSortingOrder());
         },
         [](MutationHarness& h) { return h.Of<UICanvas>().AuthoredRevision(); }},
        {"UILayoutElement",
         [](MutationHarness& h) {
             h.Of<UILayoutElement>().SetHorizontal(
                 h.Of<UILayoutElement>().Horizontal());
         },
         [](MutationHarness& h) {
             return h.Of<UILayoutElement>().AuthoredRevision();
         }},
        {"UILayoutGroup",
         [](MutationHarness& h) {
             h.Of<UILayoutGroup>().SetSpacingX(h.Of<UILayoutGroup>().SpacingX());
         },
         [](MutationHarness& h) {
             return h.Of<UILayoutGroup>().AuthoredRevision();
         }},
        {"UIContentSizeFitter",
         [](MutationHarness& h) {
             h.Of<UIContentSizeFitter>().SetHorizontalFit(
                 h.Of<UIContentSizeFitter>().HorizontalFit());
         },
         [](MutationHarness& h) {
             return h.Of<UIContentSizeFitter>().AuthoredRevision();
         }},
        {"UIMask",
         [](MutationHarness& h) {
             h.Of<UIMask>().SetClipsDescendants(
                 h.Of<UIMask>().ClipsDescendants());
         },
         [](MutationHarness& h) { return h.Of<UIMask>().AuthoredRevision(); }},
        {"UIScrollView",
         [](MutationHarness& h) {
             h.Of<UIScrollView>().SetElasticity(h.Of<UIScrollView>().Elasticity());
         },
         [](MutationHarness& h) {
             return h.Of<UIScrollView>().AuthoredRevision();
         }},
        {"UISelectable",
         [](MutationHarness& h) {
             h.Of<UISelectable>().SetInteractable(
                 h.Of<UISelectable>().Interactable());
         },
         [](MutationHarness& h) {
             return h.Of<UISelectable>().AuthoredRevision();
         }},
        {"UIImage",
         [](MutationHarness& h) {
             h.Of<UIImage>().SetTint(h.Of<UIImage>().GetTint());
         },
         [](MutationHarness& h) { return h.Of<UIImage>().AuthoredRevision(); }},
        {"UILabel",
         [](MutationHarness& h) {
             h.Of<UILabel>().SetText(h.Of<UILabel>().GetText());
         },
         [](MutationHarness& h) { return h.Of<UILabel>().AuthoredRevision(); }},
        {"UIButton",
         [](MutationHarness& h) {
             h.Of<UIButton>().SetNormalColor(h.Of<UIButton>().GetNormalColor());
         },
         [](MutationHarness& h) { return h.Of<UIButton>().AuthoredRevision(); }},
        {"UITextInput",
         [](MutationHarness& h) {
             h.Of<UITextInput>().SetInitialText(
                 h.Of<UITextInput>().InitialText());
         },
         [](MutationHarness& h) {
             return h.Of<UITextInput>().AuthoredRevision();
         }},
        {"UIAccessibility",
         [](MutationHarness& h) {
             h.Of<UIAccessibility>().SetName(h.Of<UIAccessibility>().Name());
         },
         [](MutationHarness& h) {
             return h.Of<UIAccessibility>().AuthoredRevision();
         }},
    };

    for (const auto& row : rows) {
        CAPTURE(row.name);
        MutationHarness harness;
        const auto revisionBefore = row.revision(harness);
        const auto epochBefore = molga::ui::UIRuntimeInvalidationClock::Current()
                                     .semanticDirtyGeneration;
        row.apply(harness);
        CHECK(row.revision(harness) == revisionBefore);
        CHECK(molga::ui::UIRuntimeInvalidationClock::Current()
                  .semanticDirtyGeneration == epochBefore);
    }
}

// 계층 훅은 활성 UI Canvas 서브트리를 바꿀 수 있을 때만 올린다. Canvas가 하나도
// 없는 오브젝트를 옮기는 것은 UI 스냅샷을 바꿀 수 없으므로 세대를 움직이면
// 안 된다 — 움직이면 UI가 없는 씬의 매 프레임 오브젝트 이동이 UI 캐시를
// 통째로 무효화한다.
TEST_CASE("hierarchy changes outside a Canvas subtree keep the epoch") {
    World world;
    auto makeObject = [&world](unsigned int id) {
        auto object = std::make_shared<GameObject>("Plain");
        object->SetID(id);
        GameObject* raw = world.Add(object);
        REQUIRE(raw != nullptr);
        return raw;
    };
    GameObject* parent = makeObject(1);
    GameObject* child = makeObject(2);
    GameObject* other = makeObject(3);
    REQUIRE(child->SetParent(parent));

    const auto before =
        molga::ui::UIRuntimeInvalidationClock::Current().semanticDirtyGeneration;
    child->SetActive(false);
    parent->AddChild(other);
    parent->RemoveChild(child);
    CHECK(molga::ui::UIRuntimeInvalidationClock::Current()
              .semanticDirtyGeneration == before);

    // 증인: 같은 동작도 Canvas 서브트리 안이면 올라간다. 이게 없으면 위 검사는
    // "훅이 아예 없다"에서도 통과한다.
    parent->AddComponent<UICanvas>();
    parent->AddComponent<RectTransform>();
    const auto withCanvas =
        molga::ui::UIRuntimeInvalidationClock::Current().semanticDirtyGeneration;
    parent->AddChild(child);
    CHECK(molga::ui::UIRuntimeInvalidationClock::Current()
              .semanticDirtyGeneration > withCanvas);
}

// 실패했거나 아무것도 바꾸지 않은 발행은 세대를 움직이지 않는다. 프로덕션의
// FontRepository::Invalidate는 캐시에 없는 guid에 대해 아무 것도 지우지 않으므로
// 발행이 아니다.
TEST_CASE("failed or no-op content publication preserves the epoch") {
    molga::AssetDatabase database;
    molga::text::FontRepository repository(database);
    const auto beforeFont =
        molga::ui::UIRuntimeInvalidationClock::Current().semanticDirtyGeneration;
    repository.Invalidate("never-loaded-guid");
    CHECK(molga::ui::UIRuntimeInvalidationClock::Current()
              .semanticDirtyGeneration == beforeFont);

    MutationHarness harness;
    const auto identity =
        molga::ui::CaptureTarget(harness.GetWorld(), harness.Of<UILabel>());
    REQUIRE(static_cast<bool>(identity));
    REQUIRE(molga::ui::UIIntrinsicLayoutRegistry::Get().Publish(
        identity, "same-content", RawSize(320, 128)));
    const auto beforeIntrinsic =
        molga::ui::UIRuntimeInvalidationClock::Current().semanticDirtyGeneration;
    CHECK_FALSE(molga::ui::UIIntrinsicLayoutRegistry::Get().Publish(
        identity, "same-content", RawSize(320, 128)));
    CHECK(molga::ui::UIRuntimeInvalidationClock::Current()
              .semanticDirtyGeneration == beforeIntrinsic);

    molga::ui::UITextureContentIdentity content;
    content.contentSha256 = std::string(64, 'd');
    content.contentStableId = 77;
    REQUIRE(molga::ui::UITextureContentRegistry::Get().Publish("no-op-guid",
                                                              content));
    const auto beforeTexture =
        molga::ui::UIRuntimeInvalidationClock::Current().semanticDirtyGeneration;
    CHECK_FALSE(molga::ui::UITextureContentRegistry::Get().Publish("no-op-guid",
                                                                  content));
    CHECK(molga::ui::UIRuntimeInvalidationClock::Current()
              .semanticDirtyGeneration == beforeTexture);
}

// 드라이버 우선순위는 Canvas > 부모 그룹 > 자기 fitter > 저작 rect 하나뿐이다.
// 우선순위를 뒤바꾼 구현은 두 조건이 동시에 참인 행에서만 죽으므로, 여덟 조합을
// 전부 못 박는다.
TEST_CASE("ResolveDriver returns the first true driver in priority order") {
    using molga::ui::ResolveDriver;
    using molga::ui::UILayoutDriver;
    CHECK(ResolveDriver(true, true, true) == UILayoutDriver::Canvas);
    CHECK(ResolveDriver(true, true, false) == UILayoutDriver::Canvas);
    CHECK(ResolveDriver(true, false, true) == UILayoutDriver::Canvas);
    CHECK(ResolveDriver(true, false, false) == UILayoutDriver::Canvas);
    CHECK(ResolveDriver(false, true, true) == UILayoutDriver::ParentGroup);
    CHECK(ResolveDriver(false, true, false) == UILayoutDriver::ParentGroup);
    CHECK(ResolveDriver(false, false, true) == UILayoutDriver::SelfFitter);
    CHECK(ResolveDriver(false, false, false) == UILayoutDriver::AuthoredRect);
}

// 잘못된 표면/뷰포트는 스냅샷을 내지 않는다. 0을 돌려주는 대신 빈 스냅샷을
// 게시하면 렌더와 hit-test가 "UI가 하나도 없는 프레임"과 구분하지 못한다.
TEST_CASE("Build rejects a zero surface window and an invalid viewport") {
    UILayoutCacheFixture f;
    auto& layout = f.System();
    auto& world = f.GetWorld();
    auto& sink = f.Diagnostics();
    CHECK_FALSE(layout.Build(world, molga::WindowId{0}, f.Viewport(), sink));
    CHECK_FALSE(layout.Build(world, molga::WindowId{7}, RawSize(0, 384), sink));
    CHECK_FALSE(layout.Build(world, molga::WindowId{7}, RawSize(640, -1), sink));
    CHECK(sink.Count(TextDiagnosticCode::LayoutInvalid) == 3);
    // 증인: 유효한 인자는 스냅샷을 낸다.
    CHECK(layout.Build(world, molga::WindowId{7}, f.Viewport(), sink));
}

// ── 캐시 키의 필드 민감도 ────────────────────────────────────────────────────
// 충돌 검사는 "해시가 같아도 원래 필드를 전부 다시 본다"는 주장이다. 그 주장이
// 성립하려면 operator==가 실제로 모든 필드를 보아야 하는데, 서로 다른 키만
// 넣어 본 캐시 시험은 그것을 증명하지 못한다. 필드마다 한 번씩 뒤집는다.
TEST_CASE("cache keys compare every field") {
    using namespace molga::ui;

    UILayoutFastPathStamp stampA;
    stampA.surfaceWindowId = 3;
    stampA.worldGeneration = 5;
    stampA.logicalViewport = RawSize(640, 384);
    stampA.viewportGeneration = 7;
    stampA.semanticDirtyGeneration = 11;
    stampA.scrollDisplacementGeneration = 13;
    stampA.textureBindingGeneration = 17;
    stampA.deviceGeneration = 19;
    CHECK(stampA == stampA);
    const std::vector<void (*)(UILayoutFastPathStamp&)> stampEdits{
        [](UILayoutFastPathStamp& s) { s.surfaceWindowId = 99; },
        [](UILayoutFastPathStamp& s) { s.worldGeneration = 99; },
        [](UILayoutFastPathStamp& s) {
            s.logicalViewport.width = Fixed26_6::FromRaw(99);
        },
        [](UILayoutFastPathStamp& s) {
            s.logicalViewport.height = Fixed26_6::FromRaw(99);
        },
        [](UILayoutFastPathStamp& s) { s.viewportGeneration = 99; },
        [](UILayoutFastPathStamp& s) { s.semanticDirtyGeneration = 99; },
        [](UILayoutFastPathStamp& s) { s.scrollDisplacementGeneration = 99; },
        [](UILayoutFastPathStamp& s) { s.textureBindingGeneration = 99; },
        [](UILayoutFastPathStamp& s) { s.deviceGeneration = 99; }};
    for (std::size_t i = 0; i < stampEdits.size(); ++i) {
        CAPTURE(i);
        UILayoutFastPathStamp mutated = stampA;
        stampEdits[i](mutated);
        CHECK(mutated != stampA);
    }

    UILayoutGeometryCacheKey geometryA;
    geometryA.worldGeneration = 5;
    geometryA.viewport = RawSize(640, 384);
    geometryA.viewportGeneration = 7;
    geometryA.canvasScaleRevisions = {1, 2};
    geometryA.hierarchyAndSiblingRevisions = {3, 4};
    geometryA.rectAndLayoutRevisions = {5, 6};
    geometryA.intrinsicGenerations = {7, 8};
    CHECK(geometryA == geometryA);
    const std::vector<void (*)(UILayoutGeometryCacheKey&)> geometryEdits{
        [](UILayoutGeometryCacheKey& k) { k.worldGeneration = 99; },
        [](UILayoutGeometryCacheKey& k) {
            k.viewport.width = Fixed26_6::FromRaw(99);
        },
        [](UILayoutGeometryCacheKey& k) {
            k.viewport.height = Fixed26_6::FromRaw(99);
        },
        [](UILayoutGeometryCacheKey& k) { k.viewportGeneration = 99; },
        [](UILayoutGeometryCacheKey& k) { k.canvasScaleRevisions[0] = 99; },
        [](UILayoutGeometryCacheKey& k) {
            k.hierarchyAndSiblingRevisions[1] = 99;
        },
        [](UILayoutGeometryCacheKey& k) { k.rectAndLayoutRevisions[0] = 99; },
        [](UILayoutGeometryCacheKey& k) { k.intrinsicGenerations[1] = 99; },
        // 길이만 달라도 다른 키다. 접두사가 같다고 같은 씬이 아니다.
        [](UILayoutGeometryCacheKey& k) { k.rectAndLayoutRevisions.pop_back(); },
        // 순서가 뒤바뀐 벡터는 다른 씬이다.
        [](UILayoutGeometryCacheKey& k) {
            std::swap(k.hierarchyAndSiblingRevisions[0],
                      k.hierarchyAndSiblingRevisions[1]);
        }};
    for (std::size_t i = 0; i < geometryEdits.size(); ++i) {
        CAPTURE(i);
        UILayoutGeometryCacheKey mutated = geometryA;
        geometryEdits[i](mutated);
        CHECK(mutated != geometryA);
    }

    UIVisualCacheIdentity visualA;
    visualA.sceneObjectId = 3;
    visualA.componentTypeName = "UILabel";
    visualA.componentSchemaVersion = 2;
    visualA.authoredRevision = 5;
    visualA.canonicalAuthoredPayload = "{\"text\":\"a\"}";
    visualA.immutableTextLayoutIdentity = "content-a";
    visualA.textureGuid = "guid-a";
    visualA.textureContentSha256 = std::string(64, 'a');
    visualA.textureContentStableId = 7;
    CHECK(visualA == visualA);
    const std::vector<void (*)(UIVisualCacheIdentity&)> visualEdits{
        [](UIVisualCacheIdentity& v) { v.sceneObjectId = 99; },
        [](UIVisualCacheIdentity& v) { v.componentTypeName = "UIImage"; },
        [](UIVisualCacheIdentity& v) { v.componentSchemaVersion = 99; },
        [](UIVisualCacheIdentity& v) { v.authoredRevision = 99; },
        [](UIVisualCacheIdentity& v) { v.canonicalAuthoredPayload = "{}"; },
        [](UIVisualCacheIdentity& v) {
            v.immutableTextLayoutIdentity = "content-b";
        },
        [](UIVisualCacheIdentity& v) { v.textureGuid = "guid-b"; },
        [](UIVisualCacheIdentity& v) {
            v.textureContentSha256 = std::string(64, 'b');
        },
        [](UIVisualCacheIdentity& v) { v.textureContentStableId = 99; }};
    for (std::size_t i = 0; i < visualEdits.size(); ++i) {
        CAPTURE(i);
        UIVisualCacheIdentity mutated = visualA;
        visualEdits[i](mutated);
        CHECK(mutated != visualA);
    }

    UIInteractionCacheIdentity interactionA;
    interactionA.sceneObjectId = 3;
    interactionA.componentTypeName = "UISelectable";
    interactionA.componentSchemaVersion = 1;
    interactionA.authoredRevision = 5;
    interactionA.active = true;
    interactionA.interactable = true;
    interactionA.focusable = true;
    interactionA.acceptsTextInput = true;
    interactionA.maskEnabled = true;
    interactionA.navigationMode = UINavigationMode::Explicit;
    interactionA.explicitNavigation = {Ref(11), Ref(12), Ref(13), Ref(14)};
    CHECK(interactionA == interactionA);
    const std::vector<void (*)(UIInteractionCacheIdentity&)> interactionEdits{
        [](UIInteractionCacheIdentity& v) { v.sceneObjectId = 99; },
        [](UIInteractionCacheIdentity& v) { v.componentTypeName = "UIButton"; },
        [](UIInteractionCacheIdentity& v) { v.componentSchemaVersion = 99; },
        [](UIInteractionCacheIdentity& v) { v.authoredRevision = 99; },
        [](UIInteractionCacheIdentity& v) { v.active = false; },
        [](UIInteractionCacheIdentity& v) { v.interactable = false; },
        [](UIInteractionCacheIdentity& v) { v.focusable = false; },
        [](UIInteractionCacheIdentity& v) { v.acceptsTextInput = false; },
        [](UIInteractionCacheIdentity& v) { v.maskEnabled = false; },
        [](UIInteractionCacheIdentity& v) {
            v.navigationMode = UINavigationMode::None;
        },
        // 네 방향을 하나씩. 한 슬롯만 비교하는 구현은 나머지 세 행에서 죽는다.
        [](UIInteractionCacheIdentity& v) { v.explicitNavigation[0] = Ref(99); },
        [](UIInteractionCacheIdentity& v) { v.explicitNavigation[1] = Ref(99); },
        [](UIInteractionCacheIdentity& v) { v.explicitNavigation[2] = Ref(99); },
        [](UIInteractionCacheIdentity& v) { v.explicitNavigation[3] = Ref(99); }};
    for (std::size_t i = 0; i < interactionEdits.size(); ++i) {
        CAPTURE(i);
        UIInteractionCacheIdentity mutated = interactionA;
        interactionEdits[i](mutated);
        CHECK(mutated != interactionA);
    }

    UISnapshotCacheKey completeA;
    completeA.surfaceWindowId = 3;
    completeA.geometry = geometryA;
    completeA.semanticDirtyGeneration = 11;
    completeA.scrollDisplacementGeneration = 13;
    completeA.textureBindingGeneration = 17;
    completeA.deviceGeneration = 19;
    completeA.visualContent = {visualA};
    completeA.interaction = {interactionA};
    CHECK(completeA == completeA);
    const std::vector<void (*)(UISnapshotCacheKey&)> completeEdits{
        [](UISnapshotCacheKey& k) { k.surfaceWindowId = 99; },
        [](UISnapshotCacheKey& k) { k.geometry.worldGeneration = 99; },
        [](UISnapshotCacheKey& k) { k.semanticDirtyGeneration = 99; },
        [](UISnapshotCacheKey& k) { k.scrollDisplacementGeneration = 99; },
        [](UISnapshotCacheKey& k) { k.textureBindingGeneration = 99; },
        [](UISnapshotCacheKey& k) { k.deviceGeneration = 99; },
        [](UISnapshotCacheKey& k) { k.visualContent[0].sceneObjectId = 99; },
        [](UISnapshotCacheKey& k) { k.visualContent.clear(); },
        [](UISnapshotCacheKey& k) { k.interaction[0].interactable = false; },
        [](UISnapshotCacheKey& k) { k.interaction.clear(); }};
    for (std::size_t i = 0; i < completeEdits.size(); ++i) {
        CAPTURE(i);
        UISnapshotCacheKey mutated = completeA;
        completeEdits[i](mutated);
        CHECK(mutated != completeA);
    }

    const UISnapshotWorldDeviceSlotKey slotA{5, 7};
    CHECK(slotA == UISnapshotWorldDeviceSlotKey{5, 7});
    CHECK(slotA != UISnapshotWorldDeviceSlotKey{6, 7});
    CHECK(slotA != UISnapshotWorldDeviceSlotKey{5, 8});
}

// 256 경계는 정확히 256에서와 257번째에서 각각 확인해야 한다. 2048개만 넣어
// 보면 "상한을 넘지 않는다"는 알 수 있어도 "정확히 하나만 버린다"와 "256개
// 까지는 하나도 버리지 않는다"는 알 수 없다.
TEST_CASE("the geometry LRU holds exactly 256 entries per world") {
    UILayoutCacheFixture f;
    std::vector<molga::ui::UILayoutGeometryCacheKey> keys;
    for (int i = 0; i < 256; ++i) {
        f.SetDistinctLayoutConstraintRaw(640 + i);
        REQUIRE(f.Build());
        const auto key = f.System().LastGeometryKey();
        REQUIRE(key.has_value());
        keys.push_back(*key);
    }
    CHECK(f.GeometryCacheEntryCountForWorld() == 256);
    // 정확히 256개일 때는 첫 항목까지 전부 살아 있다. 이 검사가 없으면
    // "언제나 없다"를 돌려주는 GeometryCacheContains로도 아래 CHECK_FALSE가
    // 통과한다.
    CHECK(f.System().GeometryCacheContains(keys.front()));
    CHECK(f.System().GeometryCacheContains(keys.back()));

    f.SetDistinctLayoutConstraintRaw(640 + 256);
    REQUIRE(f.Build());
    CHECK(f.GeometryCacheEntryCountForWorld() == 256);
    // 정확히 LRU 하나만 나간다: 가장 오래된 것은 사라지고 그다음은 남는다.
    CHECK_FALSE(f.System().GeometryCacheContains(keys.front()));
    CHECK(f.System().GeometryCacheContains(keys[1]));
    CHECK(f.System().GeometryCacheContains(keys.back()));
}

// 키 스크래치가 실제로 위쪽 용량을 늘리는 순간이 있어야 "600 프레임 동안
// 늘지 않는다"가 의미를 갖는다. 상수 0을 돌려주는 계수기는 여기서 죽는다.
TEST_CASE("the first cold key build grows the snapshot-key scratch") {
    UILayoutCacheFixture f;
    CHECK(f.SnapshotKeyAllocationCount() == 0);
    CHECK(f.SnapshotKeyBuildCount() == 0);
    REQUIRE(f.Build());
    CHECK(f.SnapshotKeyBuildCount() == 1);
    CHECK(f.SnapshotKeyAllocationCount() > 0);
}

namespace {

// 같은 오브젝트 id와 같은 저작 키 집합을 쓰되 크기 하나만 다른 두 씬. 두 씬을
// 각각 새로 Deserialize하면 컴포넌트 revision 열도 똑같아지므로, 세대 말고는
// 기하 캐시 키를 가를 것이 하나도 없다 — 그 조건이 이 회귀 시험의 요점이다.
nlohmann::json ReplacementScene(float childWidth) {
    return Node(1,
                {{"UICanvas", ConstantCanvasJson()},
                 {"RectTransform", StretchRectJson()}},
                {Node(2, {{"RectTransform",
                           OffsetRectJson(0, 0, childWidth, 5)}})});
}

} // namespace

// 에디터의 New/Open Scene은 World::Objects()를 통해 밖에서 내용을 갈아 끼운다.
// 그 교체가 세대를 발행하지 않으면 다음 프레임이 이전 씬의 기하를 그대로
// 재사용한다 — 사용자가 Open Scene을 고른 직후 화면에 남는 잘못된 기하다.
TEST_CASE("an external scene replacement invalidates the geometry cache") {
    molga::ui::UILayoutSystem layout;
    CountingDiagnosticSink sink;
    World world;
    BuildAuthoredNode(world, ReplacementScene(10.0f), nullptr);
    const auto viewport = RawSize(1280, 640);

    const auto first = layout.Build(world, molga::WindowId{7}, viewport, sink);
    REQUIRE(first);
    REQUIRE(first->nodes.size() == 2);
    CHECK(first->nodes[1].logicalRect.width.Raw() == 640);
    const auto firstKey = layout.LastGeometryKey();
    REQUIRE(firstKey.has_value());
    const auto generationBefore = world.Generation();

    world.Objects().clear();
    BuildAuthoredNode(world, ReplacementScene(20.0f), nullptr);
    world.RepublishGenerationAfterExternalReplacement();
    CHECK(world.Generation() != generationBefore);

    const auto second = layout.Build(world, molga::WindowId{7}, viewport, sink);
    REQUIRE(second);
    REQUIRE(second->nodes.size() == 2);
    CHECK(second->nodes[1].logicalRect.width.Raw() == 1280);

    // 이 시험이 무엇을 지키는지 못 박는다: 두 키는 worldGeneration 하나만
    // 다르다. 세대를 발행하지 않았다면 두 씬이 같은 항목을 공유했을 것이다.
    const auto secondKey = layout.LastGeometryKey();
    REQUIRE(secondKey.has_value());
    CHECK(firstKey->worldGeneration != secondKey->worldGeneration);
    CHECK(firstKey->viewport == secondKey->viewport);
    CHECK(firstKey->viewportGeneration == secondKey->viewportGeneration);
    CHECK(firstKey->canvasScaleRevisions == secondKey->canvasScaleRevisions);
    CHECK(firstKey->hierarchyAndSiblingRevisions ==
          secondKey->hierarchyAndSiblingRevisions);
    CHECK(firstKey->rectAndLayoutRevisions == secondKey->rectAndLayoutRevisions);
    CHECK(firstKey->intrinsicGenerations == secondKey->intrinsicGenerations);
    molga::ui::UILayoutGeometryCacheKey sameGeneration = *secondKey;
    sameGeneration.worldGeneration = firstKey->worldGeneration;
    CHECK(sameGeneration == *firstKey);
}

// Step 9a: 프로덕션 배치의 입구는 UISystem 하나다. 이 파사드가 자기
// UILayoutSystem을 소유하지 않고 매번 새로 만들면 변경 없는 프레임도 같은
// 스냅샷을 돌려주지 못한다.
TEST_CASE("the UISystem facade owns the one production layout system") {
    World world;
    BuildAuthoredNode(world, CacheSceneJson(), nullptr);
    CountingDiagnosticSink sink;
    const auto viewport = RawSize(640, 384);

    const auto first =
        UISystem::Get().BuildLayout(world, molga::WindowId{9}, viewport, sink);
    const auto second =
        UISystem::Get().BuildLayout(world, molga::WindowId{9}, viewport, sink);
    REQUIRE(first);
    CHECK(first.get() == second.get());
    CHECK(first->surfaceWindowId == 9);
    CHECK(first->worldGeneration == world.Generation());

    // 월드를 놓으면 그 월드의 캐시와 빠른 경로가 함께 사라진다.
    UISystem::Get().OnWorldReleased(world.Generation());
    const auto third =
        UISystem::Get().BuildLayout(world, molga::WindowId{9}, viewport, sink);
    REQUIRE(third);
    CHECK(third.get() != second.get());
    CHECK(sink.Total() == 0);
}

// 상호작용 자격은 기하가 아니다. 기하 항목을 재사용하면서도 이 플래그는
// 살아 있는 저작 상태에서 다시 읽어야 한다 — 캐시와 함께 굳으면 선택 불가로
// 바꾼 버튼이 캐시가 살아 있는 동안 계속 눌린다.
TEST_CASE("reused geometry still refreshes interaction eligibility") {
    UILayoutCacheFixture f;
    const auto before = f.Build();
    REQUIRE(before);
    REQUIRE(before->nodes.size() == 3);
    // 픽스처의 id 2는 상호작용 가능한 UISelectable을 갖고 있다.
    REQUIRE(before->nodes[1].rectTransform.objectId == 2);
    CHECK(before->nodes[1].interactionEligible);
    CHECK_FALSE(before->nodes[0].interactionEligible);
    const auto geometryBuilds = f.GeometryBuildCount();

    f.Selectable().SetInteractable(false);
    const auto after = f.Build();
    REQUIRE(after);
    REQUIRE(after->nodes.size() == 3);
    CHECK(f.GeometryBuildCount() == geometryBuilds);
    CHECK_FALSE(after->nodes[1].interactionEligible);

    f.Selectable().SetInteractable(true);
    const auto restored = f.Build();
    REQUIRE(restored);
    CHECK(f.GeometryBuildCount() == geometryBuilds);
    CHECK(restored->nodes[1].interactionEligible);
}

// 해시가 같아도 원래의 순서 있는 필드를 전부 다시 본다. 서로 다른 키만 넣어
// 보는 시험은 해시만 믿는 구현에서도 전부 통과하므로, 실제로 충돌하는 쌍을
// 만들어 넣는다.
//
// FNV-1a의 한 걸음은 (state ^ value) * prime이고 곱하는 상수가 홀수라 가역이다.
// 그래서 어느 값 하나를 바꿔 상태가 갈라져도, 그 다음 값을 x ^ hA ^ hB 로
// 고르면 상태가 다시 합쳐지고 이후 스트림 전체가 동일해진다.
TEST_CASE("the geometry cache rejects a colliding but different key") {
    using namespace molga::ui;
    UILayoutCacheFixture f;
    REQUIRE(f.Build());
    const auto cached = f.System().LastGeometryKey();
    REQUIRE(cached.has_value());
    REQUIRE(f.System().GeometryCacheContains(*cached));
    REQUIRE(cached->canvasScaleRevisions.size() == 1);
    REQUIRE(cached->hierarchyAndSiblingRevisions.size() >= 1);

    std::size_t state = kUILayoutCacheHashSeed;
    state = FoldUILayoutCacheHash(state, cached->worldGeneration);
    state = FoldUILayoutCacheHash(
        state, static_cast<std::uint64_t>(cached->viewport.width.Raw()));
    state = FoldUILayoutCacheHash(
        state, static_cast<std::uint64_t>(cached->viewport.height.Raw()));
    state = FoldUILayoutCacheHash(state, cached->viewportGeneration);
    state = FoldUILayoutCacheHash(state, cached->canvasScaleRevisions.size());

    const std::uint64_t canvasOriginal = cached->canvasScaleRevisions[0];
    const std::uint64_t canvasAltered = canvasOriginal ^ 0x5A5A5A5AULL;
    std::size_t afterOriginal = FoldUILayoutCacheHash(state, canvasOriginal);
    std::size_t afterAltered = FoldUILayoutCacheHash(state, canvasAltered);
    // 다음 스트림 값은 hierarchyAndSiblingRevisions의 길이다. 길이는 두 키가
    // 같으므로 두 상태에 같은 값이 접힌다.
    const std::uint64_t hierarchySize =
        cached->hierarchyAndSiblingRevisions.size();
    afterOriginal = FoldUILayoutCacheHash(afterOriginal, hierarchySize);
    afterAltered = FoldUILayoutCacheHash(afterAltered, hierarchySize);

    UILayoutGeometryCacheKey colliding = *cached;
    colliding.canvasScaleRevisions[0] = canvasAltered;
    colliding.hierarchyAndSiblingRevisions[0] =
        cached->hierarchyAndSiblingRevisions[0] ^
        static_cast<std::uint64_t>(afterOriginal) ^
        static_cast<std::uint64_t>(afterAltered);

    // 공허하지 않다는 증인 두 개: 두 키는 정말 다르고, 해시는 정말 같다.
    // 프로덕션 해시가 바뀌어 이 쌍이 더 이상 충돌하지 않게 되면 이 케이스는
    // 조용히 무의미해지는 대신 여기서 실패한다.
    REQUIRE(colliding != *cached);
    REQUIRE(HashUILayoutGeometryCacheKey(colliding) ==
            HashUILayoutGeometryCacheKey(*cached));
    CHECK_FALSE(f.System().GeometryCacheContains(colliding));
}

// ═══ Step 4j: 네 게시 경계를 프로덕션 입구에서 관찰한다 ═════════════════════
// Step 2h의 표는 열거값이 빠짐없이 다뤄졌다는 것만 증언한다. 그 표의 폰트/
// 텍스처 행은 게시 경계를 테스트가 직접 부르므로, 카탈로그·저장소·배치
// 서비스에 박힌 실제 훅을 지워도 표는 그대로 통과한다("맞은 것과 고장난 것이
// 같은 답을 내는 자리에서 잰 단언"). 아래 네 케이스는 그 자리를 옮긴다:
// 저작 파일을 실제로 고쳐 쓰고, 실제 카탈로그 재import·실제 face 발행·실제
// 배치 확정·실제 텍스처 내용 SHA 교체를 지나서, 그 결과로 의미 세대가
// 움직이는지만 본다. 훅을 하나라도 지우면 여기서 무너진다.
namespace {

namespace fs = std::filesystem;

std::uint64_t SemanticEpoch() {
    return molga::ui::UIRuntimeInvalidationClock::Current()
        .semanticDirtyGeneration;
}

void WriteBinaryFile(const fs::path& path,
                     const std::vector<unsigned char>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE_MESSAGE(output.good(), path.string());
}

void WriteJsonFile(const fs::path& path, const nlohmann::json& document) {
    const std::string text = document.dump(2);
    WriteBinaryFile(path, std::vector<unsigned char>(text.begin(), text.end()));
}

nlohmann::json ReadJsonFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE_MESSAGE(input.good(), path.string());
    nlohmann::json parsed;
    input >> parsed;
    return parsed;
}

// ── stb_image가 실제로 디코드하는 가장 작은 PNG ─────────────────────────────
// TextureImporter는 stbi_info가 성공해야만 import를 성공으로 표시하므로, 내용
// SHA 발행을 관찰하려면 진짜 이미지가 필요하다. 저장(stored) deflate 블록을
// 쓰기 때문에 압축기가 없어도 되고, 픽셀 한 바이트만 바꾸면 바이트가 실제로
// 다른 두 애셋이 된다 — 그 "실제로 다름"이 이 케이스의 전부다.
void AppendBigEndianU32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFU));
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
}

std::uint32_t Crc32Of(const std::vector<unsigned char>& bytes) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> built{};
        for (std::uint32_t index = 0; index < 256U; ++index) {
            std::uint32_t value = index;
            for (int step = 0; step < 8; ++step) {
                value = (value & 1U) ? (0xEDB88320U ^ (value >> 1))
                                     : (value >> 1);
            }
            built[index] = value;
        }
        return built;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const unsigned char byte : bytes) {
        crc = table[(crc ^ byte) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

void AppendPngChunk(std::vector<unsigned char>& png, const char* type,
                    const std::vector<unsigned char>& data) {
    std::vector<unsigned char> typed(type, type + 4);
    typed.insert(typed.end(), data.begin(), data.end());
    AppendBigEndianU32(png, static_cast<std::uint32_t>(data.size()));
    png.insert(png.end(), typed.begin(), typed.end());
    AppendBigEndianU32(png, Crc32Of(typed));
}

std::vector<unsigned char> OnePixelPng(unsigned char pixel) {
    std::vector<unsigned char> png{0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU,
                                   0x0AU};

    std::vector<unsigned char> ihdr;
    AppendBigEndianU32(ihdr, 1U);  // width
    AppendBigEndianU32(ihdr, 1U);  // height
    ihdr.push_back(8U);            // bit depth
    ihdr.push_back(0U);            // color type: grayscale
    ihdr.push_back(0U);            // compression method
    ihdr.push_back(0U);            // filter method
    ihdr.push_back(0U);            // interlace method
    AppendPngChunk(png, "IHDR", ihdr);

    // 한 줄 = 필터 바이트 + 픽셀 한 바이트.
    const std::array<unsigned char, 2> raw{0U, pixel};
    std::vector<unsigned char> idat{0x78U, 0x01U};  // zlib 헤더(0x7801 % 31 == 0)
    idat.push_back(0x01U);                          // BFINAL=1, BTYPE=stored
    idat.push_back(0x02U);
    idat.push_back(0x00U);  // LEN  = 2 (little endian)
    idat.push_back(0xFDU);
    idat.push_back(0xFFU);  // NLEN = ~LEN
    idat.insert(idat.end(), raw.begin(), raw.end());
    std::uint32_t low = 1U;
    std::uint32_t high = 0U;
    for (const unsigned char byte : raw) {
        low = (low + byte) % 65521U;
        high = (high + low) % 65521U;
    }
    AppendBigEndianU32(idat, (high << 16) | low);
    AppendPngChunk(png, "IDAT", idat);

    AppendPngChunk(png, "IEND", {});
    return png;
}

// 자격 트리 위에 실제 카탈로그·산출물 저장소·해석기·배치 서비스를 세운다.
// Step 4j의 훅은 전부 이 사슬의 끝에 있으므로, 여기서는 알림 함수를 한 번도
// 부르지 않고 프로덕션 입구만 지난다.
class TextAssetEpochFixture {
private:
    QualificationAssetTreeFixture tree_;

public:
    molga::AssetDatabase database;

private:
    std::shared_ptr<const molga::FontArtifactStore> store_;
    bool bound_;

public:
    molga::text::FontRepository repository;
    molga::text::FontFamilyResolver resolver;
    molga::text::TextShapingService shaper;
    molga::text::TextLayoutCache cache;
    molga::text::TextLayoutService service;
    molga::text::VectorTextDiagnosticSink sink;

    TextAssetEpochFixture()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))),
          bound_(BindAndScan()),
          repository(database),
          resolver(database, repository),
          cache(molga::text::TextLayoutCacheLimits::Production()),
          service(resolver, shaper, cache) {}

    const fs::path& AssetsRoot() const { return tree_.AssetsRoot(); }

    nlohmann::json FamilySource(const char* familyGuid) const {
        const fs::path path = database.AbsoluteSourcePath(familyGuid);
        REQUIRE_FALSE(path.empty());
        return ReadJsonFile(path);
    }

    // 저작 원본을 실제로 고쳐 쓰고 카탈로그의 재import 입구를 지난다.
    void RewriteFamilySource(const char* familyGuid,
                             const nlohmann::json& source) {
        const fs::path path = database.AbsoluteSourcePath(familyGuid);
        REQUIRE_FALSE(path.empty());
        WriteJsonFile(path, source);
        std::string error;
        REQUIRE_MESSAGE(database.TryReimport(familyGuid, &error), error);
    }

private:
    bool BindAndScan() {
        std::string bindError;
        REQUIRE_MESSAGE(database.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database.ScanProject(tree_.AssetsRoot());
        return true;
    }
};

constexpr const char* kEpochPrimaryFamily = "11111111111111111111111111111111";
constexpr const char* kEpochLatinFont = "44444444444444444444444444444444";

} // namespace

// FontRepository::Publish는 불변 face 자원이 실제로 발행되는 유일한 자리다.
// Load만 부르고 알림 함수는 부르지 않으므로, Publish의 훅을 지우면 첫 단언이
// 무너진다. Invalidate에도 성공 증인을 둔다 — 없으면 그 함수를 이른 반환으로
// 바꿔도 스위트가 초록이다.
TEST_CASE("Step 4j: a real font face publication advances the semantic epoch") {
    TextAssetEpochFixture f;

    const auto before = SemanticEpoch();
    const auto loaded = f.repository.Load(kEpochLatinFont, 0, f.sink);
    REQUIRE(loaded);
    REQUIRE(*loaded != nullptr);
    const auto afterCold = SemanticEpoch();
    CHECK(afterCold > before);

    // warm 조회는 아무 자원도 교체하지 않는다.
    const auto warm = f.repository.Load(kEpochLatinFont, 0, f.sink);
    REQUIRE(warm);
    CHECK(*warm == *loaded);
    CHECK(SemanticEpoch() == afterCold);

    // 실제로 지워진 항목이 있을 때에만 세대가 오른다.
    f.repository.Invalidate(kEpochLatinFont);
    const auto afterInvalidate = SemanticEpoch();
    CHECK(afterInvalidate > afterCold);

    // 이미 없는 GUID를 다시 무효화하는 것은 교체가 아니다.
    f.repository.Invalidate(kEpochLatinFont);
    CHECK(SemanticEpoch() == afterInvalidate);
}

// family의 fallback 순서와 style 표는 FontFamilyImporter가 발행하므로
// AssetDatabase::IndexOne의 훅이 유일한 경계다. 이 케이스가 없으면 그 훅이
// FontImporter에서만 채워지는 artifact 안에 갇혀 있어도(즉 .fontfamily 편집이
// UI에 전혀 닿지 않아도) 스위트가 초록이다.
TEST_CASE("Step 4j: a real family reimport advances the semantic epoch") {
    TextAssetEpochFixture f;
    const nlohmann::json original = f.FamilySource(kEpochPrimaryFamily);
    REQUIRE(original.contains("fallbackFamilyGuids"));
    REQUIRE(original["fallbackFamilyGuids"].size() == 2U);
    REQUIRE(original.contains("faces"));
    REQUIRE_FALSE(original["faces"].empty());

    // 같은 저작 내용을 다시 import하는 것은 교체가 아니다.
    const auto beforeNoOp = SemanticEpoch();
    f.RewriteFamilySource(kEpochPrimaryFamily, original);
    CHECK(SemanticEpoch() == beforeNoOp);

    // 저작된 fallback 순서가 곧 해석 순서다. 순서만 뒤집어도 UI가 실제로 어떤
    // face로 그려지는지가 달라진다.
    nlohmann::json swapped = original;
    std::swap(swapped["fallbackFamilyGuids"][0],
              swapped["fallbackFamilyGuids"][1]);
    const auto beforeOrder = SemanticEpoch();
    f.RewriteFamilySource(kEpochPrimaryFamily, swapped);
    CHECK(SemanticEpoch() > beforeOrder);

    // style 표(face의 weight/slant)도 같은 경계를 지난다.
    nlohmann::json restyled = swapped;
    restyled["faces"][0]["weight"] = 700;
    const auto beforeStyle = SemanticEpoch();
    f.RewriteFamilySource(kEpochPrimaryFamily, restyled);
    CHECK(SemanticEpoch() > beforeStyle);
}

// 확정된 불변 배치가 캐시에 새로 저장되는 자리. warm 조회는 요청 키에서 바로
// 돌아오므로 여기 오지 않는다 — 그래서 "성공한 교체 뒤에 정확히 한 번"이
// 양방향으로 관찰된다.
//
// 첫 배치는 세대를 재는 자리로 쓸 수 없다. 그 호출이 face 바이트를 처음
// 적재하면서 FontRepository::Publish의 훅도 함께 울리므로, 거기서 잰 증가는
// 배치 발행의 훅을 지워도 그대로 선다(실제로 그 변이가 살아남는 것을 확인했다).
// 그래서 같은 face를 쓰는 배치를 먼저 한 번 확정해 저장소를 데운 뒤, 글자만
// 다른 두 번째 배치에서 잰다 — 그 시점에 움직일 수 있는 훅은 하나뿐이다.
// ═══════════════════════════════════════════════════════════════════════════
// 은퇴한 월드 세대는 반드시 회수 알림을 낸다
// ═══════════════════════════════════════════════════════════════════════════
//
// OnWorldReleased 자체는 이미 검증돼 있다. 검증되지 않은 것은 "무엇이 그것을
// 부르는가"였다 — 아무것도 부르지 않았고, 그래서 Play/Stop, Open Scene, 그리고
// 패키징된 런타임의 씬 전환마다 기하 LRU 한 벌과 살아 있는 스냅샷 하나가
// 영원히 남았다.
namespace {
std::vector<std::uint64_t>* g_releasedGenerations = nullptr;
void RecordReleasedGeneration(std::uint64_t generation) {
    if (g_releasedGenerations) g_releasedGenerations->push_back(generation);
}
struct ScopedReleaseRecorder {
    std::vector<std::uint64_t> released;
    molga::ui::UIWorldReleaseHandler previous = nullptr;
    ScopedReleaseRecorder() {
        g_releasedGenerations = &released;
        previous = molga::ui::SetUIWorldReleaseHandler(&RecordReleasedGeneration);
    }
    ~ScopedReleaseRecorder() {
        molga::ui::SetUIWorldReleaseHandler(previous);
        g_releasedGenerations = nullptr;
    }
    bool Saw(std::uint64_t generation) const {
        return std::find(released.begin(), released.end(), generation) !=
               released.end();
    }
};
} // namespace

TEST_CASE("every world generation retirement notifies the UI runtime") {
    ScopedReleaseRecorder recorder;

    SUBCASE("destruction") {
        std::uint64_t generation = 0;
        {
            World world;
            generation = world.Generation();
            REQUIRE(generation != 0);
        }
        CHECK(recorder.Saw(generation));
    }

    SUBCASE("Clear") {
        World world;
        const auto generation = world.Generation();
        world.Clear();
        CHECK(recorder.Saw(generation));
        // 새 세대는 아직 은퇴하지 않았다.
        CHECK_FALSE(recorder.Saw(world.Generation()));
    }

    SUBCASE("external replacement republish") {
        World world;
        const auto generation = world.Generation();
        world.RepublishGenerationAfterExternalReplacement();
        CHECK(recorder.Saw(generation));
        CHECK(world.Generation() != generation);
    }

    SUBCASE("move assignment retires both sides") {
        World destination;
        World source;
        const auto destinationGeneration = destination.Generation();
        const auto sourceGeneration = source.Generation();
        destination = std::move(source);
        CHECK(recorder.Saw(destinationGeneration));
        CHECK(recorder.Saw(sourceGeneration));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 컴포넌트를 끄고 켜는 것도 저작 변경이다
// ═══════════════════════════════════════════════════════════════════════════
//
// 배치는 전부 IsEnabled()로 거른다. 기본 Component::SetEnabled는 revision도
// 의미 세대도 건드리지 않으므로, 이 재정의가 없으면 Canvas를 끈 뒤에도 빠른
// 경로가 예전 스냅샷을 영원히 돌려준다 — enabled는 canonicalAuthoredPayload에도
// 없어서 기하 키조차 두 상태를 구분하지 못한다.
TEST_CASE("disabling a UI component advances revision and the epoch") {
    World world;
    auto object = std::make_shared<GameObject>("Panel");
    auto* rect = object->AddComponent<RectTransform>();
    world.Add(object);
    REQUIRE(rect != nullptr);

    const auto beforeRevision = rect->AuthoredRevision();
    const auto beforeEpoch = SemanticEpoch();

    rect->SetEnabled(false);
    CHECK(rect->AuthoredRevision() > beforeRevision);
    CHECK(SemanticEpoch() > beforeEpoch);

    // 같은 값을 다시 쓰는 것은 변경이 아니다.
    const auto settledRevision = rect->AuthoredRevision();
    const auto settledEpoch = SemanticEpoch();
    rect->SetEnabled(false);
    CHECK(rect->AuthoredRevision() == settledRevision);
    CHECK(SemanticEpoch() == settledEpoch);

    // 되켜는 것도 변경이다.
    rect->SetEnabled(true);
    CHECK(rect->AuthoredRevision() > settledRevision);
    CHECK(SemanticEpoch() > settledEpoch);
}

TEST_CASE("Step 4j: a non-UI text layout leaves the UI epoch alone") {
    TextAssetEpochFixture f;

    const auto makeRequest = [](const char* utf8) {
        molga::text::TextLayoutRequest request;
        request.utf8 = utf8;
        request.style.fontFamilyGuid = kEpochPrimaryFamily;
        request.style.shape.fontSize = Fixed26_6::FromRaw(1024);
        request.style.shape.language = "en";
        request.style.analysis.locale = "en";
        return request;
    };

    // 폰트 저장소를 데운다. 이 호출의 세대 변화는 재지 않는다.
    const auto warming = f.service.Layout(makeRequest(u8"Av"), f.sink);
    REQUIRE(warming);
    REQUIRE(*warming != nullptr);

    // LayoutRun::Run은 UI 라벨과 월드 공간 TextRenderer2D가 함께 쓰는 경로다.
    // 배치 서비스는 이 문단이 어느 쪽 것인지 알 수 없으므로, 여기서 세대를
    // 올리면 매 프레임 바뀌는 월드 텍스트 하나가 UI 스냅샷을 매 프레임 무효화한다.
    // 세대를 올리는 것은 확정된 배치와 대상 정체성을 함께 쥔 쪽의 몫이다 —
    // UISystem::CollectRender의 고유 크기 발행(tests/test_ui.cpp의 "a rendered
    // label publishes its intrinsic size to layout")이 그 자리다.
    const molga::text::TextLayoutRequest measured = makeRequest(u8"Bx");
    const auto before = SemanticEpoch();
    const auto cold = f.service.Layout(measured, f.sink);
    REQUIRE(cold);
    REQUIRE(*cold != nullptr);
    // 차갑게 새 배치를 확정했는데도 UI 세대는 그대로다.
    CHECK(SemanticEpoch() == before);

    // warm 조회도 마찬가지다.
    const auto warm = f.service.Layout(measured, f.sink);
    REQUIRE(warm);
    CHECK(*warm == *cold);
    CHECK(SemanticEpoch() == before);
}

// Step 4d가 요구하는 텍스처 정체성은 "저작 GUID + 검증된 내용 SHA + 내용에서
// 유도된 안정 ID"다. 카탈로그가 GUID → 원본 바이트의 유일한 권한이므로 발행도
// 거기서 한다. 이 케이스가 없으면 UITextureContentRegistry에 프로덕션
// 발행자가 하나도 없고, 실제 세션의 모든 UIImage가 빈 SHA로 남는다.
TEST_CASE("Step 4j: a real texture import publishes validated content identity") {
    TextAssetEpochFixture f;
    constexpr const char* kTextureGuid = "9a9a9a9a9a9a9a9a9a9a9a9a9a9a9a9a";
    const fs::path source = f.AssetsRoot() / "textures" / "content.png";
    WriteBinaryFile(source, OnePixelPng(0x11U));
    WriteJsonFile(molga::AssetMeta::MetaPathFor(source),
                  nlohmann::json{{"guid", kTextureGuid},
                                 {"importer", "TextureImporter"},
                                 {"importerVersion", 1},
                                 {"settings", nlohmann::json::object()}});

    const auto beforeScan = SemanticEpoch();
    f.database.ScanProject(f.AssetsRoot());
    const molga::AssetRecord* record = f.database.Find(kTextureGuid);
    REQUIRE(record != nullptr);
    REQUIRE_FALSE(record->importFailed);
    const auto afterScan = SemanticEpoch();
    CHECK(afterScan > beforeScan);

    const auto published =
        molga::ui::UITextureContentRegistry::Get().Find(kTextureGuid);
    REQUIRE(published);
    CHECK(published->contentSha256 == molga::Sha256File(source));
    CHECK(published->contentSha256.size() == 64U);
    CHECK(published->contentStableId != 0U);

    // 같은 바이트를 다시 스캔하는 것은 교체가 아니다.
    f.database.ScanProject(f.AssetsRoot());
    CHECK(SemanticEpoch() == afterScan);

    // 픽셀이 실제로 달라지면 정체성과 세대가 함께 움직인다. 프로세스 지역
    // import 순번이 아니라 내용에서 나온 값이어야만 이 두 단언이 함께 선다.
    WriteBinaryFile(source, OnePixelPng(0xEEU));
    std::string reimportError;
    REQUIRE_MESSAGE(f.database.TryReimport(kTextureGuid, &reimportError),
                    reimportError);
    const auto republished =
        molga::ui::UITextureContentRegistry::Get().Find(kTextureGuid);
    REQUIRE(republished);
    CHECK(republished->contentSha256 != published->contentSha256);
    CHECK(republished->contentStableId != published->contentStableId);
    CHECK(SemanticEpoch() > afterScan);
}
