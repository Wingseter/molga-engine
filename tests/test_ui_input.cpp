// Task 12.1: 순서 있는 UI 이벤트와 스냅샷 N에 대해 얼어붙은 대상 계획.
//
// 이 파일의 스냅샷은 하나도 손으로 조립하지 않는다. 전부 진짜 World 위에서
// UILayoutSystem::Build가 게시한 값이다 — hit 기록을 직접 채워 넣으면 게시
// 경로를 통째로 이른 반환으로 바꿔도 여기가 초록이고, 그때 "N이 유일한
// 권위"라는 주장은 아무것도 지키지 않는다.

#include "doctest.h"

#include "Assets/FontArtifactStore.h"
#include "Common/Fixed26_6.h"
#include "Core/AssetDatabase.h"
#include "Core/World.h"
#include "ECS/Component.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Components/UIMask.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/Components/UISelectable.h"
#include "ECS/Components/UITextInput.h"
#include "ECS/GameObject.h"
#include "Platform/NativeKeyModifiers.h"
#include "Platform/TextInputOwner.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextShapingService.h"
#include "SmokeTestSupport.h"
#include "UI/UIInputEvent.h"
#include "UI/UIInputRouter.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIScrollSystem.h"
#include "UI/UITextInputVisualState.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using molga::Fixed26_6;
using molga::FixedPoint;
using molga::FixedSize;
using molga::platform::NativeKeyModifiers;
using molga::platform::TextInputOwnerKind;
using molga::platform::TextInputOwnerStamp;
using molga::text::TextDiagnosticCode;
using molga::ui::PlannedUIEvent;
using molga::ui::UIEventAction;
using molga::ui::UIEventActionBit;
using molga::ui::UIEventDispatchAccumulator;
using molga::ui::UIEventHandlerResult;
using molga::ui::UIEventMergeStatus;
using molga::ui::UIEventStage;
using molga::ui::UIFrozenTarget;
using molga::ui::UIHitTargetSnapshot;
using molga::ui::UIInputEvent;
using molga::ui::UIInputEventKind;
using molga::ui::UIInputRouter;
using molga::ui::UIPlanningState;
using molga::ui::UIRuntimeTargetIdentity;
using molga::ui::UISnapshot;
using molga::ui::UISnapshotPtr;

namespace {

// ── 표면과 기하 ─────────────────────────────────────────────────────────────
constexpr molga::WindowId kSurfaceWindow = 10;
constexpr molga::WindowId kForeignWindow = 20;
constexpr float kViewportPx = 640.0f;
// 이 파일이 직접 쓰는 단일 face 패밀리. 자격 트리의 primary.fontfamily를
// 재사용하지 않는다 — 그 패밀리는 히브리/데바나가리/태국 face와 arabic/cjk
// 폴백(cjk -> NotoSansKR, 복사량의 81%)을 끌고 오는데, 이 파일은 ASCII "typed"
// 말고는 아무것도 그리지 않는다.
constexpr const char* kInputFamily = "7e57a11c7e57a11c7e57a11c7e57a11c";
constexpr const char* kNotoSansFontGuid = "44444444444444444444444444444444";

// 오브젝트 id. 이름을 붙여 두면 어느 시험이 무엇을 만졌는지 한눈에 보인다.
constexpr unsigned int kCanvasId = 1;
constexpr unsigned int kLowButtonId = 2;
constexpr unsigned int kHighButtonId = 3;
constexpr unsigned int kOuterScrollId = 4;
constexpr unsigned int kOuterContentId = 5;
constexpr unsigned int kInnerScrollId = 6;
constexpr unsigned int kInnerContentId = 7;
constexpr unsigned int kComboId = 8;
constexpr unsigned int kComboLabelId = 9;
constexpr unsigned int kInputAId = 10;
constexpr unsigned int kInputALabelId = 11;
constexpr unsigned int kInputBId = 12;
constexpr unsigned int kInputBLabelId = 13;
constexpr unsigned int kClippedButtonId = 14;
constexpr unsigned int kReparentHostId = 15;
// 상호작용 불가 기록들. 라운드 3 전의 픽스처에는 이런 기록이 하나도 없어서,
// 상호작용 여부로 갈리는 모든 경로가 어떤 변이에도 초록이었다.
constexpr unsigned int kPresetButtonId = 16;    // 에디터 기본 Button 프리셋
constexpr unsigned int kPresetLabelId = 17;     //   그 위를 거의 덮는 자식 Label
constexpr unsigned int kUnderButtonId = 18;     // 켜진 버튼
constexpr unsigned int kDisabledOverlayId = 19; //   그 위의 꺼진 버튼 + 배경 이미지
constexpr unsigned int kScrollItemId = 20;      // 스크롤 내용 안의 이미지 항목
constexpr unsigned int kScrollItemLabelId = 21; //   그 위의 평범한 라벨
constexpr unsigned int kScrollOverlayId = 22;   // 스크롤 목록 위를 덮는 장식

FixedSize RawSize(std::int32_t width, std::int32_t height) {
    return FixedSize{Fixed26_6::FromRaw(width), Fixed26_6::FromRaw(height)};
}

FixedPoint PxPoint(float x, float y) {
    return FixedPoint{Fixed26_6::FromRaw(static_cast<std::int32_t>(x * 64.0f)),
                      Fixed26_6::FromRaw(static_cast<std::int32_t>(y * 64.0f))};
}

FixedPoint RawPoint(std::int32_t x, std::int32_t y) {
    return FixedPoint{Fixed26_6::FromRaw(x), Fixed26_6::FromRaw(y)};
}

// 진단을 코드별로 셀 수 있는 sink. 억제도 중복 제거도 하지 않는다 — 그런
// 필터가 있으면 "정확히 하나"라는 주장이 sink 쪽 억제 덕에 통과할 수 있다.
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
    std::size_t CountFrom(TextDiagnosticCode code,
                          const std::string& subsystem) const {
        std::size_t total = 0;
        for (const auto& record : records_) {
            if (record.code == code && record.subsystem == subsystem) ++total;
        }
        return total;
    }
    std::size_t Total() const noexcept { return records_.size(); }
    void Clear() { records_.clear(); }

private:
    std::vector<molga::text::TextDiagnostic> records_;
};

// ── 케이스마다 새로 만드는 최소 애셋 트리 ──────────────────────────────────
// 실물 폰트 바이트와 실물 카탈로그는 그대로다(입력창의 라벨 주장은 프로덕션
// 게시 경로를 지나야만 서고, 그래야 textInputTarget이 hit 기록에 나타난다).
// 줄인 것은 쓰지 않는 바이트뿐이다: NotoSans-Regular 한 face와 그 라이선스,
// 그리고 이 파일이 직접 쓰는 폴백 없는 단일 face 패밀리.
//
// 케이스마다 새로 만든다. 프로세스 수명 정적 런타임으로 올리면 안 된다 —
// text_doctest_main은 context.run() 뒤에 ShutdownAfterTests()를 부르고, 그때
// 살아 있는 TextRuntimeClientHandle이 하나라도 있으면 main이 5를 돌려준다.
// 캐시 이력도 케이스 사이로 새어 "-tc= 로는 통과, 파일 전체로는 실패"가 된다.
class SlimInputAssetTree {
public:
    SlimInputAssetTree()
        : temp_("ui-input-assets"),
          projectRoot_(temp_.Path()),
          assetsRoot_(projectRoot_ / "Assets") {
        const std::filesystem::path sourceRoot =
            MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT;
        for (const char* relative :
             {"fonts/NotoSans-Regular.ttf",
              "licenses/NotoFonts-ffebf8c1-OFL.txt"}) {
            CopyPair(sourceRoot, relative);
        }
        test_support::WriteText(
            assetsRoot_ / "families" / "input.fontfamily",
            std::string(R"({"schemaVersion":1,"faces":[{"fontGuid":")") +
                kNotoSansFontGuid +
                R"(","faceIndex":0,"weight":400,"stretchPercent":100,)"
                R"("slant":"Upright"}],"fallbackFamilyGuids":[]})");
        test_support::WriteText(
            assetsRoot_ / "families" / "input.fontfamily.meta",
            std::string(R"({"guid":")") + kInputFamily +
                R"(","importer":"FontFamilyImporter","importerVersion":1,)"
                R"("settings":{}})");
    }
    const std::filesystem::path& ProjectRoot() const { return projectRoot_; }
    const std::filesystem::path& AssetsRoot() const { return assetsRoot_; }

private:
    // 소스와 .meta는 언제나 한 쌍이다. 한쪽만 복사하면 ScanProject가 새 GUID를
    // 만들어 이 트리의 고정 GUID 계약이 조용히 무너진다.
    void CopyPair(const std::filesystem::path& sourceRoot,
                  const std::string& relative) {
        for (const std::string& name : {relative, relative + ".meta"}) {
            const auto source = sourceRoot / name;
            const auto destination = assetsRoot_ / name;
            REQUIRE_MESSAGE(std::filesystem::is_regular_file(source),
                            "missing fixture pair member: " << source.string());
            std::filesystem::create_directories(destination.parent_path());
            REQUIRE(std::filesystem::copy_file(
                source, destination, std::filesystem::copy_options::none));
        }
    }

    test_support::TempDirectory temp_;
    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
};

// ── 실물 폰트 위의 실물 배치 서비스 ─────────────────────────────────────────
// Build의 서명이 TextLayoutService를 요구한다. 폰트가 없는 서비스를 쓰면 입력창
// 라벨 주장이 배치 실패로 뭉개져 textInputTarget이 아예 서지 않고, 그러면 단계
// 대상 표가 자기가 시험한다고 믿는 것을 시험하지 못한다.
class InputTextRuntime {
private:
    SlimInputAssetTree tree_;

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

    InputTextRuntime()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))),
          bound_(BindAndScan()),
          repository(database),
          resolver(database, repository),
          cache(molga::text::TextLayoutCacheLimits::Production()),
          service(resolver, shaper, cache) {}

private:
    bool BindAndScan() {
        std::string bindError;
        REQUIRE_MESSAGE(database.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database.ScanProject(tree_.AssetsRoot());
        return true;
    }
};

GameObject* AddObject(World& world, unsigned int id, GameObject* parent) {
    auto object = std::make_shared<GameObject>("ui" + std::to_string(id));
    object->SetID(id);
    GameObject* raw = world.Add(object);
    REQUIRE(raw != nullptr);
    if (parent) REQUIRE(raw->SetParent(parent));
    return raw;
}

RectTransform* AddOffsetRect(GameObject& object, float x, float y, float width,
                             float height) {
    auto* rect = object.AddComponent<RectTransform>();
    REQUIRE(rect != nullptr);
    rect->SetAnchorMin({0.0f, 0.0f});
    rect->SetAnchorMax({0.0f, 0.0f});
    rect->SetPivot({0.0f, 0.0f});
    rect->SetAnchoredPosition({x, y});
    rect->SetSizeDelta({width, height});
    return rect;
}

// 부모를 채우되 가장자리를 insetX/insetY씩 비운다. 에디터 Button 프리셋의
// Label과 같은 모양이다(anchors 0..1, pivot 0.5, sizeDelta -16,-8).
RectTransform* AddStretchRect(GameObject& object, float insetX, float insetY) {
    auto* rect = object.AddComponent<RectTransform>();
    REQUIRE(rect != nullptr);
    rect->SetAnchorMin({0.0f, 0.0f});
    rect->SetAnchorMax({1.0f, 1.0f});
    rect->SetPivot({0.5f, 0.5f});
    rect->SetAnchoredPosition({0.0f, 0.0f});
    rect->SetSizeDelta({-2.0f * insetX, -2.0f * insetY});
    return rect;
}

UILabel* AddLabel(GameObject& object, const std::string& text) {
    auto* label = object.AddComponent<UILabel>();
    REQUIRE(label != nullptr);
    label->SetFontFamilyGuid(kInputFamily);
    label->SetText(text);
    label->SetFontSizePx(16.0f);
    label->SetWrapMode(molga::text::TextWrapMode::NoWrap);
    return label;
}

UITextInput* AddTextInput(GameObject& object, unsigned int renderedLabelId) {
    auto* input = object.AddComponent<UITextInput>();
    REQUIRE(input != nullptr);
    input->SetFontFamilyGuid(kInputFamily);
    input->SetInitialText("typed");
    SceneObjectRef ref;
    ref.targetId = renderedLabelId;
    input->SetRenderedLabel(ref);
    return input;
}

const UIHitTargetSnapshot* FindHit(const UISnapshot& snapshot,
                                   const UIRuntimeTargetIdentity& target) {
    for (const auto& hit : snapshot.hitTargets) {
        if (hit.target == target) return &hit;
    }
    return nullptr;
}

bool RectContainsExclusive(const molga::FixedRect& rect, FixedPoint point) {
    const std::int64_t x = point.x.Raw();
    const std::int64_t y = point.y.Raw();
    const std::int64_t left = rect.x.Raw();
    const std::int64_t top = rect.y.Raw();
    return x >= left &&
           x < left + static_cast<std::int64_t>(rect.width.Raw()) &&
           y >= top && y < top + static_cast<std::int64_t>(rect.height.Raw());
}

bool HitRecordContains(const UIHitTargetSnapshot& hit, FixedPoint point) {
    if (hit.logicalClip && !RectContainsExclusive(*hit.logicalClip, point)) {
        return false;
    }
    return RectContainsExclusive(hit.logicalRect, point);
}

// ── 얼어붙은 대상 픽스처 ────────────────────────────────────────────────────
//
// 기본 씬은 일부러 "잘못 갈 곳"이 있는 상태다:
//  * 겹치는 버튼 둘 — 역순 hit이 아니면 아래 버튼이 뽑힌다.
//  * 포커스 가능한 위젯 셋 — 라우팅이 잘못 갈 자리가 실제로 있다.
//  * 조상 스크롤 뷰 둘 — 안쪽/바깥쪽 순서가 뒤집히면 드러난다.
//  * 텍스트 입력창 둘 — 도장이 투사된 포커스로 미끄러지면 드러난다.
//  * 클립에 잘리는 버튼 하나 — 부모 클립을 무시하면 드러난다.
class UIFrozenInputFixture {
public:
    UIFrozenInputFixture() {
        GameObject* canvas = AddObject(world_, kCanvasId, nullptr);
        AddOffsetRect(*canvas, 0.0f, 0.0f, kViewportPx, kViewportPx);
        auto* canvasComponent = canvas->AddComponent<UICanvas>();
        REQUIRE(canvasComponent != nullptr);
        canvasComponent->SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
        canvasComponent->SetSortingOrder(0);

        // 겹치는 두 버튼. 나중 형제가 위에 그려지므로 (10,10)의 정답은 high다.
        GameObject* low = AddObject(world_, kLowButtonId, canvas);
        AddOffsetRect(*low, 0.0f, 0.0f, 100.0f, 100.0f);
        lowButton_ = low->AddComponent<UIButton>();
        REQUIRE(lowButton_ != nullptr);
        lowButton_->SetOnClick([this] { ++lowClicks_; });

        GameObject* high = AddObject(world_, kHighButtonId, canvas);
        AddOffsetRect(*high, 0.0f, 0.0f, 100.0f, 100.0f);
        highButton_ = high->AddComponent<UIButton>();
        REQUIRE(highButton_ != nullptr);
        highButton_->SetOnClick([this] { ++highClicks_; });

        // 조상 스크롤 둘. 안쪽이 6, 바깥쪽이 4다.
        GameObject* outerScroll = AddObject(world_, kOuterScrollId, canvas);
        AddOffsetRect(*outerScroll, 200.0f, 0.0f, 200.0f, 200.0f);
        AddMask(*outerScroll);
        AddScrollView(*outerScroll, kOuterScrollId, kOuterContentId);
        GameObject* outerContent =
            AddObject(world_, kOuterContentId, outerScroll);
        AddOffsetRect(*outerContent, 0.0f, 0.0f, 200.0f, 400.0f);

        GameObject* innerScroll =
            AddObject(world_, kInnerScrollId, outerContent);
        AddOffsetRect(*innerScroll, 0.0f, 0.0f, 100.0f, 100.0f);
        AddMask(*innerScroll);
        AddScrollView(*innerScroll, kInnerScrollId, kInnerContentId);
        GameObject* innerContent =
            AddObject(world_, kInnerContentId, innerScroll);
        AddOffsetRect(*innerContent, 0.0f, 0.0f, 100.0f, 200.0f);

        // 한 오브젝트가 동작/포커스/텍스트 대상을 모두 낸다. 셋이 서로 다른
        // 컴포넌트라는 것이 단계 대상 표의 전부다.
        GameObject* combo = AddObject(world_, kComboId, innerContent);
        AddOffsetRect(*combo, 0.0f, 0.0f, 50.0f, 50.0f);
        comboButton_ = combo->AddComponent<UIButton>();
        REQUIRE(comboButton_ != nullptr);
        comboButton_->SetOnClick([this] { ++comboClicks_; });
        comboSelectable_ = combo->AddComponent<UISelectable>();
        REQUIRE(comboSelectable_ != nullptr);
        comboInput_ = AddTextInput(*combo, kComboLabelId);

        GameObject* comboLabel = AddObject(world_, kComboLabelId, innerContent);
        AddOffsetRect(*comboLabel, 0.0f, 0.0f, 50.0f, 50.0f);
        AddLabel(*comboLabel, "typed");

        // 부모 클립이 실제로 자르는 버튼. 사각형은 (200,80,50,50)이고 안쪽
        // 마스크는 (200,0,100,100)이라 아래 절반이 잘린다.
        GameObject* clipped = AddObject(world_, kClippedButtonId, innerContent);
        AddOffsetRect(*clipped, 0.0f, 80.0f, 50.0f, 50.0f);
        clippedButton_ = clipped->AddComponent<UIButton>();
        REQUIRE(clippedButton_ != nullptr);

        // 텍스트 입력창 둘. 도장이 투사된 포커스로 미끄러지면 여기서 갈린다.
        GameObject* inputA = AddObject(world_, kInputAId, canvas);
        AddOffsetRect(*inputA, 400.0f, 300.0f, 100.0f, 40.0f);
        inputASelectable_ = inputA->AddComponent<UISelectable>();
        REQUIRE(inputASelectable_ != nullptr);
        inputAInput_ = AddTextInput(*inputA, kInputALabelId);
        GameObject* inputALabel = AddObject(world_, kInputALabelId, inputA);
        AddOffsetRect(*inputALabel, 0.0f, 0.0f, 100.0f, 40.0f);
        AddLabel(*inputALabel, "typed");

        GameObject* inputB = AddObject(world_, kInputBId, canvas);
        AddOffsetRect(*inputB, 400.0f, 400.0f, 100.0f, 40.0f);
        // 버튼을 함께 둔다. 발송 중 콜백이 세계를 바꾸는 케이스가 실제 콜백
        // 경로를 지나야 하기 때문이다.
        inputBButton_ = inputB->AddComponent<UIButton>();
        REQUIRE(inputBButton_ != nullptr);
        inputBSelectable_ = inputB->AddComponent<UISelectable>();
        REQUIRE(inputBSelectable_ != nullptr);
        inputBInput_ = AddTextInput(*inputB, kInputBLabelId);
        GameObject* inputBLabel = AddObject(world_, kInputBLabelId, inputB);
        AddOffsetRect(*inputBLabel, 0.0f, 0.0f, 100.0f, 40.0f);
        AddLabel(*inputBLabel, "typed");

        // 재부모 케이스의 새 부모. 기하를 바꾸되 식별자는 건드리지 않는다.
        GameObject* host = AddObject(world_, kReparentHostId, canvas);
        AddOffsetRect(*host, 500.0f, 0.0f, 100.0f, 100.0f);

        // 에디터 "Create UI > Button" 프리셋과 같은 모양: 220x64 버튼과 그
        // 위를 가장자리 8/4 px만 남기고 덮는 자식 Label. Label 기록은 상호작용
        // 불가이고, 자식이므로 버튼 기록보다 위에 있다.
        GameObject* preset = AddObject(world_, kPresetButtonId, canvas);
        AddOffsetRect(*preset, 0.0f, 300.0f, 220.0f, 64.0f);
        presetButton_ = preset->AddComponent<UIButton>();
        REQUIRE(presetButton_ != nullptr);
        presetButton_->SetOnClick([this] { ++presetClicks_; });
        GameObject* presetLabel = AddObject(world_, kPresetLabelId, preset);
        AddStretchRect(*presetLabel, 8.0f, 4.0f);
        AddLabel(*presetLabel, "Button");

        // 켜진 버튼 위에 꺼진 버튼. 꺼진 UIButton 혼자는 기록을 내지 않으므로
        // 배경 이미지를 함께 둔다 — 그러면 그 오브젝트는 상호작용 불가 기록을
        // 낸다(동작 컴포넌트가 이미지로 넘어간다).
        GameObject* under = AddObject(world_, kUnderButtonId, canvas);
        AddOffsetRect(*under, 0.0f, 400.0f, 100.0f, 100.0f);
        underButton_ = under->AddComponent<UIButton>();
        REQUIRE(underButton_ != nullptr);
        underButton_->SetOnClick([this] { ++underClicks_; });
        GameObject* overlay = AddObject(world_, kDisabledOverlayId, canvas);
        AddOffsetRect(*overlay, 0.0f, 400.0f, 100.0f, 100.0f);
        disabledButton_ = overlay->AddComponent<UIButton>();
        REQUIRE(disabledButton_ != nullptr);
        disabledButton_->SetInteractable(false);
        disabledButton_->SetOnClick([this] { ++disabledClicks_; });
        REQUIRE(overlay->AddComponent<UIImage>() != nullptr);

        // 스크롤 내용 안의 평범한 항목: 배경 이미지와 그 위의 라벨. 둘 다
        // 상호작용 불가지만 조상 스크롤 둘을 안쪽에서 바깥쪽으로 든다. 절대
        // 사각형은 (250,0,50,50)이고 안쪽 마스크 (200,0,100,100) 안이다.
        GameObject* item = AddObject(world_, kScrollItemId, innerContent);
        AddOffsetRect(*item, 50.0f, 0.0f, 50.0f, 50.0f);
        REQUIRE(item->AddComponent<UIImage>() != nullptr);
        GameObject* itemLabel = AddObject(world_, kScrollItemLabelId, item);
        AddStretchRect(*itemLabel, 0.0f, 0.0f);
        AddLabel(*itemLabel, "item");

        // 스크롤 목록 위를 덮는 장식. 캔버스의 나중 자식이므로 목록보다 위에
        // 그려지고, 어떤 스크롤 뷰의 자손도 아니다. 가림의 증인이다.
        GameObject* decoration = AddObject(world_, kScrollOverlayId, canvas);
        AddOffsetRect(*decoration, 280.0f, 20.0f, 20.0f, 20.0f);
        REQUIRE(decoration->AddComponent<UIImage>() != nullptr);

        Rebuild();
    }

    // ── 접근자 ──────────────────────────────────────────────────────────────
    World& World() { return world_; }
    UIInputRouter& Router() { return router_; }
    CountingDiagnosticSink& Diagnostics() { return sink_; }
    molga::WindowId SurfaceWindowId() const noexcept { return kSurfaceWindow; }

    UISnapshotPtr Snapshot() {
        REQUIRE(snapshot_ != nullptr);
        return snapshot_;
    }
    UISnapshotPtr BuildSnapshot() { return Rebuild(); }

    UISnapshotPtr Rebuild(molga::WindowId window = kSurfaceWindow) {
        snapshot_ = layout_.Build(
            world_, window,
            RawSize(static_cast<std::int32_t>(kViewportPx * 64.0f),
                    static_cast<std::int32_t>(kViewportPx * 64.0f)),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            runtime_.service, sink_);
        REQUIRE(snapshot_ != nullptr);
        return snapshot_;
    }

    UIPlanningState InitialPlanningState(
        molga::WindowId window = kSurfaceWindow) const {
        UIPlanningState state;
        state.surfaceWindowId = window;
        state.surfaceWorldGeneration = world_.Generation();
        state.nativeWindowFocused = true;
        return state;
    }

    UIRuntimeTargetIdentity IdentityOf(const Component& component) const {
        return molga::ui::CaptureTarget(world_, component);
    }
    UIRuntimeTargetIdentity LowButtonIdentity() const {
        return IdentityOf(*lowButton_);
    }
    UIRuntimeTargetIdentity HighButtonIdentity() const {
        return IdentityOf(*highButton_);
    }
    UIRuntimeTargetIdentity ComboButtonIdentity() const {
        return IdentityOf(*comboButton_);
    }
    UIRuntimeTargetIdentity ComboSelectableIdentity() const {
        return IdentityOf(*comboSelectable_);
    }
    UIRuntimeTargetIdentity ComboTextInputIdentity() const {
        return IdentityOf(*comboInput_);
    }
    UIRuntimeTargetIdentity InputAIdentity() const {
        return IdentityOf(*inputAInput_);
    }
    UIRuntimeTargetIdentity InputBIdentity() const {
        return IdentityOf(*inputBInput_);
    }
    UIRuntimeTargetIdentity InputBButtonIdentity() const {
        return IdentityOf(*inputBButton_);
    }
    UIRuntimeTargetIdentity InputBSelectableIdentity() const {
        return IdentityOf(*inputBSelectable_);
    }
    UIRuntimeTargetIdentity ClippedButtonIdentity() const {
        return IdentityOf(*clippedButton_);
    }
    UIRuntimeTargetIdentity ScrollIdentity(unsigned int objectId) const {
        GameObject* object = world_.FindById(objectId);
        REQUIRE(object != nullptr);
        auto* view = object->GetComponent<UIScrollView>();
        REQUIRE(view != nullptr);
        return IdentityOf(*view);
    }

    UIRuntimeTargetIdentity PresetButtonIdentity() const {
        return IdentityOf(*presetButton_);
    }
    UIRuntimeTargetIdentity UnderButtonIdentity() const {
        return IdentityOf(*underButton_);
    }
    int PresetButtonClicks() const noexcept { return presetClicks_; }
    int UnderButtonClicks() const noexcept { return underClicks_; }
    int DisabledButtonClicks() const noexcept { return disabledClicks_; }
    UIButton& DisabledButton() { return *disabledButton_; }

    UIButton& LowButton() { return *lowButton_; }
    UIButton& HighButton() { return *highButton_; }
    UIButton& ComboButton() { return *comboButton_; }
    UIButton& InputBButton() { return *inputBButton_; }

    int LowButtonClicks() const noexcept { return lowClicks_; }
    int HighButtonClicks() const noexcept { return highClicks_; }
    int ComboButtonClicks() const noexcept { return comboClicks_; }

    // ── 이벤트 조립 ─────────────────────────────────────────────────────────
    UIInputEvent PointerDown(std::uint64_t sequence, FixedPoint point,
                             molga::WindowId window = kSurfaceWindow) const {
        return Pointer(UIInputEventKind::PointerButton, sequence, point, true,
                       window);
    }
    UIInputEvent PointerUp(std::uint64_t sequence, FixedPoint point,
                           molga::WindowId window = kSurfaceWindow) const {
        return Pointer(UIInputEventKind::PointerButton, sequence, point, false,
                       window);
    }
    UIInputEvent PointerMove(std::uint64_t sequence, FixedPoint point,
                             molga::WindowId window = kSurfaceWindow) const {
        return Pointer(UIInputEventKind::PointerMotion, sequence, point, false,
                       window);
    }
    UIInputEvent PointerDeparture(std::uint64_t sequence) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::PointerMotion;
        event.logicalPointValid = false;
        return event;
    }
    UIInputEvent PointerDownAtOverlap(std::uint64_t sequence) const {
        return PointerDown(sequence, PxPoint(10.0f, 10.0f));
    }
    UIInputEvent KeyDown(std::uint64_t sequence, std::uint32_t key = 'a',
                         molga::WindowId window = kSurfaceWindow) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = window;
        event.kind = UIInputEventKind::Key;
        event.control = key;
        event.active = true;
        return event;
    }
    UIInputEvent Scroll(std::uint64_t sequence, std::int32_t deltaXRaw,
                        std::int32_t deltaYRaw) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::Scroll;
        event.logicalDelta = RawPoint(deltaXRaw, deltaYRaw);
        return event;
    }
    UIInputEvent WindowFocus(std::uint64_t sequence, bool active,
                             molga::WindowId window) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = window;
        event.kind = UIInputEventKind::WindowFocus;
        event.active = active;
        return event;
    }
    UIInputEvent TextCommit(std::uint64_t sequence, const std::string& utf8,
                            const TextInputOwnerStamp& stamp,
                            molga::WindowId window = kSurfaceWindow) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = window;
        event.kind = UIInputEventKind::TextCommit;
        molga::ui::UITextInputEventPayload payload;
        payload.utf8 = utf8;
        payload.ownerAtIngest = stamp;
        event.text = std::move(payload);
        return event;
    }
    UIInputEvent UnstampedCommit(std::uint64_t sequence,
                                 const std::string& utf8) const {
        return TextCommit(sequence, utf8, TextInputOwnerStamp{});
    }
    // 조합 중인 편집. start/length는 SDL이 주는 UTF-8 **문자** 수다.
    UIInputEvent TextEditing(std::uint64_t sequence, const std::string& utf8,
                             const TextInputOwnerStamp& stamp,
                             std::int32_t start, std::int32_t length) const {
        UIInputEvent event = TextCommit(sequence, utf8, stamp);
        event.kind = UIInputEventKind::TextEditing;
        event.text->editingStartUtf8Characters = start;
        event.text->editingLengthUtf8Characters = length;
        return event;
    }
    UIInputEvent GamepadButton(std::uint64_t sequence,
                               std::uint32_t button) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::GamepadButton;
        event.control = button;
        event.active = true;
        return event;
    }
    UIInputEvent GamepadAxis(std::uint64_t sequence, std::uint32_t axis,
                             std::int32_t valueRaw) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::GamepadAxis;
        event.control = axis;
        event.axisValue = Fixed26_6::FromRaw(valueRaw);
        return event;
    }

    static TextInputOwnerStamp RuntimeStamp(
        const UIRuntimeTargetIdentity& target, std::uint64_t generation) {
        TextInputOwnerStamp stamp;
        stamp.kind = TextInputOwnerKind::RuntimeUITextInput;
        stamp.runtimeTarget = target;
        stamp.generation = generation;
        return stamp;
    }
    static TextInputOwnerStamp EditorStamp(std::uint64_t generation) {
        TextInputOwnerStamp stamp;
        stamp.kind = TextInputOwnerKind::EditorImGui;
        stamp.generation = generation;
        return stamp;
    }

    // ── 계획 ────────────────────────────────────────────────────────────────
    std::vector<PlannedUIEvent> ProjectAll(
        const UISnapshot& n, const std::vector<UIInputEvent>& events,
        UIPlanningState& state) {
        std::vector<PlannedUIEvent> plans;
        plans.reserve(events.size());
        for (const auto& event : events) {
            plans.push_back(
                router_.PlanNext(n, state.surfaceWindowId, event, state));
        }
        return plans;
    }
    std::vector<PlannedUIEvent> ProjectAll(
        const UISnapshot& n, const std::vector<UIInputEvent>& events) {
        UIPlanningState state = InitialPlanningState();
        return ProjectAll(n, events, state);
    }

    // 실제 포인터 다운으로 포커스를 세운 상태. 상태를 손으로 채우지 않는
    // 이유는 투사 규칙 자체가 시험 대상이기 때문이다.
    UIPlanningState FocusedPlanningState(const UISnapshot& n,
                                         std::uint64_t sequence = 1) {
        UIPlanningState state = InitialPlanningState();
        const auto plan = router_.PlanNext(
            n, kSurfaceWindow, PointerDown(sequence, PxPoint(210.0f, 10.0f)),
            state);
        REQUIRE(plan.targetFromSnapshotN.has_value());
        REQUIRE(state.focused.has_value());
        return state;
    }

    // ── 세계 변형 ───────────────────────────────────────────────────────────
    // 계획과 발송 사이에 세계를 바꾼다. 대상을 **제거**하고(식별자 소실), 새
    // 기하를 게시한다. 이름은 계획의 Step 1a 조각을 따른다. SetActive(false)만
    // 하는 "숨김"은 식별자를 잃지 않으므로 여기서 말하는 낡음이 아니다 —
    // 스냅샷 N이 가시성과 상호작용 여부의 유일한 권위이기 때문이다(라운드 3
    // 기록 R7; 그 결과 UIButton의 살아 있는 interactable_ 검사와 감사 기록이
    // 갈리는 문제는 Task 12.2 Step 6이 라우터 소유 상태로 옮기며 닫는다).
    void HideHighAndPublishNewGeometry() {
        world_.RemoveByIds({kHighButtonId});
        // 새 기하다: 아래 버튼이 겹침 지점의 유일한 대상이 되고 크기도 달라진다.
        GameObject* low = world_.FindById(kLowButtonId);
        REQUIRE(low != nullptr);
        auto* rect = low->GetComponent<RectTransform>();
        REQUIRE(rect != nullptr);
        rect->SetSizeDelta({150.0f, 150.0f});
        Rebuild();
    }

    molga::ui::UILayoutSystem& Layout() { return layout_; }

private:
    UIInputEvent Pointer(UIInputEventKind kind, std::uint64_t sequence,
                         FixedPoint point, bool active,
                         molga::WindowId window) const {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = window;
        event.kind = kind;
        event.logicalPoint = point;
        event.logicalPointValid = true;
        event.active = active;
        return event;
    }

    void AddMask(GameObject& object) {
        auto* mask = object.AddComponent<UIMask>();
        REQUIRE(mask != nullptr);
        mask->SetClipsDescendants(true);
    }
    void AddScrollView(GameObject& object, unsigned int viewportId,
                       unsigned int contentId) {
        auto* view = object.AddComponent<UIScrollView>();
        REQUIRE(view != nullptr);
        view->SetViewport(SceneObjectRef{viewportId});
        view->SetContent(SceneObjectRef{contentId});
        view->SetHorizontal(false);
        view->SetVertical(true);
    }

    InputTextRuntime runtime_;
    ::World world_;
    molga::ui::UILayoutSystem layout_;
    CountingDiagnosticSink sink_;
    UIInputRouter router_;
    UISnapshotPtr snapshot_;

    UIButton* lowButton_ = nullptr;
    UIButton* highButton_ = nullptr;
    UIButton* comboButton_ = nullptr;
    UIButton* clippedButton_ = nullptr;
    UIButton* inputBButton_ = nullptr;
    UISelectable* comboSelectable_ = nullptr;
    UISelectable* inputASelectable_ = nullptr;
    UISelectable* inputBSelectable_ = nullptr;
    UITextInput* comboInput_ = nullptr;
    UITextInput* inputAInput_ = nullptr;
    UITextInput* inputBInput_ = nullptr;
    UIButton* presetButton_ = nullptr;
    UIButton* underButton_ = nullptr;
    UIButton* disabledButton_ = nullptr;
    int lowClicks_ = 0;
    int highClicks_ = 0;
    int comboClicks_ = 0;
    int presetClicks_ = 0;
    int underClicks_ = 0;
    int disabledClicks_ = 0;
};

}  // namespace

// ── Step 1a ─────────────────────────────────────────────────────────────────
TEST_CASE("planned target never changes after snapshot N") {
    UIFrozenInputFixture f;
    const auto snapshotN = f.BuildSnapshot();
    const auto high = f.HighButtonIdentity();
    const UIInputEvent down = f.PointerDownAtOverlap(10);
    auto planning = f.InitialPlanningState();
    const auto planned =
        f.Router().PlanNext(*snapshotN, f.SurfaceWindowId(), down, planning);
    // 클릭이 실제로 성립할 수 있는 짝을 함께 계획한다. 누름 하나만 발송하면
    // 아래의 "아래 버튼이 눌리지 않았다"가 클릭 경로를 지나지 않아 언제나
    // 참이 된다.
    const auto plannedUp =
        f.Router().PlanNext(*snapshotN, f.SurfaceWindowId(),
                            f.PointerUp(11, PxPoint(10.0f, 10.0f)), planning);
    REQUIRE(planned.targetFromSnapshotN);
    CHECK(*planned.targetFromSnapshotN == high);
    REQUIRE(plannedUp.targetFromSnapshotN);
    CHECK(*plannedUp.targetFromSnapshotN == high);

    f.HideHighAndPublishNewGeometry();
    UIEventDispatchAccumulator accumulator(planned, f.Diagnostics());
    CHECK(accumulator.Merge(f.Router().HandleEvent(
              f.World(), *snapshotN, planned, f.Diagnostics())) ==
          UIEventMergeStatus::Merged);
    const auto dispatch = std::move(accumulator).Finish();
    UIEventDispatchAccumulator upAccumulator(plannedUp, f.Diagnostics());
    CHECK(upAccumulator.Merge(f.Router().HandleEvent(
              f.World(), *snapshotN, plannedUp, f.Diagnostics())) ==
          UIEventMergeStatus::Merged);
    const auto dispatchUp = std::move(upAccumulator).Finish();

    CHECK(dispatch.runtimeTarget == high);
    CHECK_FALSE(dispatch.delivered);  // hidden/replaced target is stale
    CHECK(dispatchUp.runtimeTarget == high);
    CHECK_FALSE(dispatchUp.delivered);
    CHECK(f.LowButtonClicks() == 0);  // no retarget to lower button
    CHECK_FALSE(f.LowButton().IsPressed());
    CHECK(f.HighButtonClicks() == 0);
    // 낡은 계획은 조용히 사라지지 않는다. 둘 다 보고된다.
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                    "ui.input") == 2);
}

// 발송은 스냅샷 N의 기하로만 판정한다. 계획과 발송 사이에 사각형이 움직여도
// 클릭 판정은 N의 사각형을 본다 — 살아 있는 컴포넌트를 다시 읽는 구현은
// 저작자가 방금 옮겨 놓은 자리로 판정하게 되고, 그것이 "N+1로 재조준하지
// 않는다"가 막으려는 바로 그 일이다.
TEST_CASE("dispatch judges the click against snapshot N geometry only") {
    UIFrozenInputFixture f;
    const auto snapshotN = f.BuildSnapshot();
    const auto* record = FindHit(*snapshotN, f.HighButtonIdentity());
    REQUIRE(record != nullptr);
    // N에서는 밖, 옮긴 뒤의 사각형에서는 안인 한 점.
    const FixedPoint outsideN = PxPoint(130.0f, 130.0f);
    REQUIRE_FALSE(RectContainsExclusive(record->logicalRect, outsideN));

    auto planning = f.InitialPlanningState();
    const auto press = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(), f.PointerDownAtOverlap(120), planning);
    // 잡혀 있으므로 사각형 밖에서 놓아도 대상은 그대로다. 그 대상 위에서
    // 클릭이 성립하는지가 여기서 갈린다.
    const auto release =
        f.Router().PlanNext(*snapshotN, f.SurfaceWindowId(),
                            f.PointerUp(121, outsideN), planning);
    REQUIRE(press.targetFromSnapshotN);
    REQUIRE(release.targetFromSnapshotN);
    CHECK(*release.targetFromSnapshotN == f.HighButtonIdentity());

    // 계획 뒤에 사각형을 키운다. 이제 outsideN은 살아 있는 사각형 안이다.
    GameObject* high = f.World().FindById(kHighButtonId);
    REQUIRE(high != nullptr);
    auto* rect = high->GetComponent<RectTransform>();
    REQUIRE(rect != nullptr);
    rect->SetSizeDelta({300.0f, 300.0f});
    const auto snapshotNPlus1 = f.Rebuild();
    const auto* moved = FindHit(*snapshotNPlus1, f.HighButtonIdentity());
    REQUIRE(moved != nullptr);
    REQUIRE(RectContainsExclusive(moved->logicalRect, outsideN));

    (void)f.Router().HandleEvent(f.World(), *snapshotN, press, f.Diagnostics());
    const auto result =
        f.Router().HandleEvent(f.World(), *snapshotN, release, f.Diagnostics());
    CHECK(result.callbackDelivered);
    CHECK((result.actionMask & UIEventActionBit(UIEventAction::Release)) != 0);
    // N의 사각형 밖이므로 클릭이 아니다.
    CHECK((result.actionMask & UIEventActionBit(UIEventAction::Click)) == 0);
    CHECK(f.HighButtonClicks() == 0);
    CHECK_FALSE(f.HighButton().IsPressed());

    // 대조군: N의 사각형 **안**에서 놓으면 같은 경로가 클릭을 낸다.
    auto second = f.InitialPlanningState();
    const auto pressAgain = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(), f.PointerDownAtOverlap(122), second);
    const auto releaseInside =
        f.Router().PlanNext(*snapshotN, f.SurfaceWindowId(),
                            f.PointerUp(123, PxPoint(10.0f, 10.0f)), second);
    (void)f.Router().HandleEvent(f.World(), *snapshotN, pressAgain,
                                 f.Diagnostics());
    const auto clicked = f.Router().HandleEvent(f.World(), *snapshotN,
                                                releaseInside, f.Diagnostics());
    CHECK((clicked.actionMask & UIEventActionBit(UIEventAction::Click)) != 0);
    CHECK(f.HighButtonClicks() == 1);
}

// 위 케이스의 대조군. 세계를 건드리지 않으면 같은 계획이 실제로 배달된다 —
// 이것이 없으면 "발송을 통째로 지운" 구현도 위의 CHECK_FALSE를 통과한다.
TEST_CASE("an unmutated plan delivers to its frozen target") {
    UIFrozenInputFixture f;
    const auto snapshotN = f.BuildSnapshot();
    auto planning = f.InitialPlanningState();
    const auto planned = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(), f.PointerDownAtOverlap(10), planning);
    REQUIRE(planned.targetFromSnapshotN);

    UIEventDispatchAccumulator accumulator(planned, f.Diagnostics());
    CHECK(accumulator.Merge(f.Router().HandleEvent(
              f.World(), *snapshotN, planned, f.Diagnostics())) ==
          UIEventMergeStatus::Merged);
    const auto dispatch = std::move(accumulator).Finish();
    CHECK(dispatch.runtimeTarget == f.HighButtonIdentity());
    CHECK(dispatch.delivered);
    CHECK(f.HighButton().IsPressed());
    CHECK_FALSE(f.LowButton().IsPressed());
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                    "ui.input") == 0);
}

// ── Step 1b: 얼어붙은 대상의 수명/경계 표 ───────────────────────────────────
TEST_CASE("a reparented target with the same identity is delivered exactly once") {
    UIFrozenInputFixture f;
    const auto snapshotN = f.BuildSnapshot();
    auto planning = f.InitialPlanningState();
    const auto pressPlan = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(), f.PointerDownAtOverlap(10), planning);
    const auto releasePlan = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(),
        f.PointerUp(11, PxPoint(10.0f, 10.0f)), planning);
    REQUIRE(pressPlan.targetFromSnapshotN);
    REQUIRE(releasePlan.targetFromSnapshotN);

    // 재부모는 식별자를 무효화하지 않는다. 기하는 달라지지만 계획은 스냅샷 N의
    // 사각형으로만 판정하므로 클릭은 그대로 성립한다.
    GameObject* high = f.World().FindById(kHighButtonId);
    GameObject* host = f.World().FindById(kReparentHostId);
    REQUIRE(high != nullptr);
    REQUIRE(host != nullptr);
    REQUIRE(high->SetParent(host));

    (void)f.Router().HandleEvent(f.World(), *snapshotN, pressPlan,
                                 f.Diagnostics());
    const auto release = f.Router().HandleEvent(f.World(), *snapshotN,
                                                releasePlan, f.Diagnostics());
    CHECK(release.callbackDelivered);
    CHECK((release.actionMask & UIEventActionBit(UIEventAction::Click)) != 0);
    CHECK(f.HighButtonClicks() == 1);
    CHECK(f.LowButtonClicks() == 0);
}

TEST_CASE("a removed and re-added component of the same type is skipped") {
    UIFrozenInputFixture f;
    const auto snapshotN = f.BuildSnapshot();
    auto planning = f.InitialPlanningState();
    const auto pressPlan = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(), f.PointerDownAtOverlap(10), planning);
    // 놓기까지 계획한다. 누름만 발송하면 아래의 "교체본이 클릭되지 않았다"는
    // 누름이 결코 클릭하지 않으므로 언제나 참이다.
    const auto releasePlan =
        f.Router().PlanNext(*snapshotN, f.SurfaceWindowId(),
                            f.PointerUp(11, PxPoint(10.0f, 10.0f)), planning);
    REQUIRE(pressPlan.targetFromSnapshotN);
    REQUIRE(releasePlan.targetFromSnapshotN);
    const auto frozen = *pressPlan.targetFromSnapshotN;

    GameObject* high = f.World().FindById(kHighButtonId);
    REQUIRE(high != nullptr);
    high->RemoveComponent<UIButton>();
    auto* replacement = high->AddComponent<UIButton>();
    REQUIRE(replacement != nullptr);
    int replacementClicks = 0;
    replacement->SetOnClick([&replacementClicks] { ++replacementClicks; });
    // 같은 오브젝트, 같은 타입, 다른 컴포넌트다.
    REQUIRE(f.IdentityOf(*replacement) != frozen);

    const auto press = f.Router().HandleEvent(f.World(), *snapshotN, pressPlan,
                                              f.Diagnostics());
    const auto release = f.Router().HandleEvent(f.World(), *snapshotN,
                                                releasePlan, f.Diagnostics());
    CHECK_FALSE(press.callbackDelivered);
    CHECK_FALSE(release.callbackDelivered);
    CHECK(press.resolvedStageTarget.has_value());
    CHECK(press.resolvedStageTarget->runtimeTarget == frozen);
    CHECK_FALSE(replacement->IsPressed());
    CHECK(replacementClicks == 0);
}

TEST_CASE("a replacement world that reuses the object id is skipped") {
    UIFrozenInputFixture f;
    const auto snapshotN = f.BuildSnapshot();
    auto planning = f.InitialPlanningState();
    const auto pressPlan = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(), f.PointerDownAtOverlap(10), planning);
    REQUIRE(pressPlan.targetFromSnapshotN);

    // 같은 오브젝트 id를 다시 발급하는 두 번째 월드. 세대가 다르므로 이전
    // 세대의 식별자는 다시 해석되지 않는다.
    World replacement;
    GameObject* reused = AddObject(replacement, kHighButtonId, nullptr);
    AddOffsetRect(*reused, 0.0f, 0.0f, 100.0f, 100.0f);
    auto* button = reused->AddComponent<UIButton>();
    REQUIRE(button != nullptr);
    int reusedClicks = 0;
    button->SetOnClick([&reusedClicks] { ++reusedClicks; });
    REQUIRE(replacement.Generation() != f.World().Generation());

    const auto press = f.Router().HandleEvent(replacement, *snapshotN, pressPlan,
                                              f.Diagnostics());
    CHECK_FALSE(press.callbackDelivered);
    CHECK(reusedClicks == 0);
    CHECK_FALSE(button->IsPressed());
}

TEST_CASE("a scene transition inside one world is skipped") {
    UIFrozenInputFixture f;
    const auto snapshotN = f.BuildSnapshot();
    auto planning = f.InitialPlanningState();
    const auto pressPlan = f.Router().PlanNext(
        *snapshotN, f.SurfaceWindowId(), f.PointerDownAtOverlap(10), planning);
    REQUIRE(pressPlan.targetFromSnapshotN);
    const auto generationBefore = f.World().Generation();

    f.World().Objects().clear();
    GameObject* reused = AddObject(f.World(), kHighButtonId, nullptr);
    AddOffsetRect(*reused, 0.0f, 0.0f, 100.0f, 100.0f);
    auto* button = reused->AddComponent<UIButton>();
    REQUIRE(button != nullptr);
    int reusedClicks = 0;
    button->SetOnClick([&reusedClicks] { ++reusedClicks; });
    f.World().RepublishGenerationAfterExternalReplacement();
    REQUIRE(f.World().Generation() != generationBefore);

    const auto press = f.Router().HandleEvent(f.World(), *snapshotN, pressPlan,
                                              f.Diagnostics());
    CHECK_FALSE(press.callbackDelivered);
    CHECK(reusedClicks == 0);
}

TEST_CASE("hit selection walks snapshot N in exact reverse draw order") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const FixedPoint point = PxPoint(10.0f, 10.0f);

    // 겹침이 진짜인지 먼저 못 박는다. 한 기록만 그 점을 담는다면 "역순"은
    // 아무것도 시험하지 않는다.
    std::vector<const UIHitTargetSnapshot*> containing;
    for (const auto& hit : n->hitTargets) {
        if (HitRecordContains(hit, point)) containing.push_back(&hit);
    }
    REQUIRE(containing.size() >= 2);
    const auto* topmost = *std::max_element(
        containing.begin(), containing.end(),
        [](const UIHitTargetSnapshot* a, const UIHitTargetSnapshot* b) {
            return a->order < b->order;
        });

    auto planning = f.InitialPlanningState();
    const auto plan = f.Router().PlanNext(*n, f.SurfaceWindowId(),
                                          f.PointerDown(20, point), planning);
    REQUIRE(plan.targetFromSnapshotN);
    CHECK(*plan.targetFromSnapshotN == topmost->target);
    CHECK(*plan.targetFromSnapshotN == f.HighButtonIdentity());
    // 정방향 순회는 정확히 이 값을 낸다.
    CHECK(*plan.targetFromSnapshotN != f.LowButtonIdentity());
}

TEST_CASE("a point outside the parent clip does not hit the clipped record") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const auto clipped = f.ClippedButtonIdentity();
    const auto* record = FindHit(*n, clipped);
    REQUIRE(record != nullptr);
    REQUIRE(record->logicalClip.has_value());

    // 사각형 안이면서 클립 안. 여기서는 잡힌다.
    const FixedPoint inside = PxPoint(210.0f, 90.0f);
    REQUIRE(RectContainsExclusive(record->logicalRect, inside));
    REQUIRE(RectContainsExclusive(*record->logicalClip, inside));
    // 사각형 안이지만 클립 밖. 여기서는 잡히면 안 된다 — 그리고 그 점이
    // 정말로 사각형 안이어야 이 케이스가 클립을 시험한다.
    const FixedPoint outside = PxPoint(210.0f, 110.0f);
    REQUIRE(RectContainsExclusive(record->logicalRect, outside));
    REQUIRE_FALSE(RectContainsExclusive(*record->logicalClip, outside));

    auto stateInside = f.InitialPlanningState();
    const auto hit = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(30, inside), stateInside);
    REQUIRE(hit.targetFromSnapshotN);
    CHECK(*hit.targetFromSnapshotN == clipped);

    auto stateOutside = f.InitialPlanningState();
    const auto miss = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(31, outside), stateOutside);
    CHECK((!miss.targetFromSnapshotN || *miss.targetFromSnapshotN != clipped));
    CHECK_FALSE(stateOutside.pointerCapture == clipped);
}

TEST_CASE("hit rectangles exclude their far boundary") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const auto* record = FindHit(*n, f.HighButtonIdentity());
    REQUIRE(record != nullptr);
    const std::int32_t rightRaw =
        record->logicalRect.x.Raw() + record->logicalRect.width.Raw();
    const std::int32_t insideRaw = rightRaw - 1;
    const std::int32_t y = record->logicalRect.y.Raw() + 10;

    auto stateInside = f.InitialPlanningState();
    const auto lastInside = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(40, RawPoint(insideRaw, y)),
        stateInside);
    REQUIRE(lastInside.targetFromSnapshotN);
    CHECK(*lastInside.targetFromSnapshotN == f.HighButtonIdentity());

    auto stateBoundary = f.InitialPlanningState();
    const auto onBoundary = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(41, RawPoint(rightRaw, y)),
        stateBoundary);
    CHECK_FALSE(onBoundary.targetFromSnapshotN);
    CHECK_FALSE(stateBoundary.pointerCapture.has_value());
}

// ── Step 1c ─────────────────────────────────────────────────────────────────
TEST_CASE("batch planning projects pointer capture using snapshot N") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const std::vector<UIInputEvent> events{
        f.PointerDown(20, PxPoint(10.0f, 10.0f)),
        f.PointerMove(21, PxPoint(700.0f, 500.0f)),
        f.PointerUp(22, PxPoint(700.0f, 500.0f)),
        // 놓은 뒤의 한 걸음. 캡처가 실제로 풀렸는지는 여기서만 보인다 —
        // 이것이 없으면 "캡처를 영원히 붙든다"는 구현도 통과한다.
        f.PointerMove(23, PxPoint(700.0f, 500.0f))};
    const auto plans = f.ProjectAll(*n, events);
    REQUIRE(plans.size() == 4);
    REQUIRE(plans[0].targetFromSnapshotN);
    CHECK(*plans[0].targetFromSnapshotN == f.HighButtonIdentity());
    CHECK(plans[1].targetFromSnapshotN == plans[0].targetFromSnapshotN);
    CHECK(plans[2].targetFromSnapshotN == plans[0].targetFromSnapshotN);
    CHECK_FALSE(plans[3].targetFromSnapshotN);
    // (700,500)은 어떤 hit 기록도 담지 않는다. 잡혀 있지 않았다면 대상이 없다.
    for (const auto& hit : n->hitTargets) {
        CHECK_FALSE(HitRecordContains(hit, PxPoint(700.0f, 500.0f)));
    }
}

TEST_CASE("a pointer departure clears hover and capture but not focus") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    UIPlanningState state = f.InitialPlanningState();
    const auto down = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(50, PxPoint(210.0f, 10.0f)),
        state);
    REQUIRE(down.targetFromSnapshotN);
    REQUIRE(state.pointerCapture.has_value());
    REQUIRE(state.focused.has_value());
    // 누름은 hover도 그 대상으로 옮긴다.
    CHECK(state.hovered == f.ComboButtonIdentity());
    const auto focusedBefore = *state.focused;

    const auto departure =
        f.Router().PlanNext(*n, f.SurfaceWindowId(), f.PointerDeparture(51),
                            state);
    CHECK(departure.surfaceEligible);
    CHECK_FALSE(departure.targetFromSnapshotN);
    CHECK_FALSE(state.pointerCapture.has_value());
    CHECK_FALSE(state.hovered.has_value());
    CHECK_FALSE(state.pointerValid);
    // 포인터가 나갔다고 키보드 포커스가 사라지지는 않는다.
    CHECK(state.focused == focusedBefore);

    // 그리고 그 뒤의 스크롤은 위치가 없으므로 대상이 없다. (0,0)을 위치로
    // 삼는 구현은 여기서만 드러난다.
    const auto scroll =
        f.Router().PlanNext(*n, f.SurfaceWindowId(), f.Scroll(52, 0, -64),
                            state);
    CHECK(scroll.surfaceEligible);
    CHECK_FALSE(scroll.targetFromSnapshotN);
    CHECK(scroll.ScrollTargetCount() == 0);
}

// ── Step 1d ─────────────────────────────────────────────────────────────────
TEST_CASE("key follows projected focus but text stays on its ingest owner") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const auto oldInput = f.InputAIdentity();
    const auto newInput = f.InputBIdentity();
    const auto newAction = f.InputBButtonIdentity();
    const auto newFocus = f.InputBSelectableIdentity();
    // 포인터를 **다른 위젯 위로 옮긴 뒤에** 키를 보낸다. 옮기지 않으면
    // "투사된 포커스로 라우팅한다"와 "마지막 포인터 위치로 라우팅한다"가 같은
    // 답을 내고, 그 둘을 구별하지 못하는 시험은 후자의 구현에서도 통과한다.
    UIPlanningState state = f.InitialPlanningState();
    const std::vector<UIInputEvent> events{
        f.PointerDown(23, PxPoint(410.0f, 410.0f)),  // inputB 위
        f.PointerUp(24, PxPoint(410.0f, 410.0f)),    // 캡처를 놓는다
        f.PointerMove(25, PxPoint(10.0f, 10.0f)),    // 포인터는 high 위로
        f.KeyDown(26, 'a'),
        f.TextCommit(27, "old", UIFrozenInputFixture::RuntimeStamp(oldInput, 7)),
        f.TextCommit(28, "editor", UIFrozenInputFixture::EditorStamp(9))};
    const auto plans = f.ProjectAll(*n, events, state);
    REQUIRE(plans.size() == 6);

    // 포인터가 새 포커스를 투사했다.
    REQUIRE(plans[0].targetFromSnapshotN);
    CHECK(*plans[0].targetFromSnapshotN == newAction);
    // 포인터는 지금 high 위에 있다.
    REQUIRE(plans[2].targetFromSnapshotN);
    CHECK(*plans[2].targetFromSnapshotN == f.HighButtonIdentity());
    CHECK(state.hovered == f.HighButtonIdentity());

    // 그런데 키는 포인터가 아니라 투사된 포커스를 따라간다.
    REQUIRE(plans[3].targetFromSnapshotN);
    CHECK(*plans[3].targetFromSnapshotN == newAction);
    CHECK(*plans[3].targetFromSnapshotN != f.HighButtonIdentity());
    REQUIRE(plans[3].TargetFor(UIEventStage::Focus));
    CHECK(plans[3].TargetFor(UIEventStage::Focus)->runtimeTarget == newFocus);

    // 이미 ingest된 텍스트는 그 투사를 따라가지 않는다. 도장이 답이다.
    REQUIRE(plans[4].targetFromSnapshotN);
    CHECK(*plans[4].targetFromSnapshotN == oldInput);
    CHECK(*plans[4].targetFromSnapshotN != newInput);
    CHECK(*plans[4].targetFromSnapshotN != f.HighButtonIdentity());
    REQUIRE(plans[4].TargetFor(UIEventStage::TextInput));
    CHECK(plans[4].TargetFor(UIEventStage::TextInput)->runtimeTarget ==
          oldInput);

    // 에디터 도장은 UI 대상이 없다. ImGui 관찰자의 것이다.
    CHECK_FALSE(plans[5].targetFromSnapshotN);
    CHECK_FALSE(plans[5].TargetFor(UIEventStage::TextInput));
}

TEST_CASE("a key with no projected focus is targetless, not pointer-routed") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    UIPlanningState state = f.InitialPlanningState();
    // high 버튼에는 UISelectable이 없으므로 그 위를 지나도 포커스는 서지
    // 않는다. 포인터 위치는 분명히 hit 기록 위에 있다.
    const auto hover = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerMove(130, PxPoint(10.0f, 10.0f)),
        state);
    REQUIRE(hover.targetFromSnapshotN);
    CHECK(*hover.targetFromSnapshotN == f.HighButtonIdentity());
    REQUIRE_FALSE(state.focused.has_value());

    const auto key =
        f.Router().PlanNext(*n, f.SurfaceWindowId(), f.KeyDown(131), state);
    CHECK(key.surfaceEligible);
    CHECK_FALSE(key.targetFromSnapshotN);
    CHECK_FALSE(key.TargetFor(UIEventStage::Input));
}

// ── Step 1e ─────────────────────────────────────────────────────────────────
TEST_CASE("callback mutation during dispatch never retargets a preplanned event") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const auto oldInput = f.InputAIdentity();
    const auto newAction = f.InputBButtonIdentity();

    // 배치 전체를 **먼저** 계획한다. 이것이 요점이다 — 발송 중의 어떤 변형도
    // 이미 얼어붙은 대상을 옮길 수 없어야 한다.
    const std::vector<UIInputEvent> events{
        f.PointerDown(60, PxPoint(410.0f, 410.0f)),
        f.PointerUp(61, PxPoint(410.0f, 410.0f)),
        f.KeyDown(62, 'a'),
        f.TextCommit(63, "old", UIFrozenInputFixture::RuntimeStamp(oldInput, 7))};
    const auto plans = f.ProjectAll(*n, events);
    REQUIRE(plans.size() == 4);
    REQUIRE(plans[2].targetFromSnapshotN);
    CHECK(*plans[2].targetFromSnapshotN == newAction);
    REQUIRE(plans[3].targetFromSnapshotN);
    CHECK(*plans[3].targetFromSnapshotN == oldInput);

    // 포인터 콜백이 새 포커스를 재부모하고, 텍스트 소유자의 오브젝트를 숨긴다.
    int callbackRuns = 0;
    f.InputBButton().SetOnClick([&] {
        ++callbackRuns;
        GameObject* inputB = f.World().FindById(kInputBId);
        GameObject* host = f.World().FindById(kReparentHostId);
        REQUIRE(inputB != nullptr);
        REQUIRE(host != nullptr);
        REQUIRE(inputB->SetParent(host));
        f.World().RemoveByIds({kInputAId});
        // 그리고 새 기하를 게시한다. N+1이다 — 어떤 계획도 이 값을 보지 않는다.
        f.Rebuild();
    });

    for (std::size_t i = 0; i < 2; ++i) {
        (void)f.Router().HandleEvent(f.World(), *n, plans[i], f.Diagnostics());
    }
    REQUIRE(callbackRuns == 1);

    // 키 계획은 투사된 N 정체성을 그대로 든다. 재부모는 식별자를 무효화하지
    // 않으므로 실제로 배달된다.
    const auto key =
        f.Router().HandleEvent(f.World(), *n, plans[2], f.Diagnostics());
    REQUIRE(plans[2].targetFromSnapshotN);
    CHECK(*plans[2].targetFromSnapshotN == newAction);
    REQUIRE(key.resolvedStageTarget.has_value());
    CHECK(key.resolvedStageTarget->runtimeTarget == newAction);
    CHECK(key.callbackDelivered);

    // 텍스트 계획은 옛 도장을 그대로 든다. 그 컴포넌트는 사라졌으므로 배달은
    // 되지 않지만, 대상이 새 포커스로 옮겨 가지는 않는다.
    const auto text =
        f.Router().HandleEvent(f.World(), *n, plans[3], f.Diagnostics());
    REQUIRE(plans[3].targetFromSnapshotN);
    CHECK(*plans[3].targetFromSnapshotN == oldInput);
    CHECK(*plans[3].targetFromSnapshotN != newAction);
    CHECK_FALSE(text.callbackDelivered);
    REQUIRE(text.resolvedStageTarget.has_value());
    CHECK(text.resolvedStageTarget->runtimeTarget == oldInput);
}

// ── Step 1f ─────────────────────────────────────────────────────────────────
TEST_CASE("handlers resolve only their frozen stage target") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto planning = f.InitialPlanningState();
    // combo(8) 위의 한 점. 동작/포커스/텍스트가 서로 다른 컴포넌트이고
    // 조상 스크롤이 둘이다.
    const auto plan = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(70, PxPoint(210.0f, 10.0f)),
        planning);

    REQUIRE(plan.TargetFor(UIEventStage::Input));
    CHECK(plan.TargetFor(UIEventStage::Input)->runtimeTarget ==
          f.ComboButtonIdentity());
    REQUIRE(plan.TargetFor(UIEventStage::Focus));
    CHECK(plan.TargetFor(UIEventStage::Focus)->runtimeTarget ==
          f.ComboSelectableIdentity());
    REQUIRE(plan.TargetFor(UIEventStage::TextInput));
    CHECK(plan.TargetFor(UIEventStage::TextInput)->runtimeTarget ==
          f.ComboTextInputIdentity());
    REQUIRE(plan.TargetFor(UIEventStage::Scroll, 0));
    CHECK(plan.TargetFor(UIEventStage::Scroll, 0)->runtimeTarget ==
          f.ScrollIdentity(kInnerScrollId));
    REQUIRE(plan.TargetFor(UIEventStage::Scroll, 1));
    CHECK(plan.TargetFor(UIEventStage::Scroll, 1)->runtimeTarget ==
          f.ScrollIdentity(kOuterScrollId));
    CHECK_FALSE(plan.TargetFor(UIEventStage::Scroll, 2));
    CHECK(plan.ScrollTargetCount() == 2);
    // 넷은 정말로 서로 다른 컴포넌트다. 같은 값이면 위의 다섯 줄은 아무것도
    // 구별하지 못한다.
    CHECK(f.ComboButtonIdentity() != f.ComboSelectableIdentity());
    CHECK(f.ComboButtonIdentity() != f.ComboTextInputIdentity());
    CHECK(f.ScrollIdentity(kInnerScrollId) != f.ScrollIdentity(kOuterScrollId));
    // 아직 아무도 소유자 전이 대상을 채우지 않는다(Task 13).
    CHECK_FALSE(plan.TargetFor(UIEventStage::OwnerTransition));
}

TEST_CASE("dispatch accumulator picks one primary target by the event kind") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();

    SUBCASE("a runtime-stamped text target is primary") {
        auto state = f.FocusedPlanningState(*n);
        const auto plan = f.Router().PlanNext(
            *n, f.SurfaceWindowId(),
            f.TextCommit(80, "x",
                         UIFrozenInputFixture::RuntimeStamp(f.InputAIdentity(),
                                                            5)),
            state);
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        const auto record = std::move(accumulator).Finish();
        CHECK(record.runtimeTarget == f.InputAIdentity());
        CHECK(record.kind == UIInputEventKind::TextCommit);
    }

    SUBCASE("a pointer action target is primary") {
        auto state = f.InitialPlanningState();
        const auto plan = f.Router().PlanNext(
            *n, f.SurfaceWindowId(), f.PointerDownAtOverlap(81), state);
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        const auto record = std::move(accumulator).Finish();
        CHECK(record.runtimeTarget == f.HighButtonIdentity());
    }

    SUBCASE("a key action target is primary") {
        auto state = f.FocusedPlanningState(*n);
        const auto plan =
            f.Router().PlanNext(*n, f.SurfaceWindowId(), f.KeyDown(82), state);
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        const auto record = std::move(accumulator).Finish();
        CHECK(record.runtimeTarget == f.ComboButtonIdentity());
    }

    SUBCASE("a window-focus event has no primary") {
        auto state = f.FocusedPlanningState(*n);
        const auto plan = f.Router().PlanNext(
            *n, f.SurfaceWindowId(), f.WindowFocus(83, false, kSurfaceWindow),
            state);
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        const auto record = std::move(accumulator).Finish();
        CHECK_FALSE(record.runtimeTarget.has_value());
    }

    SUBCASE("a foreign-window event has no primary") {
        auto state = f.InitialPlanningState();
        const auto plan = f.Router().PlanNext(
            *n, f.SurfaceWindowId(),
            f.PointerDown(84, PxPoint(10.0f, 10.0f), kForeignWindow), state);
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        const auto record = std::move(accumulator).Finish();
        CHECK_FALSE(record.runtimeTarget.has_value());
    }

    SUBCASE("the first consumed scroll target becomes primary exactly once") {
        auto state = f.InitialPlanningState();
        const std::vector<UIInputEvent> events{
            f.PointerMove(85, PxPoint(210.0f, 10.0f)), f.Scroll(86, 0, -64)};
        const auto plans = f.ProjectAll(*n, events, state);
        const auto& plan = plans[1];
        REQUIRE(plan.ScrollTargetCount() == 2);

        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        // 아무것도 소비하지 않은 스크롤 결과는 주 대상을 채우지 않는다.
        UIEventHandlerResult unconsumed;
        unconsumed.sequence = plan.event.sequence;
        unconsumed.stage = UIEventStage::Scroll;
        unconsumed.stageTargetOrdinal = 0;
        unconsumed.resolvedStageTarget = plan.TargetFor(UIEventStage::Scroll, 0);
        CHECK(accumulator.Merge(unconsumed) == UIEventMergeStatus::Merged);

        UIEventHandlerResult consumedOuter;
        consumedOuter.sequence = plan.event.sequence;
        consumedOuter.stage = UIEventStage::Scroll;
        consumedOuter.stageTargetOrdinal = 1;
        consumedOuter.consumed = true;
        consumedOuter.actionMask = UIEventActionBit(UIEventAction::Scroll);
        consumedOuter.resolvedStageTarget =
            plan.TargetFor(UIEventStage::Scroll, 1);
        CHECK(accumulator.Merge(consumedOuter) == UIEventMergeStatus::Merged);

        // 두 번째로 소비된 결과가 와도 주 대상은 바뀌지 않는다.
        UIEventHandlerResult consumedInner = consumedOuter;
        consumedInner.stageTargetOrdinal = 0;
        consumedInner.resolvedStageTarget =
            plan.TargetFor(UIEventStage::Scroll, 0);
        CHECK(accumulator.Merge(consumedInner) == UIEventMergeStatus::Merged);

        const auto record = std::move(accumulator).Finish();
        CHECK(record.runtimeTarget == f.ScrollIdentity(kOuterScrollId));
        CHECK(record.consumed);
    }

    SUBCASE("a focus-stage result never replaces the primary") {
        auto state = f.InitialPlanningState();
        const auto plan = f.Router().PlanNext(
            *n, f.SurfaceWindowId(), f.PointerDown(87, PxPoint(210.0f, 10.0f)),
            state);
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        UIEventHandlerResult focusResult;
        focusResult.sequence = plan.event.sequence;
        focusResult.stage = UIEventStage::Focus;
        focusResult.consumed = true;
        focusResult.resolvedStageTarget = plan.TargetFor(UIEventStage::Focus);
        focusResult.resultingFocus = f.ComboSelectableIdentity();
        CHECK(accumulator.Merge(focusResult) == UIEventMergeStatus::Merged);
        const auto record = std::move(accumulator).Finish();
        CHECK(record.runtimeTarget == f.ComboButtonIdentity());
        CHECK(record.resultingFocus == f.ComboSelectableIdentity());
    }
}

TEST_CASE("a result naming another stage's valid target is rejected") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    const auto plan = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(90, PxPoint(210.0f, 10.0f)),
        state);
    const auto focusTarget = plan.TargetFor(UIEventStage::Focus);
    REQUIRE(focusTarget.has_value());

    UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
    // Input 단계를 주장하면서 Focus 단계의 (그 자체로는 멀쩡한) 대상을 든다.
    UIEventHandlerResult crossed;
    crossed.sequence = plan.event.sequence;
    crossed.stage = UIEventStage::Input;
    crossed.consumed = true;
    crossed.callbackDelivered = true;
    crossed.actionMask = UIEventActionBit(UIEventAction::Click);
    crossed.resolvedStageTarget = focusTarget;
    CHECK(accumulator.Merge(crossed) == UIEventMergeStatus::RejectedStageTarget);

    // 알 수 없는 대상도 같은 대접을 받는다.
    UIEventHandlerResult unknown = crossed;
    unknown.resolvedStageTarget =
        UIFrozenTarget{f.LowButtonIdentity(),
                       molga::ui::UIStableComponentKey{kLowButtonId, "UIButton",
                                                       1}};
    CHECK(accumulator.Merge(unknown) == UIEventMergeStatus::RejectedStageTarget);

    // 서로 다른 sequence도 거절이다.
    UIEventHandlerResult wrongSequence;
    wrongSequence.sequence = plan.event.sequence + 1;
    wrongSequence.stage = UIEventStage::Input;
    wrongSequence.consumed = true;
    wrongSequence.resolvedStageTarget = plan.TargetFor(UIEventStage::Input);
    CHECK(accumulator.Merge(wrongSequence) ==
          UIEventMergeStatus::RejectedSequence);

    const auto record = std::move(accumulator).Finish();
    // 어떤 것도 합쳐지지 않았고, 어떤 대상도 대신 들어오지 않았다.
    CHECK(record.runtimeTarget == f.ComboButtonIdentity());
    CHECK_FALSE(record.consumed);
    CHECK_FALSE(record.delivered);
    CHECK(record.actionMask == 0);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                    "ui.input") == 3);
}

TEST_CASE("dispatch accumulator merge order does not change the record") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    const auto plan = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(95, PxPoint(210.0f, 10.0f)),
        state);

    UIEventHandlerResult input;
    input.sequence = plan.event.sequence;
    input.stage = UIEventStage::Input;
    input.actionMask = static_cast<std::uint16_t>(
        UIEventActionBit(UIEventAction::Press) |
        UIEventActionBit(UIEventAction::Hover));
    input.consumed = true;
    input.callbackDelivered = true;
    input.visualDirty = true;
    input.resolvedStageTarget = plan.TargetFor(UIEventStage::Input);

    UIEventHandlerResult focus;
    focus.sequence = plan.event.sequence;
    focus.stage = UIEventStage::Focus;
    focus.actionMask = UIEventActionBit(UIEventAction::Focus);
    focus.arrangementDirty = true;
    focus.resolvedStageTarget = plan.TargetFor(UIEventStage::Focus);
    focus.resultingFocus = f.ComboSelectableIdentity();

    UIEventHandlerResult scroll;
    scroll.sequence = plan.event.sequence;
    scroll.stage = UIEventStage::Scroll;
    scroll.stageTargetOrdinal = 0;
    scroll.actionMask = UIEventActionBit(UIEventAction::Scroll);
    scroll.consumed = true;
    scroll.resolvedStageTarget = plan.TargetFor(UIEventStage::Scroll, 0);

    const auto run = [&](const std::vector<UIEventHandlerResult>& order) {
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        for (const auto& result : order) {
            REQUIRE(accumulator.Merge(result) == UIEventMergeStatus::Merged);
        }
        return std::move(accumulator).Finish();
    };

    const auto a = run({input, focus, scroll});
    const auto b = run({scroll, focus, input});
    const auto c = run({focus, scroll, input});
    for (const auto* record : {&b, &c}) {
        CHECK(record->actionMask == a.actionMask);
        CHECK(record->consumed == a.consumed);
        CHECK(record->delivered == a.delivered);
        CHECK(record->arrangementDirty == a.arrangementDirty);
        CHECK(record->visualDirty == a.visualDirty);
        CHECK(record->runtimeTarget == a.runtimeTarget);
        CHECK(record->canonicalTarget == a.canonicalTarget);
        CHECK(record->resultingFocus == a.resultingFocus);
    }
    // 그리고 그 값이 실제로 셋을 합친 값이다.
    CHECK(a.actionMask ==
          static_cast<std::uint16_t>(UIEventActionBit(UIEventAction::Press) |
                                     UIEventActionBit(UIEventAction::Hover) |
                                     UIEventActionBit(UIEventAction::Focus) |
                                     UIEventActionBit(UIEventAction::Scroll)));
    CHECK(a.consumed);
    CHECK(a.delivered);
    CHECK(a.arrangementDirty);
    CHECK(a.visualDirty);
    CHECK(a.runtimeTarget == f.ComboButtonIdentity());
}

TEST_CASE("a finished dispatch accumulator refuses further results") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    const auto plan = f.Router().PlanNext(*n, f.SurfaceWindowId(),
                                          f.PointerDownAtOverlap(96), state);
    UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
    (void)std::move(accumulator).Finish();
    CHECK(accumulator.Spent());
    UIEventHandlerResult late;
    late.sequence = plan.event.sequence;
    late.stage = UIEventStage::Input;
    late.consumed = true;
    late.resolvedStageTarget = plan.TargetFor(UIEventStage::Input);
    CHECK(accumulator.Merge(late) == UIEventMergeStatus::RejectedSpent);
}

// ── Step 1g ─────────────────────────────────────────────────────────────────
TEST_CASE("a foreign-window event stays in order and touches nothing") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    UIPlanningState state = f.InitialPlanningState();

    const std::vector<UIInputEvent> events{
        f.PointerDown(100, PxPoint(210.0f, 10.0f), kSurfaceWindow),
        // 다른 창의 포커스 상실. 이 표면의 포커스를 놓아서는 안 된다.
        f.WindowFocus(101, false, kForeignWindow),
        f.KeyDown(102, 'a', kSurfaceWindow)};
    const auto plans = f.ProjectAll(*n, events, state);
    REQUIRE(plans.size() == 3);

    // 감사 순서는 그대로다.
    CHECK(plans[0].event.sequence == 100);
    CHECK(plans[1].event.sequence == 101);
    CHECK(plans[2].event.sequence == 102);

    CHECK(plans[0].surfaceEligible);
    CHECK_FALSE(plans[1].surfaceEligible);
    CHECK(plans[2].surfaceEligible);

    // 외부 창 기록은 어떤 단계 대상도 없다.
    CHECK_FALSE(plans[1].targetFromSnapshotN);
    for (const auto stage :
         {UIEventStage::Input, UIEventStage::Focus, UIEventStage::Scroll,
          UIEventStage::TextInput, UIEventStage::OwnerTransition}) {
        CHECK_FALSE(plans[1].TargetFor(stage));
    }
    UIEventDispatchAccumulator foreign(plans[1], f.Diagnostics());
    const auto foreignRecord = std::move(foreign).Finish();
    CHECK_FALSE(foreignRecord.runtimeTarget.has_value());
    CHECK_FALSE(foreignRecord.consumed);
    CHECK_FALSE(foreignRecord.delivered);
    CHECK(foreignRecord.traceOrdinal == 101);

    // 그리고 창 10의 포커스는 살아남았다 — 뒤따르는 키가 그 증인이다.
    CHECK(state.nativeWindowFocused);
    REQUIRE(plans[2].targetFromSnapshotN);
    CHECK(*plans[2].targetFromSnapshotN == f.ComboButtonIdentity());
    CHECK(state.pointerCapture == f.ComboButtonIdentity());

    // 대조군: **같은** 창의 포커스 상실은 실제로 놓는다. 없으면 위의 단언은
    // "포커스 상실을 통째로 무시한다"는 구현에서도 통과한다. 놓는 것은 넷이다 —
    // 그 전에 넷 모두 서 있었는지부터 못 박는다.
    REQUIRE(state.hovered.has_value());
    REQUIRE(state.pointerValid);
    const auto localLoss = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.WindowFocus(103, false, kSurfaceWindow),
        state);
    CHECK(localLoss.surfaceEligible);
    CHECK_FALSE(state.focused.has_value());
    CHECK_FALSE(state.pointerCapture.has_value());
    CHECK_FALSE(state.hovered.has_value());
    CHECK_FALSE(state.pointerValid);
    CHECK_FALSE(state.nativeWindowFocused);
}

TEST_CASE("a detached surface keeps its own focus and capture state") {
    UIFrozenInputFixture f;
    const auto n10 = f.Rebuild(kSurfaceWindow);
    UIPlanningState state10 = f.InitialPlanningState(kSurfaceWindow);
    const auto plan10 = f.Router().PlanNext(
        *n10, kSurfaceWindow, f.PointerDown(110, PxPoint(210.0f, 10.0f)),
        state10);
    REQUIRE(plan10.targetFromSnapshotN);
    const auto capture10 = state10.pointerCapture;
    const auto focus10 = state10.focused;
    REQUIRE(capture10.has_value());
    REQUIRE(focus10.has_value());

    // 같은 월드를 창 20에서도 게시한다. 두 표면은 서로의 상태를 나눠 갖지
    // 않는다.
    const auto n20 = f.Rebuild(kForeignWindow);
    REQUIRE(n20->surfaceWindowId == kForeignWindow);
    UIPlanningState state20 = f.InitialPlanningState(kForeignWindow);
    const auto plan20 = f.Router().PlanNext(
        *n20, kForeignWindow,
        f.PointerDown(111, PxPoint(10.0f, 10.0f), kForeignWindow), state20);
    REQUIRE(plan20.targetFromSnapshotN);
    CHECK(*plan20.targetFromSnapshotN == f.HighButtonIdentity());
    CHECK(state20.pointerCapture == f.HighButtonIdentity());
    // 창 10의 상태는 그대로다.
    CHECK(state10.pointerCapture == capture10);
    CHECK(state10.focused == focus10);
    CHECK(state20.pointerCapture != state10.pointerCapture);

    // 다른 표면의 스냅샷으로는 이 표면의 대상을 이름할 수 없다.
    UIPlanningState mismatched = f.InitialPlanningState(kForeignWindow);
    const auto crossed = f.Router().PlanNext(
        *n10, kForeignWindow,
        f.PointerDown(112, PxPoint(10.0f, 10.0f), kForeignWindow), mismatched);
    CHECK_FALSE(crossed.targetFromSnapshotN);
    CHECK_FALSE(mismatched.pointerCapture.has_value());
}

// ── Step 1h ─────────────────────────────────────────────────────────────────
TEST_CASE("unstamped text is targetless and never follows projected focus") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.FocusedPlanningState(*n);
    REQUIRE(state.focused.has_value());

    struct Row {
        const char* name;
        TextInputOwnerStamp stamp;
        bool dropPayload = false;
    };
    TextInputOwnerStamp zeroGeneration =
        UIFrozenInputFixture::RuntimeStamp(f.InputAIdentity(), 0);
    TextInputOwnerStamp emptyRuntimeTarget;
    emptyRuntimeTarget.kind = TextInputOwnerKind::RuntimeUITextInput;
    emptyRuntimeTarget.generation = 7;
    TextInputOwnerStamp editorWithTarget =
        UIFrozenInputFixture::EditorStamp(9);
    editorWithTarget.runtimeTarget = f.InputAIdentity();

    const std::vector<Row> rows{
        {"no payload", TextInputOwnerStamp{}, true},
        {"None stamp", TextInputOwnerStamp{}, false},
        {"editor stamp", UIFrozenInputFixture::EditorStamp(9), false},
        {"editor stamp carrying a runtime target", editorWithTarget, false},
        {"runtime stamp with no target", emptyRuntimeTarget, false},
        {"runtime stamp with generation 0", zeroGeneration, false},
    };

    std::uint64_t sequence = 200;
    for (const auto& row : rows) {
        CAPTURE(row.name);
        UIInputEvent event = f.TextCommit(++sequence, "x", row.stamp);
        if (row.dropPayload) event.text.reset();
        const auto plan =
            f.Router().PlanNext(*n, f.SurfaceWindowId(), event, state);
        CHECK(plan.surfaceEligible);
        CHECK_FALSE(plan.targetFromSnapshotN);
        CHECK_FALSE(plan.textInputTargetFromSnapshotN);
        CHECK_FALSE(plan.TargetFor(UIEventStage::Input));
        CHECK_FALSE(plan.TargetFor(UIEventStage::TextInput));
    }

    // 대조군. 유효한 런타임 도장 하나는 실제로 대상을 낸다 — 없으면 위의
    // 여섯 줄은 "텍스트에는 언제나 대상이 없다"는 구현에서도 전부 통과한다.
    const auto stamped = f.Router().PlanNext(
        *n, f.SurfaceWindowId(),
        f.TextCommit(++sequence, "x",
                     UIFrozenInputFixture::RuntimeStamp(f.InputAIdentity(), 7)),
        state);
    REQUIRE(stamped.targetFromSnapshotN);
    CHECK(*stamped.targetFromSnapshotN == f.InputAIdentity());
    REQUIRE(stamped.TargetFor(UIEventStage::TextInput));
    // 그리고 투사된 포커스와 다른 값이다.
    CHECK(state.focused != f.InputAIdentity());
}

// ── Step 2 ──────────────────────────────────────────────────────────────────
TEST_CASE("UI events preserve signed scroll and axis values") {
    molga::ui::UIInputEvent scroll;
    scroll.kind = molga::ui::UIInputEventKind::Scroll;
    scroll.logicalDelta = {molga::Fixed26_6::FromRaw(-17),
                           molga::Fixed26_6::FromRaw(29)};
    molga::ui::UIInputEvent axis;
    axis.kind = molga::ui::UIInputEventKind::GamepadAxis;
    axis.axisValue = molga::Fixed26_6::FromRaw(-31);
    axis.control = 3;  // 이름 붙은 축. 불리언으로 접히지 않는다.
    CHECK(scroll.logicalDelta.x.Raw() == -17);
    CHECK(scroll.logicalDelta.y.Raw() == 29);
    CHECK(axis.axisValue.Raw() == -31);
    CHECK(axis.control == 3);
    const molga::platform::NativeKeyModifiers command{false, false, false, true};
    CHECK(command == molga::platform::NativeKeyModifiers{false, false, false,
                                                         true});
    CHECK(command != molga::platform::NativeKeyModifiers{});
}

TEST_CASE("native key modifiers compare every one of their four booleans") {
    // C++17이다. defaulted operator<=>도 rewritten comparison도 없으므로,
    // 필드 하나를 빠뜨린 비교는 그 필드 하나만 다른 쌍에서만 드러난다.
    static_assert(!std::is_same<decltype(&NativeKeyModifiers::operator==),
                                void*>::value,
                  "NativeKeyModifiers must define its own operator==");
    const NativeKeyModifiers none{};
    const std::vector<std::pair<const char*, NativeKeyModifiers>> single{
        {"shift", NativeKeyModifiers{true, false, false, false}},
        {"control", NativeKeyModifiers{false, true, false, false}},
        {"alt", NativeKeyModifiers{false, false, true, false}},
        {"gui", NativeKeyModifiers{false, false, false, true}}};
    for (const auto& row : single) {
        CAPTURE(row.first);
        CHECK(row.second != none);
        CHECK_FALSE(row.second == none);
        CHECK(row.second == row.second);
        for (const auto& other : single) {
            if (other.first == row.first) continue;
            CAPTURE(other.first);
            CHECK(row.second != other.second);
        }
    }
    // Control+Enter와 Command+Enter는 Milestone A에서 같은 분기를 타지만 값은
    // 다르다. 두 값이 같아지면 그 분기 선택은 시험할 수 없다.
    const NativeKeyModifiers control{false, true, false, false};
    const NativeKeyModifiers gui{false, false, false, true};
    CHECK(control != gui);
    // 열여섯 조합 전부가 서로 다르다.
    std::vector<NativeKeyModifiers> all;
    for (int bits = 0; bits < 16; ++bits) {
        all.push_back(NativeKeyModifiers{(bits & 1) != 0, (bits & 2) != 0,
                                         (bits & 4) != 0, (bits & 8) != 0});
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            CAPTURE(i);
            CAPTURE(j);
            CHECK(all[i] != all[j]);
        }
    }
}

TEST_CASE("text input owner stamps compare every field") {
    UIRuntimeTargetIdentity a;
    a.worldGeneration = 3;
    a.objectId = 4;
    a.componentRuntimeTypeId = 5;
    a.componentInstanceId = 6;
    UIRuntimeTargetIdentity b = a;
    b.componentInstanceId = 7;

    const auto runtimeA = UIFrozenInputFixture::RuntimeStamp(a, 11);
    auto runtimeB = UIFrozenInputFixture::RuntimeStamp(b, 11);
    auto runtimeGeneration = UIFrozenInputFixture::RuntimeStamp(a, 12);
    CHECK(runtimeA == runtimeA);
    CHECK(runtimeA != runtimeB);
    CHECK(runtimeA != runtimeGeneration);
    CHECK(static_cast<bool>(runtimeA));
    CHECK(runtimeA.WellFormed());
    CHECK(runtimeA.NamesRuntimeTarget());

    // 세대 0은 도장이 아니다.
    CHECK_FALSE(static_cast<bool>(UIFrozenInputFixture::RuntimeStamp(a, 0)));
    // 에디터 도장은 런타임 대상을 이름하지 않는다.
    const auto editor = UIFrozenInputFixture::EditorStamp(11);
    CHECK(editor.WellFormed());
    CHECK_FALSE(editor.NamesRuntimeTarget());
    CHECK(editor != runtimeA);
    // 대상을 든 에디터 도장은 잘못 만들어진 값이다.
    auto editorWithTarget = editor;
    editorWithTarget.runtimeTarget = a;
    CHECK_FALSE(editorWithTarget.WellFormed());
    // 대상 없는 런타임 도장도 마찬가지다.
    TextInputOwnerStamp runtimeNoTarget;
    runtimeNoTarget.kind = TextInputOwnerKind::RuntimeUITextInput;
    runtimeNoTarget.generation = 11;
    CHECK_FALSE(runtimeNoTarget.WellFormed());
    CHECK_FALSE(runtimeNoTarget.NamesRuntimeTarget());
}

// ── Step 7: 벡터 오버로드를 컴파일 시각에 금지한다 ──────────────────────────
//
// 서명이 아니라 **이름**을 본다. 한 인자짜리 호출식으로 탐지하면
// HandleEvents(World&, const UISnapshot&, const std::vector<PlannedUIEvent>&,
// TextDiagnosticSink&) — HandleEvent를 그대로 본뜬, 가장 그럴듯한 금지 대상 —
// 이 그냥 빠져나간다(라운드 3 리뷰 F5). Task 12.2와 14.1이 이 특성을 믿고 자기
// static_assert 한 줄을 더하므로, 서명을 추측하는 탐지는 쓸 수 없다.
//
// 모호한 기반(ambiguous base) 관용구: T와 같은 이름의 데이터 멤버를 가진
// Fallback을 함께 상속하면, T에 그 이름이 **있을 때만** &Derived::Name이
// 모호해져 치환 실패가 된다. 함수든 오버로드 집합이든 템플릿이든, 접근
// 지정자와도 무관하게 이름 하나로 걸린다. T는 final이 아닌 클래스여야 한다 —
// final 서브시스템에 이 줄을 더하면 조용히 통과하는 대신 컴파일이 멈춘다.
namespace {

#define MOLGA_TEST_MEMBER_NAME_DETECTOR(Name)                                  \
    template <typename T>                                                      \
    struct HasMemberNamed##Name {                                              \
        struct Fallback {                                                      \
            int Name;                                                          \
        };                                                                     \
        struct Derived : T, Fallback {};                                       \
        template <typename U, U>                                               \
        struct Check;                                                          \
        template <typename U>                                                  \
        static std::false_type Test(Check<int Fallback::*, &U::Name>*);        \
        template <typename U>                                                  \
        static std::true_type Test(...);                                       \
        static constexpr bool value = decltype(Test<Derived>(nullptr))::value; \
    };

MOLGA_TEST_MEMBER_NAME_DETECTOR(HandleEvents)
MOLGA_TEST_MEMBER_NAME_DETECTOR(Process)
#undef MOLGA_TEST_MEMBER_NAME_DETECTOR

// 탐지기 자신의 대조군. 이것이 없으면 "언제나 false"인 특성도 아래의 모든
// 금지를 통과하고, 그 순간 이 시험은 아무것도 금지하지 못한다. 모양마다 하나:
// 한 인자, HandleEvent를 본뜬 네 인자, 비-const 참조, 공개되지 않은 멤버,
// 멤버 템플릿.
struct OneArgumentCanary {
    void HandleEvents(const std::vector<PlannedUIEvent>&) {}
    void Process(const std::vector<UIInputEvent>&) {}
};
struct MirroredSignatureCanary {
    UIEventHandlerResult HandleEvents(::World&, const UISnapshot&,
                                      const std::vector<PlannedUIEvent>&,
                                      molga::text::TextDiagnosticSink&) {
        return {};
    }
    void Process(::World&, const UISnapshot&, std::vector<UIInputEvent>&,
                 molga::text::TextDiagnosticSink&) {}
};
class NonPublicCanary {
    void HandleEvents(std::vector<UIInputEvent>) {}
    template <typename E>
    void Process(const std::vector<E>&) {}
};
// 음성 대조군: 허용된 한 이벤트짜리 이름은 걸리지 않는다. 이것이 없으면
// "HandleEvent*로 시작하는 모든 이름"을 거는 탐지기도 통과한다.
struct SingularNameControl {
    UIEventHandlerResult HandleEvent(::World&, const UISnapshot&,
                                     const PlannedUIEvent&,
                                     molga::text::TextDiagnosticSink&) {
        return {};
    }
};

static_assert(HasMemberNamedHandleEvents<OneArgumentCanary>::value,
              "the detector must see a vector HandleEvents overload");
static_assert(HasMemberNamedHandleEvents<MirroredSignatureCanary>::value,
              "the detector must see a HandleEvent-shaped HandleEvents");
static_assert(HasMemberNamedProcess<MirroredSignatureCanary>::value,
              "the detector must see a multi-argument Process");
static_assert(HasMemberNamedHandleEvents<NonPublicCanary>::value &&
                  HasMemberNamedProcess<NonPublicCanary>::value,
              "the detector must see non-public and template members");
static_assert(!HasMemberNamedHandleEvents<SingularNameControl>::value &&
                  !HasMemberNamedProcess<SingularNameControl>::value,
              "the detector must not flag the one-event HandleEvent");

// 그리고 실제 금지. UIFocusSystem(Task 12.2)과 UITextInputSystem(Task 14.1)은
// 아직 존재하지 않으므로, 그 두 타입이 생기는 태스크가 여기에 자기 줄을
// 더한다 — 특성은 그대로 쓰고 타입만 늘리면 된다.
template <typename T>
constexpr bool ForbidsEventVectors() {
    return !HasMemberNamedHandleEvents<T>::value &&
           !HasMemberNamedProcess<T>::value;
}
static_assert(ForbidsEventVectors<UIInputRouter>(),
              "UIInputRouter must not expose a vector-processing API");
static_assert(ForbidsEventVectors<molga::ui::UIScrollSystem>(),
              "UIScrollSystem must not expose a vector-processing API");

}  // namespace

TEST_CASE("no UI subsystem exposes an event-vector processing API") {
    // static_assert가 이미 컴파일 시각에 답했다. 이 케이스는 그 답이 실제로
    // 이 실행 파일 안에서 평가되었다는 것을 보고서에 남긴다.
    CHECK(ForbidsEventVectors<UIInputRouter>());
    CHECK(ForbidsEventVectors<molga::ui::UIScrollSystem>());
    CHECK_FALSE(ForbidsEventVectors<OneArgumentCanary>());
    CHECK_FALSE(ForbidsEventVectors<MirroredSignatureCanary>());
    CHECK_FALSE(ForbidsEventVectors<NonPublicCanary>());
    CHECK(ForbidsEventVectors<SingularNameControl>());
    // 그리고 한 이벤트짜리 API는 실제로 있다.
    CHECK(std::is_same<decltype(std::declval<UIInputRouter&>().HandleEvent(
                           std::declval<::World&>(),
                           std::declval<const UISnapshot&>(),
                           std::declval<const PlannedUIEvent&>(),
                           std::declval<molga::text::TextDiagnosticSink&>())),
                       UIEventHandlerResult>::value);
}

// ── M18 / 누산기 생성자: 표면 자격은 계획 자신의 접근자가 지킨다 ────────────
//
// PlanNext는 외부 창/외부 표면 이벤트를 어떤 단계 필드도 채우기 전에
// 돌려보낸다. 그래서 PlanNext를 거친 부적격 계획만 보는 시험에서는
// TargetFor와 누산기의 surfaceEligible 검사가 계획기와 중복이고, 지워도
// 초록이다 — 캠페인에서 M18이 정확히 그렇게 살아남았다.
//
// 그 검사는 장식이 아니다. Task 12.2의 UIFocusSystem::ProjectEvent는 이미
// 만들어진 계획에 focusDestinationFromSnapshotN을 **쓴다**. 투사가 부적격
// 계획에 무언가를 쓰는 날 배달을 막는 것은 이 검사들뿐이다. 그래서 여기서는
// 모든 단계 필드가 채워진 부적격 계획을 직접 만든다.
namespace {

std::optional<UIFrozenTarget> FrozenFocusIn(const UISnapshot& snapshot,
                                            const UIRuntimeTargetIdentity& focus) {
    for (const auto& hit : snapshot.hitTargets) {
        if (hit.focusTarget && hit.focusTarget->runtimeTarget == focus) {
            return hit.focusTarget;
        }
    }
    return std::nullopt;
}

std::optional<UIFrozenTarget> FrozenTextInputIn(
    const UISnapshot& snapshot, const UIRuntimeTargetIdentity& input) {
    for (const auto& hit : snapshot.hitTargets) {
        if (hit.textInputTarget && hit.textInputTarget->runtimeTarget == input) {
            return hit.textInputTarget;
        }
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("an ineligible plan names no stage target even with every field populated") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    // 실제로 라우팅된 계획 하나. combo(8)는 동작/포커스/텍스트 대상과 조상
    // 스크롤 둘을 모두 낸다.
    PlannedUIEvent populated = f.Router().PlanNext(
        *n, f.SurfaceWindowId(), f.PointerDown(140, PxPoint(210.0f, 10.0f)),
        state);
    REQUIRE(populated.surfaceEligible);
    REQUIRE(populated.targetFromSnapshotN.has_value());
    REQUIRE(populated.canonicalTargetFromSnapshotN.has_value());
    REQUIRE(populated.focusTargetFromSnapshotN.has_value());
    REQUIRE(populated.textInputTargetFromSnapshotN.has_value());
    REQUIRE(populated.ScrollTargetCount() == 2);

    // 아직 아무도 채우지 않는 두 필드 — Task 12.2의 방향 목적지와 Task 13의
    // 소유자 전이 대상 — 는 N이 실제로 얼려 둔 **다른** 대상으로 손수 채운다.
    // 값이 서로 달라야 어느 필드가 어느 단계로 새어 나갔는지 구별된다.
    const auto destination = FrozenFocusIn(*n, f.InputBSelectableIdentity());
    const auto ownerTarget = FrozenTextInputIn(*n, f.InputAIdentity());
    REQUIRE(destination.has_value());
    REQUIRE(ownerTarget.has_value());
    REQUIRE(*destination != *populated.focusTargetFromSnapshotN);
    REQUIRE(*ownerTarget != *populated.textInputTargetFromSnapshotN);
    populated.focusDestinationFromSnapshotN = destination;
    populated.ownerTransitionTargetFromSnapshotN = ownerTarget;

    const UIFrozenTarget input{*populated.targetFromSnapshotN,
                               *populated.canonicalTargetFromSnapshotN};
    const UIFrozenTarget inner = populated.scrollTargetsFromSnapshotN[0];
    const UIFrozenTarget outer = populated.scrollTargetsFromSnapshotN[1];
    REQUIRE(inner != outer);

    // 대조군: 적격인 같은 계획은 단계마다 자기 값을 돌려준다. 이것이 없으면
    // "언제나 nullopt"인 TargetFor도 아래의 부적격 단언을 전부 통과한다.
    CHECK(populated.TargetFor(UIEventStage::Input) == input);
    // 목적지가 있으면 목적지가 포커스 대상을 이긴다.
    CHECK(populated.TargetFor(UIEventStage::Focus) == *destination);
    CHECK(populated.TargetFor(UIEventStage::Focus) !=
          populated.focusTargetFromSnapshotN);
    CHECK(populated.TargetFor(UIEventStage::TextInput) ==
          *populated.textInputTargetFromSnapshotN);
    CHECK(populated.TargetFor(UIEventStage::Scroll, 0) == inner);
    CHECK(populated.TargetFor(UIEventStage::Scroll, 1) == outer);
    CHECK_FALSE(populated.TargetFor(UIEventStage::Scroll, 2));
    CHECK(populated.TargetFor(UIEventStage::OwnerTransition) == *ownerTarget);

    // 같은 필드, 부적격. 어떤 단계도, 어떤 스크롤 순번도 대상을 내지 않는다.
    PlannedUIEvent ineligible = populated;
    ineligible.surfaceEligible = false;
    for (const auto stage : {UIEventStage::Input, UIEventStage::Focus,
                             UIEventStage::TextInput,
                             UIEventStage::OwnerTransition}) {
        CAPTURE(static_cast<int>(stage));
        CHECK_FALSE(ineligible.TargetFor(stage));
    }
    for (std::size_t ordinal = 0; ordinal <= ineligible.ScrollTargetCount();
         ++ordinal) {
        CAPTURE(ordinal);
        CHECK_FALSE(ineligible.TargetFor(UIEventStage::Scroll, ordinal));
    }

    // 누산기: 부적격 계획의 감사 기록은 주 대상이 없다(생성자의 검사). 그리고
    // 채워진 동작 대상을 이름하는 결과는 거절된다(TargetFor의 검사).
    UIEventDispatchAccumulator accumulator(ineligible, f.Diagnostics());
    UIEventHandlerResult named;
    named.sequence = ineligible.event.sequence;
    named.stage = UIEventStage::Input;
    named.consumed = true;
    named.callbackDelivered = true;
    named.actionMask = UIEventActionBit(UIEventAction::Press);
    named.resolvedStageTarget = input;
    CHECK(accumulator.Merge(named) == UIEventMergeStatus::RejectedStageTarget);
    const auto record = std::move(accumulator).Finish();
    CHECK_FALSE(record.runtimeTarget.has_value());
    CHECK_FALSE(record.canonicalTarget.has_value());
    CHECK_FALSE(record.consumed);
    CHECK_FALSE(record.delivered);
    CHECK(record.actionMask == 0);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                    "ui.input") == 1);

    // 발송도 막힌다. 채워진 동작 대상은 살아 있는 버튼이지만 눌리지 않는다.
    REQUIRE(molga::ui::ResolveTarget(f.World(), input.runtimeTarget) != nullptr);
    const auto blocked =
        f.Router().HandleEvent(f.World(), *n, ineligible, f.Diagnostics());
    CHECK_FALSE(blocked.callbackDelivered);
    CHECK_FALSE(blocked.resolvedStageTarget.has_value());
    CHECK_FALSE(f.ComboButton().IsPressed());

    // 대조군: 같은 계획이 적격이면 실제로 눌린다. 이것이 없으면 "눌리지
    // 않았다"는 관찰이 살아 있는지 알 수 없다.
    const auto delivered =
        f.Router().HandleEvent(f.World(), *n, populated, f.Diagnostics());
    CHECK(delivered.callbackDelivered);
    CHECK(f.ComboButton().IsPressed());
}

// ── M25: 스크롤의 주 대상은 계획이 아니라 소비가 정한다 ─────────────────────
//
// 기존 "첫 소비 대상이 정확히 한 번 주 대상이 된다" 케이스는 언제나 누군가
// 소비하는 결과로 끝나므로, 누산기가 계획의 동작 대상을 미리 채워 두어도 그
// 값이 소비자에게 덮여 같은 답이 나왔다 — 캠페인에서 M25가 살아남은 이유다.
TEST_CASE("a scroll's primary is its first consumer, never the planned action target") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    const std::vector<UIInputEvent> events{
        f.PointerMove(150, PxPoint(210.0f, 10.0f)), f.Scroll(151, 0, -64)};
    const auto plans = f.ProjectAll(*n, events, state);
    REQUIRE(plans.size() == 2);
    const auto& plan = plans[1];
    REQUIRE(plan.event.kind == UIInputEventKind::Scroll);
    // 전제: 이 계획은 동작 대상을 **이름한다**. 이름하지 않는다면 "계획에서
    // 주 대상을 미리 채운다"는 구현도 빈 값을 채워 두 구현이 같은 답을 낸다.
    REQUIRE(plan.targetFromSnapshotN.has_value());
    REQUIRE(plan.ScrollTargetCount() == 2);
    const UIFrozenTarget inner = *plan.TargetFor(UIEventStage::Scroll, 0);
    const UIFrozenTarget outer = *plan.TargetFor(UIEventStage::Scroll, 1);
    // 소비자는 동작 대상과 다른 컴포넌트다. 같다면 (b)는 두 구현을 구별하지
    // 못한다.
    REQUIRE(outer.runtimeTarget != *plan.targetFromSnapshotN);
    REQUIRE(inner.runtimeTarget != *plan.targetFromSnapshotN);

    const auto scrollResult = [&plan](std::size_t ordinal, bool consumed) {
        UIEventHandlerResult result;
        result.sequence = plan.event.sequence;
        result.stage = UIEventStage::Scroll;
        result.stageTargetOrdinal = ordinal;
        result.consumed = consumed;
        if (consumed) result.actionMask = UIEventActionBit(UIEventAction::Scroll);
        result.resolvedStageTarget = plan.TargetFor(UIEventStage::Scroll, ordinal);
        return result;
    };

    SUBCASE("(a) nothing consumes: the primary stays empty") {
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        // 진짜 Input 단계 결과. 동작 대상에 전달되지만 스크롤을 소비하지 않는다.
        const auto inputStage =
            f.Router().HandleEvent(f.World(), *n, plan, f.Diagnostics());
        REQUIRE(inputStage.callbackDelivered);
        REQUIRE_FALSE(inputStage.consumed);
        CHECK(accumulator.Merge(inputStage) == UIEventMergeStatus::Merged);
        CHECK(accumulator.Merge(scrollResult(0, false)) ==
              UIEventMergeStatus::Merged);
        CHECK(accumulator.Merge(scrollResult(1, false)) ==
              UIEventMergeStatus::Merged);
        const auto record = std::move(accumulator).Finish();
        CHECK_FALSE(record.runtimeTarget.has_value());
        CHECK_FALSE(record.canonicalTarget.has_value());
        CHECK_FALSE(record.consumed);
        // 전달은 있었다. 주 대상이 비어 있는 것은 전달이 없어서가 아니라
        // 아무도 소비하지 않아서다.
        CHECK(record.delivered);
    }

    SUBCASE("(b) the outer scroll consumes: the primary is that consumer") {
        UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
        const auto inputStage =
            f.Router().HandleEvent(f.World(), *n, plan, f.Diagnostics());
        CHECK(accumulator.Merge(inputStage) == UIEventMergeStatus::Merged);
        CHECK(accumulator.Merge(scrollResult(0, false)) ==
              UIEventMergeStatus::Merged);
        CHECK(accumulator.Merge(scrollResult(1, true)) ==
              UIEventMergeStatus::Merged);
        const auto record = std::move(accumulator).Finish();
        REQUIRE(record.runtimeTarget.has_value());
        CHECK(*record.runtimeTarget == outer.runtimeTarget);
        CHECK(record.canonicalTarget == outer.canonicalTarget);
        CHECK(*record.runtimeTarget != *plan.targetFromSnapshotN);
        CHECK(*record.runtimeTarget != inner.runtimeTarget);
        CHECK(record.consumed);
    }
}

// ── Step 4f: 프레임 감사 값 ─────────────────────────────────────────────────
namespace {

template <typename T, typename = void>
struct HasFrameIndexMember : std::false_type {};
template <typename T>
struct HasFrameIndexMember<T,
                           std::void_t<decltype(std::declval<T&>().frameIndex)>>
    : std::true_type {};

// 탐지기의 대조군은 합성 타입이 아니라 **진짜** 프레임 감사 값 둘이다. 둘 다
// frameIndex를 들고 있으므로, 탐지기가 고장 나 언제나 false를 내면 여기서
// 먼저 컴파일이 멈춘다 — 그러지 않으면 아래의 금지는 전부 공허하게 참이다.
static_assert(HasFrameIndexMember<molga::ui::UIFrameInput>::value,
              "the frameIndex detector must see UIFrameInput::frameIndex");
static_assert(HasFrameIndexMember<molga::ui::UIFrameResult>::value,
              "the frameIndex detector must see UIFrameResult::frameIndex");
static_assert(std::is_same<decltype(molga::ui::UIFrameInput::frameIndex),
                           std::uint64_t>::value,
              "UIFrameInput::frameIndex must be a full 64-bit frame ordinal");
static_assert(std::is_same<decltype(molga::ui::UIFrameResult::frameIndex),
                           std::uint64_t>::value,
              "UIFrameResult::frameIndex must be a full 64-bit frame ordinal");

// 금지: 스냅샷과 그 재사용 정체성 어디에도 frameIndex가 없다. 하나라도 있으면
// 변경 없는 프레임마다 값이 달라져 "같은 저작 상태는 같은 스냅샷을
// 재사용한다"가 그 자리에서 깨진다.
static_assert(!HasFrameIndexMember<molga::ui::UISnapshot>::value,
              "UISnapshot must not carry a frameIndex");
static_assert(!HasFrameIndexMember<molga::ui::UISnapshotCacheKey>::value,
              "UISnapshotCacheKey must not carry a frameIndex");
static_assert(!HasFrameIndexMember<molga::ui::UILayoutGeometryCacheKey>::value,
              "UILayoutGeometryCacheKey must not carry a frameIndex");
static_assert(!HasFrameIndexMember<molga::ui::UILayoutFastPathStamp>::value,
              "UILayoutFastPathStamp must not carry a frameIndex");

// N과 N+1은 게시된 뒤 불변이다. 감사 결과가 변경 가능한 포인터를 들면, 결과를
// 읽는 쪽이 이번 프레임의 렌더/hit-test가 공유하는 인스턴스를 고쳐 쓸 수 있다.
static_assert(std::is_same<decltype(molga::ui::UIFrameResult::interactionSnapshot),
                           molga::ui::UISnapshotPtr>::value,
              "the interaction snapshot must be an immutable UISnapshotPtr");
static_assert(std::is_same<decltype(molga::ui::UIFrameResult::renderSnapshot),
                           molga::ui::UISnapshotPtr>::value,
              "the render snapshot must be an immutable UISnapshotPtr");
static_assert(std::is_same<molga::ui::UISnapshotPtr,
                           std::shared_ptr<const molga::ui::UISnapshot>>::value,
              "UISnapshotPtr must point at a const UISnapshot");

}  // namespace

TEST_CASE("frame audit values carry the frame index and fail-closed batch seeds") {
    // 기본값은 fail-closed다. 씨앗을 매핑하지 않은(잊은) 부르는 쪽이 유효한
    // 포인터를 주장하면 배치의 첫 스크롤이 (0,0)을 위치로 삼아 대상을 고르고,
    // 창 포커스를 주장하면 포커스를 잃은 창이 키를 받는다.
    const molga::ui::UIFrameInput input;
    CHECK(input.frameIndex == 0);
    CHECK(input.windowId == 0);
    CHECK_FALSE(input.pointerAtBatchStartValid);
    CHECK_FALSE(input.nativeWindowFocusedAtBatchStart);
    CHECK(input.pointerAtBatchStart == RawPoint(0, 0));
    CHECK(input.orderedEvents.empty());
    CHECK(input.uiTicks.empty());

    const molga::ui::UIFrameResult result;
    CHECK(result.frameIndex == 0);
    CHECK(result.interactionSnapshot == nullptr);
    CHECK(result.renderSnapshot == nullptr);
    CHECK(result.dispatches.empty());
}

// ── 라운드 3: 상호작용 불가 기록이 있는 표면 ────────────────────────────────
namespace {

// 그 점을 담는 기록들 가운데 draw order가 가장 큰 것. 겹침이 없다면 nullptr.
const UIHitTargetSnapshot* TopmostRecordAt(const UISnapshot& snapshot,
                                           FixedPoint point,
                                           std::size_t* containingCount) {
    const UIHitTargetSnapshot* topmost = nullptr;
    std::size_t count = 0;
    for (const auto& hit : snapshot.hitTargets) {
        if (!HitRecordContains(hit, point)) continue;
        ++count;
        if (!topmost || topmost->order < hit.order) topmost = &hit;
    }
    if (containingCount) *containingCount = count;
    return topmost;
}

}  // namespace

// A4 (컨트롤러 결정): 포인터 동작 대상은 그 점 아래의 **상호작용 가능한** 맨
// 위 기록이다. 에디터 기본 Button 프리셋은 자식 Label이 버튼을 거의 다 덮는데,
// 맨 위 기록에서 멈추면 그 버튼은 가장자리에서만 눌린다.
TEST_CASE("a press on the editor button preset's label clicks the button") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const FixedPoint onLabel = PxPoint(100.0f, 330.0f);

    // 전제: 라벨 기록이 정말로 버튼 기록 위에 있고, 상호작용 불가다. 이것이
    // 아니면 이 케이스는 투명성을 시험하지 않는다.
    std::size_t containing = 0;
    const auto* topmost = TopmostRecordAt(*n, onLabel, &containing);
    REQUIRE(containing == 2);
    REQUIRE(topmost != nullptr);
    REQUIRE(topmost->canonicalTarget.sceneObjectId == kPresetLabelId);
    REQUIRE_FALSE(topmost->interactable);
    const auto* button = FindHit(*n, f.PresetButtonIdentity());
    REQUIRE(button != nullptr);
    REQUIRE(button->interactable);
    REQUIRE(HitRecordContains(*button, onLabel));

    auto state = f.InitialPlanningState();
    const auto plans = f.ProjectAll(
        *n, {f.PointerMove(700, onLabel), f.PointerDown(701, onLabel),
             f.PointerUp(702, onLabel)},
        state);
    REQUIRE(plans.size() == 3);
    for (const auto& plan : plans) {
        REQUIRE(plan.targetFromSnapshotN.has_value());
        CHECK(*plan.targetFromSnapshotN == f.PresetButtonIdentity());
    }
    CHECK(state.hovered == f.PresetButtonIdentity());
    for (const auto& plan : plans) {
        (void)f.Router().HandleEvent(f.World(), *n, plan, f.Diagnostics());
    }
    CHECK(f.PresetButtonClicks() == 1);
}

TEST_CASE("a disabled button's background over an enabled button passes the press through") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const FixedPoint point = PxPoint(10.0f, 410.0f);
    std::size_t containing = 0;
    const auto* topmost = TopmostRecordAt(*n, point, &containing);
    REQUIRE(containing == 2);
    REQUIRE(topmost != nullptr);
    REQUIRE(topmost->canonicalTarget.sceneObjectId == kDisabledOverlayId);
    REQUIRE_FALSE(topmost->interactable);
    const auto* under = FindHit(*n, f.UnderButtonIdentity());
    REQUIRE(under != nullptr);
    REQUIRE(under->interactable);

    auto state = f.InitialPlanningState();
    const auto plans = f.ProjectAll(
        *n, {f.PointerDown(710, point), f.PointerUp(711, point)}, state);
    for (const auto& plan : plans) {
        REQUIRE(plan.targetFromSnapshotN.has_value());
        CHECK(*plan.targetFromSnapshotN == f.UnderButtonIdentity());
        (void)f.Router().HandleEvent(f.World(), *n, plan, f.Diagnostics());
    }
    CHECK(f.UnderButtonClicks() == 1);
    CHECK(f.DisabledButtonClicks() == 0);
    CHECK_FALSE(f.DisabledButton().IsPressed());
}

// A3: 스크롤은 상호작용 여부와 무관하게 **맨 위** 기록의 스크롤 사슬을 얼린다.
TEST_CASE("a wheel over non-interactable list content scrolls its chain") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const FixedPoint onItem = PxPoint(260.0f, 10.0f);
    std::size_t containing = 0;
    const auto* topmost = TopmostRecordAt(*n, onItem, &containing);
    REQUIRE(topmost != nullptr);
    REQUIRE(topmost->canonicalTarget.sceneObjectId == kScrollItemLabelId);
    REQUIRE_FALSE(topmost->interactable);
    REQUIRE(topmost->scrollTargets.size() == 2);
    // 그 점 아래에 상호작용 가능한 기록은 없다. 있으면 "상호작용 가능한 기록의
    // 사슬을 쓴다"는 구현도 같은 답을 낸다.
    for (const auto& hit : n->hitTargets) {
        if (HitRecordContains(hit, onItem)) REQUIRE_FALSE(hit.interactable);
    }

    auto state = f.InitialPlanningState();
    const auto plans = f.ProjectAll(
        *n, {f.PointerMove(720, onItem), f.Scroll(721, 0, -64)}, state);
    const auto& scroll = plans[1];
    CHECK(scroll.surfaceEligible);
    REQUIRE(scroll.ScrollTargetCount() == 2);
    CHECK(scroll.TargetFor(UIEventStage::Scroll, 0)->runtimeTarget ==
          f.ScrollIdentity(kInnerScrollId));
    CHECK(scroll.TargetFor(UIEventStage::Scroll, 1)->runtimeTarget ==
          f.ScrollIdentity(kOuterScrollId));
    // 상호작용 불가 기록은 동작/포커스/텍스트 대상을 내지 않는다.
    CHECK_FALSE(scroll.targetFromSnapshotN);
    CHECK_FALSE(scroll.TargetFor(UIEventStage::Input));
    CHECK_FALSE(scroll.TargetFor(UIEventStage::Focus));
    CHECK_FALSE(scroll.TargetFor(UIEventStage::TextInput));
    // 그 계획의 Input 단계는 아무것도 하지 않는다.
    const auto input = f.Router().HandleEvent(f.World(), *n, scroll, f.Diagnostics());
    CHECK_FALSE(input.callbackDelivered);
}

TEST_CASE("an overlay above a scroll list occludes the list's scroll chain") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const FixedPoint onOverlay = PxPoint(285.0f, 25.0f);
    std::size_t containing = 0;
    const auto* topmost = TopmostRecordAt(*n, onOverlay, &containing);
    REQUIRE(topmost != nullptr);
    REQUIRE(topmost->canonicalTarget.sceneObjectId == kScrollOverlayId);
    REQUIRE(topmost->scrollTargets.empty());
    // 뒤에는 정말로 스크롤 사슬을 든 목록이 있다. 없으면 "가림"은 아무것도
    // 시험하지 않는다.
    bool listBehind = false;
    for (const auto& hit : n->hitTargets) {
        if (HitRecordContains(hit, onOverlay) && !hit.scrollTargets.empty()) {
            listBehind = true;
        }
    }
    REQUIRE(listBehind);

    auto state = f.InitialPlanningState();
    const auto plans = f.ProjectAll(
        *n, {f.PointerMove(730, onOverlay), f.Scroll(731, 0, -64)}, state);
    CHECK(plans[1].surfaceEligible);
    CHECK(plans[1].ScrollTargetCount() == 0);
}

// A5 (R1): 클릭은 같은 완전한 식별자 위에서 **잡힌 누름**을 요구한다.
TEST_CASE("a release over a button that was never pressed does not click") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    const auto high = f.HighButtonIdentity();
    const FixedPoint empty = PxPoint(300.0f, 300.0f);
    const FixedPoint onHigh = PxPoint(10.0f, 10.0f);
    for (const auto& hit : n->hitTargets) {
        REQUIRE_FALSE(HitRecordContains(hit, empty));
    }
    const auto dispatchAll = [&](const std::vector<PlannedUIEvent>& plans) {
        for (const auto& plan : plans) {
            (void)f.Router().HandleEvent(f.World(), *n, plan, f.Diagnostics());
        }
    };

    // (a) 빈 곳에서 누르고, 버튼 위로 옮겨, 버튼 위에서 놓는다.
    auto a = f.InitialPlanningState();
    const auto planA = f.ProjectAll(
        *n, {f.PointerDown(740, empty), f.PointerMove(741, onHigh),
             f.PointerUp(742, onHigh)},
        a);
    CHECK_FALSE(planA[0].targetFromSnapshotN);
    REQUIRE(planA[1].targetFromSnapshotN);
    CHECK(*planA[1].targetFromSnapshotN == high);  // hover는 따라간다
    CHECK_FALSE(planA[2].targetFromSnapshotN);     // 잡히지 않은 놓기
    CHECK(a.hovered == high);
    dispatchAll(planA);
    CHECK(f.HighButtonClicks() == 0);

    // (b) 누른 뒤 창 포커스를 잃고, 같은 자리에서 놓는다.
    auto b = f.InitialPlanningState();
    const auto planB = f.ProjectAll(
        *n, {f.PointerDown(750, onHigh), f.WindowFocus(751, false, kSurfaceWindow),
             f.PointerUp(752, onHigh)},
        b);
    CHECK_FALSE(planB[2].targetFromSnapshotN);
    dispatchAll(planB);
    CHECK(f.HighButtonClicks() == 0);

    // (c) 누른 뒤 포인터가 창을 떠났다가 돌아와서 놓는다.
    auto c = f.InitialPlanningState();
    const auto planC = f.ProjectAll(
        *n, {f.PointerDown(760, onHigh), f.PointerDeparture(761),
             f.PointerMove(762, onHigh), f.PointerUp(763, onHigh)},
        c);
    CHECK_FALSE(planC[3].targetFromSnapshotN);
    dispatchAll(planC);
    CHECK(f.HighButtonClicks() == 0);

    // 대조군: 같은 버튼 위에서 누르고 놓으면 정확히 한 번 클릭된다.
    auto d = f.InitialPlanningState();
    const auto planD = f.ProjectAll(
        *n, {f.PointerDown(770, onHigh), f.PointerUp(771, onHigh)}, d);
    REQUIRE(planD[1].targetFromSnapshotN);
    CHECK(*planD[1].targetFromSnapshotN == high);
    dispatchAll(planD);
    CHECK(f.HighButtonClicks() == 1);
}

// 캡처는 프레임을 건너 넘어올 수 있다(Task 12.3이 프레임마다 투사 상태를 다시
// 세운다). 그 사이에 스냅샷이 대상을 상호작용 불가라고 말하면 배달하지 않는다.
TEST_CASE("a captured press is not delivered where the current snapshot says non-interactable") {
    UIFrozenInputFixture f;
    // 이미지가 동작 컴포넌트이고 selectable이 상호작용 여부를 정하는 오브젝트.
    // 캡처 식별자(이미지)를 가진 기록이 N2에서도 남되 상호작용 불가가 된다 —
    // 이 모양이어야 캡처 경로의 상호작용 검사가 관찰된다.
    GameObject* canvas = f.World().FindById(kCanvasId);
    REQUIRE(canvas != nullptr);
    GameObject* tile = AddObject(f.World(), 30, canvas);
    AddOffsetRect(*tile, 500.0f, 400.0f, 50.0f, 50.0f);
    REQUIRE(tile->AddComponent<UIImage>() != nullptr);
    auto* selectable = tile->AddComponent<UISelectable>();
    REQUIRE(selectable != nullptr);
    const auto n1 = f.Rebuild();
    const FixedPoint onTile = PxPoint(510.0f, 410.0f);

    auto state = f.InitialPlanningState();
    const auto press =
        f.Router().PlanNext(*n1, f.SurfaceWindowId(), f.PointerDown(780, onTile), state);
    REQUIRE(press.targetFromSnapshotN.has_value());
    const auto captured = *press.targetFromSnapshotN;
    REQUIRE(state.pointerCapture == captured);

    // 대조군: 스냅샷이 여전히 상호작용 가능이라고 말하면 놓기가 그 대상에 간다.
    {
        auto copy = state;
        const auto n1b = f.Rebuild();
        const auto release = f.Router().PlanNext(
            *n1b, f.SurfaceWindowId(), f.PointerUp(781, onTile), copy);
        REQUIRE(release.targetFromSnapshotN.has_value());
        CHECK(*release.targetFromSnapshotN == captured);
    }

    selectable->SetInteractable(false);
    const auto n2 = f.Rebuild();
    const auto* record = FindHit(*n2, captured);
    REQUIRE(record != nullptr);
    REQUIRE_FALSE(record->interactable);
    const auto release =
        f.Router().PlanNext(*n2, f.SurfaceWindowId(), f.PointerUp(782, onTile), state);
    CHECK_FALSE(release.targetFromSnapshotN);
    CHECK_FALSE(state.pointerCapture.has_value());
}

// A6: 누산기는 계획을 가리킬 뿐이다. 임시 계획에 묶이거나 복사되면 안 된다.
static_assert(!std::is_constructible<UIEventDispatchAccumulator, PlannedUIEvent&&,
                                     molga::text::TextDiagnosticSink&>::value,
              "an accumulator must not bind to a temporary plan");
static_assert(!std::is_constructible<UIEventDispatchAccumulator,
                                     const PlannedUIEvent&&,
                                     molga::text::TextDiagnosticSink&>::value,
              "an accumulator must not bind to a const temporary plan");
static_assert(!std::is_copy_constructible<UIEventDispatchAccumulator>::value,
              "a copied accumulator would emit a second audit record");
static_assert(!std::is_copy_assignable<UIEventDispatchAccumulator>::value,
              "a copied accumulator would emit a second audit record");
// 대조군: 이름 있는 계획에는 여전히 묶인다.
static_assert(std::is_constructible<UIEventDispatchAccumulator, PlannedUIEvent&,
                                    molga::text::TextDiagnosticSink&>::value,
              "an accumulator must bind to a named plan");

TEST_CASE("a dispatch accumulator refuses a second Finish") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    const auto plan = f.Router().PlanNext(*n, f.SurfaceWindowId(),
                                          f.PointerDownAtOverlap(790), state);
    UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
    REQUIRE(accumulator.Merge(f.Router().HandleEvent(f.World(), *n, plan,
                                                     f.Diagnostics())) ==
            UIEventMergeStatus::Merged);
    const auto first = std::move(accumulator).Finish();
    REQUIRE(first.delivered);
    REQUIRE(first.runtimeTarget == f.HighButtonIdentity());
    const auto reportsBefore =
        f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.input");

    const auto second = std::move(accumulator).Finish();
    CHECK(second.sequence == first.sequence);
    CHECK(second.traceOrdinal == first.traceOrdinal);
    CHECK_FALSE(second.runtimeTarget.has_value());
    CHECK_FALSE(second.canonicalTarget.has_value());
    CHECK_FALSE(second.delivered);
    CHECK_FALSE(second.consumed);
    CHECK(second.actionMask == 0);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                    "ui.input") == reportsBefore + 1);
}

// A7 (R5): 기대 대상도 보고 대상도 없는 결과가 "둘 다 nullopt"라는 이유로
// 효과를 들여올 수는 없다.
TEST_CASE("a targetless handler result cannot smuggle effects into the audit") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();

    // 외부 창 기록: 어떤 효과도, dirty 플래그조차 들어올 수 없다.
    auto foreignState = f.InitialPlanningState();
    const auto foreign = f.Router().PlanNext(
        *n, f.SurfaceWindowId(),
        f.PointerDown(800, PxPoint(10.0f, 10.0f), kForeignWindow), foreignState);
    REQUIRE_FALSE(foreign.surfaceEligible);
    {
        UIEventDispatchAccumulator accumulator(foreign, f.Diagnostics());
        UIEventHandlerResult claim;
        claim.sequence = foreign.event.sequence;
        claim.stage = UIEventStage::Input;
        claim.consumed = true;
        claim.callbackDelivered = true;
        claim.actionMask = UIEventActionBit(UIEventAction::Click);
        CHECK(accumulator.Merge(claim) == UIEventMergeStatus::RejectedTargetless);
        UIEventHandlerResult dirt;
        dirt.sequence = foreign.event.sequence;
        dirt.stage = UIEventStage::Focus;
        dirt.visualDirty = true;
        CHECK(accumulator.Merge(dirt) == UIEventMergeStatus::RejectedTargetless);
        // 아무것도 주장하지 않는 결과는 해가 없다.
        UIEventHandlerResult noop;
        noop.sequence = foreign.event.sequence;
        CHECK(accumulator.Merge(noop) == UIEventMergeStatus::Merged);
        const auto record = std::move(accumulator).Finish();
        CHECK_FALSE(record.runtimeTarget.has_value());
        CHECK_FALSE(record.consumed);
        CHECK_FALSE(record.delivered);
        CHECK_FALSE(record.visualDirty);
        CHECK(record.actionMask == 0);
    }

    // 적격 포인터 계획이지만 텍스트/포커스/스크롤 대상이 없다(high 버튼).
    auto state = f.InitialPlanningState();
    const auto plan = f.Router().PlanNext(*n, f.SurfaceWindowId(),
                                          f.PointerDownAtOverlap(810), state);
    REQUIRE(plan.surfaceEligible);
    REQUIRE_FALSE(plan.TargetFor(UIEventStage::TextInput));
    REQUIRE_FALSE(plan.TargetFor(UIEventStage::Focus));
    REQUIRE(plan.ScrollTargetCount() == 0);
    UIEventDispatchAccumulator accumulator(plan, f.Diagnostics());
    UIEventHandlerResult edit;
    edit.sequence = plan.event.sequence;
    edit.stage = UIEventStage::TextInput;
    edit.actionMask = UIEventActionBit(UIEventAction::Edit);
    CHECK(accumulator.Merge(edit) == UIEventMergeStatus::RejectedTargetless);
    UIEventHandlerResult focus;
    focus.sequence = plan.event.sequence;
    focus.stage = UIEventStage::Focus;
    focus.resultingFocus = f.ComboSelectableIdentity();
    CHECK(accumulator.Merge(focus) == UIEventMergeStatus::RejectedTargetless);
    UIEventHandlerResult scroll;
    scroll.sequence = plan.event.sequence;
    scroll.stage = UIEventStage::Scroll;
    scroll.stageTargetOrdinal = 7;
    scroll.consumed = true;
    CHECK(accumulator.Merge(scroll) == UIEventMergeStatus::RejectedTargetless);
    // 대조군: 적격 계획에서 대상 없이 표면만 더럽히는 결과는 받아들인다
    // (Task 12.2의 포커스 해제가 그 모양이다). 이것이 없으면 "대상 없는 결과는
    // 전부 거절"하는 구현도 위의 단언을 통과한다.
    UIEventHandlerResult dirtOnly;
    dirtOnly.sequence = plan.event.sequence;
    dirtOnly.stage = UIEventStage::Focus;
    dirtOnly.visualDirty = true;
    CHECK(accumulator.Merge(dirtOnly) == UIEventMergeStatus::Merged);
    const auto record = std::move(accumulator).Finish();
    CHECK(record.runtimeTarget == f.HighButtonIdentity());
    CHECK(record.actionMask == 0);
    CHECK_FALSE(record.consumed);
    CHECK_FALSE(record.resultingFocus.has_value());
    CHECK(record.visualDirty);
}

// A9: 한 번도 계획되지 않던 경로들.
TEST_CASE("gamepad buttons and axes follow projected focus like keys") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    // 포커스는 combo에 세우고, 포인터는 high 위로 옮긴다. 포인터로 라우팅하는
    // 구현이라면 high를 고른다.
    const auto plans = f.ProjectAll(
        *n, {f.PointerDown(820, PxPoint(210.0f, 10.0f)),
             f.PointerUp(821, PxPoint(210.0f, 10.0f)),
             f.PointerMove(822, PxPoint(10.0f, 10.0f)),
             f.GamepadButton(823, 0), f.GamepadAxis(824, 1, -31)},
        state);
    REQUIRE(plans[2].targetFromSnapshotN);
    CHECK(*plans[2].targetFromSnapshotN == f.HighButtonIdentity());
    for (const std::size_t i : {std::size_t{3}, std::size_t{4}}) {
        CAPTURE(i);
        REQUIRE(plans[i].targetFromSnapshotN);
        CHECK(*plans[i].targetFromSnapshotN == f.ComboButtonIdentity());
        CHECK(*plans[i].targetFromSnapshotN != f.HighButtonIdentity());
        UIEventDispatchAccumulator accumulator(plans[i], f.Diagnostics());
        CHECK(accumulator.Merge(f.Router().HandleEvent(
                  f.World(), *n, plans[i], f.Diagnostics())) ==
              UIEventMergeStatus::Merged);
        const auto record = std::move(accumulator).Finish();
        CHECK(record.runtimeTarget == f.ComboButtonIdentity());
        CHECK(record.delivered);
    }
    // 부호 있는 축 값은 계획 안에서도 정확히 남는다.
    CHECK(plans[4].event.axisValue.Raw() == -31);

    // 포커스가 없으면 게임패드도 대상이 없다(포인터 위치로 떨어지지 않는다).
    auto noFocus = f.InitialPlanningState();
    const auto idle = f.ProjectAll(
        *n, {f.PointerMove(830, PxPoint(10.0f, 10.0f)), f.GamepadButton(831, 0),
             f.GamepadAxis(832, 1, 17)},
        noFocus);
    CHECK_FALSE(idle[1].targetFromSnapshotN);
    CHECK_FALSE(idle[2].targetFromSnapshotN);
}

TEST_CASE("a stamped TextEditing plan targets its ingest owner like a commit") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.FocusedPlanningState(*n);
    REQUIRE(state.focused != f.InputAIdentity());
    const auto editing = f.Router().PlanNext(
        *n, f.SurfaceWindowId(),
        f.TextEditing(840, "\xEA\xB0\x80",
                      UIFrozenInputFixture::RuntimeStamp(f.InputAIdentity(), 7),
                      0, 1),
        state);
    REQUIRE(editing.targetFromSnapshotN);
    CHECK(*editing.targetFromSnapshotN == f.InputAIdentity());
    REQUIRE(editing.TargetFor(UIEventStage::TextInput));
    CHECK(editing.TargetFor(UIEventStage::TextInput)->runtimeTarget ==
          f.InputAIdentity());
    UIEventDispatchAccumulator accumulator(editing, f.Diagnostics());
    CHECK(accumulator.Merge(f.Router().HandleEvent(f.World(), *n, editing,
                                                   f.Diagnostics())) ==
          UIEventMergeStatus::Merged);
    const auto record = std::move(accumulator).Finish();
    CHECK(record.runtimeTarget == f.InputAIdentity());
    CHECK(record.kind == UIInputEventKind::TextEditing);
    CHECK(record.delivered);

    const auto unstamped = f.Router().PlanNext(
        *n, f.SurfaceWindowId(),
        f.TextEditing(841, "x", TextInputOwnerStamp{}, 0, 1), state);
    CHECK_FALSE(unstamped.targetFromSnapshotN);
    CHECK_FALSE(unstamped.TargetFor(UIEventStage::TextInput));
}

TEST_CASE("a stamped text plan freezes its input's scroll chain") {
    UIFrozenInputFixture f;
    const auto n = f.BuildSnapshot();
    auto state = f.InitialPlanningState();
    // combo의 입력창은 안쪽/바깥쪽 스크롤 아래에 있다.
    const auto inScroll = f.Router().PlanNext(
        *n, f.SurfaceWindowId(),
        f.TextCommit(850, "x",
                     UIFrozenInputFixture::RuntimeStamp(f.ComboTextInputIdentity(), 3)),
        state);
    REQUIRE(inScroll.ScrollTargetCount() == 2);
    CHECK(inScroll.TargetFor(UIEventStage::Scroll, 0)->runtimeTarget ==
          f.ScrollIdentity(kInnerScrollId));
    CHECK(inScroll.TargetFor(UIEventStage::Scroll, 1)->runtimeTarget ==
          f.ScrollIdentity(kOuterScrollId));
    // 대조군: 스크롤 밖의 입력창(inputA)은 빈 사슬이다.
    const auto outside = f.Router().PlanNext(
        *n, f.SurfaceWindowId(),
        f.TextCommit(851, "x",
                     UIFrozenInputFixture::RuntimeStamp(f.InputAIdentity(), 3)),
        state);
    REQUIRE(outside.targetFromSnapshotN);
    CHECK(outside.ScrollTargetCount() == 0);
}
