#include "Assets/FontArtifactStore.h"
#include "Common/Fixed26_6.h"
#include "Core/AssetDatabase.h"
#include "Core/SceneSerializer.h"
#include "ECS/Component.h"
#include "ECS/Components/TextRenderer2D.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/TextRenderer.h"
#include "Rendering/WorldRenderTraversal.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutService.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextRuntimeDependencies.h"
#include "TextQualificationAssetTree.h"
#include "TextRuntimeTestSession.h"
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace text = molga::text;
using molga::Fixed26_6;
using molga::FixedPoint;
using molga::FixedRect;
using molga::FixedSize;
using text::TextDiagnosticCode;

namespace fs = std::filesystem;

// ── 픽스처 트리의 고정 GUID ─────────────────────────────────────────────────
// Task 4.2가 커밋한 자격 트리의 계약이다. 이름이 아니라 이 상수들이 계약이므로
// 케이스가 자기 자리에서 GUID를 적지 않는다.
constexpr const char* kPrimaryFamilyGuid = "11111111111111111111111111111111";
// primary family가 처음으로 지목하는 face. hot reload 케이스가 이 파일의
// 바이트만 갈아 끼운다 — .meta를 건드리지 않으므로 GUID와 저작된 family 순서는
// 그대로이고, 달라지는 것은 내용 주소뿐이다.
constexpr const char* kPrimaryFirstFaceGuid = "44444444444444444444444444444444";
constexpr const char* kPrimaryFirstFaceSource = "fonts/NotoSans-Regular.ttf";
// 자격 트리에 없는 family. resolver는 후보 0개를 돌려주므로 "face가 하나도 없는
// 문단"의 유일한 실제 경로다.
constexpr const char* kMissingFamily = "missing-family";

std::string Label(const char* value) { return std::string(value); }

std::vector<std::uint8_t> ReadAllBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE_MESSAGE(input.good(), path.string());
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

// ── 진단 조회 ────────────────────────────────────────────────────────────────
bool HasDiagnostic(const std::vector<text::TextDiagnostic>& diagnostics,
                   TextDiagnosticCode code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const text::TextDiagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

bool HasDiagnostic(const text::VectorTextDiagnosticSink& sink,
                   TextDiagnosticCode code) {
    return HasDiagnostic(sink.Diagnostics(), code);
}

// ── 명령이 어느 텍스처를 들고 있는가 ────────────────────────────────────────
// 레거시 ASCII 비트맵은 atlas page가 아닌 텍스처를 명령에 실었다. 이관 이후
// 그런 명령은 하나도 없어야 한다: 유효한 텍스처를 든 명령은 반드시 자기 page의
// 정체성도 함께 든다. 텍스처 정체성 대신 이 구조로 판정하는 이유는 지워진
// 텍스처를 이름으로 물을 수 없기 때문이다.
bool CommandUsesNonAtlasTexture(const molga::RenderCommand& command) {
    const bool hasTexture = static_cast<bool>(command.batchKey.texture) ||
                            command.batchKey.textureStableId != 0U;
    return hasTexture && command.resourceLifetimeIdentity == 0U;
}

// 없는 glyph의 명령은 유효하지 않은 핸들을 그대로 들고 나가고, 그 핸들을
// renderer 소유의 흰색으로 묶는 것은 sprite 경로다(Step 5c). 그래야 atlas 예산이
// 0이어도 grapheme마다 그릴 수 있는 명령이 하나씩 남는다.
bool AllUseInvalidTextureHandleWhiteFallback(
    const std::vector<molga::RenderCommand>& commands) {
    if (commands.empty()) return false;
    return std::all_of(commands.begin(), commands.end(),
                       [](const molga::RenderCommand& command) {
                           return !static_cast<bool>(command.batchKey.texture) &&
                                  command.batchKey.textureStableId == 0U &&
                                  command.isBatchableSprite;
                       });
}

// ── 비교 가능한 표준형 ───────────────────────────────────────────────────────
// UI와 월드는 서로 다른 출력 affine을 쓰므로, "하나의 셰이핑 결과를 나눠 쓴다"는
// 주장은 명령 좌표가 아니라 배치가 낸 glyph 기록으로만 확인된다.
struct CanonicalGlyphRecord {
    std::string fontGuid;
    std::string fontRevision;
    std::uint32_t faceIndex = 0;
    std::uint32_t glyphId = 0;
    bool missing = false;
    std::int32_t fontSizeRaw = 0;
    std::int32_t advanceXRaw = 0;
    std::int32_t advanceYRaw = 0;
    std::int32_t offsetXRaw = 0;
    std::int32_t offsetYRaw = 0;
    std::int32_t originXRaw = 0;
    std::int32_t originYRaw = 0;
    std::uint32_t sourceBegin = 0;
    std::uint32_t sourceEnd = 0;
    std::uint32_t graphemeBegin = 0;
    std::uint32_t graphemeEnd = 0;
    std::uint8_t bidiLevel = 0;

    bool operator==(const CanonicalGlyphRecord& other) const {
        return fontGuid == other.fontGuid &&
               fontRevision == other.fontRevision &&
               faceIndex == other.faceIndex && glyphId == other.glyphId &&
               missing == other.missing && fontSizeRaw == other.fontSizeRaw &&
               advanceXRaw == other.advanceXRaw &&
               advanceYRaw == other.advanceYRaw &&
               offsetXRaw == other.offsetXRaw &&
               offsetYRaw == other.offsetYRaw &&
               originXRaw == other.originXRaw &&
               originYRaw == other.originYRaw &&
               sourceBegin == other.sourceBegin && sourceEnd == other.sourceEnd &&
               graphemeBegin == other.graphemeBegin &&
               graphemeEnd == other.graphemeEnd && bidiLevel == other.bidiLevel;
    }
};

// 명령 하나의 네 꼭짓점을 26.6 raw로 되돌린 값. UI 문맥은 배율이 1이고
// 평행이동만 있으므로 이 왕복은 정확하다.
struct CanonicalQuad {
    std::array<std::int32_t, 4> x{};
    std::array<std::int32_t, 4> y{};
    bool operator==(const CanonicalQuad& other) const {
        return x == other.x && y == other.y;
    }
};

struct CanonicalRect {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const CanonicalRect& other) const {
        return x == other.x && y == other.y && width == other.width &&
               height == other.height;
    }
};

std::int32_t ToRaw26_6(float value) {
    return static_cast<std::int32_t>(std::lround(value * 64.0f));
}

// ── 한 번의 수집 관찰 ────────────────────────────────────────────────────────
class CollectedDraw {
public:
    CollectedDraw() = default;
    CollectedDraw(std::shared_ptr<const text::TextLayout> layout,
                  std::vector<molga::RenderCommand> commands)
        : layout_(std::move(layout)), commands_(std::move(commands)) {}

    const text::TextLayout& Layout() const {
        REQUIRE(layout_ != nullptr);
        return *layout_;
    }
    const std::vector<molga::RenderCommand>& TextCommands() const {
        return commands_;
    }
    std::shared_ptr<const text::TextLayout> LayoutShare() const {
        return layout_;
    }

    std::vector<CanonicalGlyphRecord> CanonicalGlyphRecords() const {
        std::vector<CanonicalGlyphRecord> records;
        if (!layout_) return records;
        for (const text::TextLine& line : layout_->lines) {
            for (const text::VisualRun& run : line.visualRuns) {
                for (const text::PositionedGlyph& positioned : run.glyphs) {
                    const text::ShapedGlyph& glyph = positioned.glyph;
                    CanonicalGlyphRecord record;
                    record.fontGuid = glyph.fontGuid;
                    record.fontRevision = glyph.fontRevision;
                    record.faceIndex = glyph.faceIndex;
                    record.glyphId = glyph.glyphId;
                    record.missing = glyph.missing;
                    record.fontSizeRaw = glyph.fontSize.Raw();
                    record.advanceXRaw = glyph.advanceX.Raw();
                    record.advanceYRaw = glyph.advanceY.Raw();
                    record.offsetXRaw = glyph.offsetX.Raw();
                    record.offsetYRaw = glyph.offsetY.Raw();
                    record.originXRaw = positioned.origin.x.Raw();
                    record.originYRaw = positioned.origin.y.Raw();
                    record.sourceBegin = glyph.sourceBytes.begin;
                    record.sourceEnd = glyph.sourceBytes.end;
                    record.graphemeBegin = glyph.graphemes.begin;
                    record.graphemeEnd = glyph.graphemes.end;
                    record.bidiLevel = glyph.bidiLevel;
                    records.push_back(std::move(record));
                }
            }
        }
        return records;
    }

    std::vector<CanonicalQuad> CanonicalVertices() const {
        std::vector<CanonicalQuad> quads;
        quads.reserve(commands_.size());
        for (const molga::RenderCommand& command : commands_) {
            CanonicalQuad quad;
            for (std::size_t index = 0; index < 4U; ++index) {
                quad.x[index] = ToRaw26_6(command.vertices[index].x);
                quad.y[index] = ToRaw26_6(command.vertices[index].y);
            }
            quads.push_back(quad);
        }
        return quads;
    }

    std::vector<CanonicalRect> CanonicalTofuRects() const {
        std::vector<CanonicalRect> rects;
        rects.reserve(commands_.size());
        for (const molga::RenderCommand& command : commands_) {
            float minX = command.vertices[0].x, maxX = command.vertices[0].x;
            float minY = command.vertices[0].y, maxY = command.vertices[0].y;
            for (std::size_t index = 1; index < 4U; ++index) {
                minX = std::min(minX, command.vertices[index].x);
                maxX = std::max(maxX, command.vertices[index].x);
                minY = std::min(minY, command.vertices[index].y);
                maxY = std::max(maxY, command.vertices[index].y);
            }
            rects.push_back({ToRaw26_6(minX), ToRaw26_6(minY),
                             ToRaw26_6(maxX) - ToRaw26_6(minX),
                             ToRaw26_6(maxY) - ToRaw26_6(minY)});
        }
        return rects;
    }

    // 레거시 ASCII 비트맵을 썼는가. 지워진 텍스처를 이름으로 물을 수 없으므로
    // 구조로 판정한다 — atlas page 지분 없이 텍스처를 든 명령이 하나라도 있는가.
    bool UsedBuiltinAsciiTexture() const {
        return std::any_of(commands_.begin(), commands_.end(),
                           CommandUsesNonAtlasTexture);
    }

private:
    std::shared_ptr<const text::TextLayout> layout_;
    std::vector<molga::RenderCommand> commands_;
};

// Step 5a의 tofu 기하를 배치 지표에서 독립적으로 다시 계산한다. 프로덕션이
// 계산한 값과 여기서 계산한 값이 같아야 하며, 한쪽을 다른 쪽에서 읽어 오지
// 않는다.
std::vector<CanonicalRect> TofuRectsFromLayoutMetrics(
    const text::TextLayout& layout) {
    std::vector<CanonicalRect> rects;
    for (const text::TextLine& line : layout.lines) {
        for (const text::VisualRun& run : line.visualRuns) {
            for (const text::PositionedGlyph& positioned : run.glyphs) {
                const std::int32_t advance =
                    std::abs(positioned.glyph.advanceX.Raw());
                const std::int32_t width = std::max(advance, 1);
                rects.push_back(
                    {positioned.origin.x.Raw(),
                     line.baseline.Raw() - line.ascent.Raw(), width,
                     line.ascent.Raw() + line.descent.Raw()});
            }
        }
    }
    return rects;
}

const text::ShapedGlyph& FirstGlyph(const text::TextLayout& layout) {
    REQUIRE_FALSE(layout.lines.empty());
    REQUIRE_FALSE(layout.lines.front().visualRuns.empty());
    REQUIRE_FALSE(layout.lines.front().visualRuns.front().glyphs.empty());
    return layout.lines.front().visualRuns.front().glyphs.front().glyph;
}

// 픽스처의 월드 오브젝트. GameObject는 Transform을 자동으로 붙이지 않으므로
// 순서를 여기 한 곳에 적어 둔다.
std::shared_ptr<GameObject> MakeWorldTextObject() {
    auto object = std::make_shared<GameObject>("WorldText");
    object->AddComponent<Transform>();
    object->AddComponent<TextRenderer2D>();
    return object;
}

// ── 픽스처가 세는 큐 ─────────────────────────────────────────────────────────
// 이 큐에는 텍스트 수집만 들어간다. 그래서 명령 수가 곧 텍스트 명령 수다 —
// 이관 전의 즉시 그리기 우회로가 여기 무엇이든 흘리면 그 순간 0이 아니게 된다.
class ObservedRenderQueue {
public:
    operator molga::RenderQueue&() noexcept { return queue_; }
    molga::RenderQueue& Raw() noexcept { return queue_; }
    const molga::RenderQueue& Raw() const noexcept { return queue_; }
    std::size_t TextCommandCount() const {
        return queue_.GetCommands().size();
    }
    void Clear() { queue_.Clear(); }

private:
    molga::RenderQueue queue_;
};

// ── 즉시 그리기 우회로의 대역 ────────────────────────────────────────────────
// TextRenderer2D::RenderSprite가 지워졌는지는 "아무것도 그리지 않았다"로만
// 관찰된다. Component::RenderSprite는 Renderer*를 받으므로 대역도 Renderer여야
// 하고, Init을 부르지 않은 Renderer는 GPU 장치를 잡지 않는다 — 이 프로세스에는
// 장치가 없다. 재정의가 사라진 지금 기본 구현은 이 포인터를 역참조조차 하지
// 않으므로 draw call 계수기는 0에서 움직이지 않아야 하고, 움직이는 순간
// 즉시 그리기 경로가 되살아난 것이다.
class ImmediateDrawCounter : public Renderer {
public:
    std::size_t DrawCallCount() noexcept {
        return static_cast<std::size_t>(Stats().drawCalls);
    }
};

// Transform의 월드 접근자에 계획서가 쓰는 이름을 붙인 얇은 어댑터. Transform은
// 이 태스크의 파일 목록 밖이므로 그쪽 API를 고치지 않는다.
class WorldTransformView {
public:
    explicit WorldTransformView(Transform& transform) : transform_(&transform) {}

    void SetWorldScale(const Vector2& scale) {
        REQUIRE(transform_->TrySetWorldScale(scale));
    }
    void SetWorldRotationDegrees(float degrees) {
        transform_->SetWorldRotation(degrees);
    }
    void SetWorldPosition(const Vector2& position) {
        transform_->SetWorldPosition(position);
    }

private:
    Transform* transform_ = nullptr;
};

// FontRepository가 실제로 바이트를 연 횟수를 읽는 창. 수집이 GUID를 다시 열지
// 않는다는 주장은 이 계수기로만 관찰된다.
class RepositoryLoadWindow {
public:
    std::uint64_t ByteLoadCount() const noexcept {
        return text::detail::FontRepositoryByteLoadCount();
    }
};

// ── 공유 소비자 픽스처 ───────────────────────────────────────────────────────
// 하나의 프로세스 텍스트 런타임 세션 위에, UI와 월드가 실제로 쓰는 그 경로를
// 세운다. AssetDatabase는 픽스처 소유이고 쓸 수 있다 — hot reload 케이스가
// 폰트 바이트를 갈아 끼우고 데이터베이스로만 다시 스캔한다.
class SharedTextConsumerFixture {
private:
    // 선언 순서가 곧 초기화 순서다. 트리와 store는 데이터베이스 바인딩보다
    // 먼저 서야 하므로 여기 맨 앞에 둔다.
    QualificationAssetTreeFixture tree_;
    std::shared_ptr<const molga::FontArtifactStore> store_;

public:
    SharedTextConsumerFixture()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))),
          glyphAtlas(renderer.GlyphAtlas()),
          worldObject(MakeWorldTextObject()),
          worldText(*worldObject->GetComponent<TextRenderer2D>()),
          worldTransform(*worldObject->GetComponent<Transform>()) {
        std::string bindError;
        REQUIRE_MESSAGE(database.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database.ScanProject(tree_.AssetsRoot());
        REQUIRE(database.Find(std::string(kPrimaryFamilyGuid)) != nullptr);
        REQUIRE(renderer.Init(database, initSink));
        worldText.SetFontFamilyGuid(kPrimaryFamilyGuid);
        worldText.SetFontSizePx(16.0f);
        worldText.SetComponentScale(1.0f);
        worldObjects.push_back(worldObject);
    }

    // ── 컨텍스트 ─────────────────────────────────────────────────────────
    TextCollectContext UiCollectContext() const {
        TextCollectContext context;
        context.color = Color::White();
        context.cameraPass = 1;
        context.sortingLayer = 0;
        context.sortingOrder = 1;
        return context;
    }

    TextCollectContext WorldCollectContext() const {
        TextCollectContext context;
        context.color = Color::White();
        context.cameraPass = 0;
        context.sortingLayer = 3;
        context.sortingOrder = 7;
        context.depthOrYSort = 2.5f;
        return context;
    }

    WorldRenderCollectionContext WorldCollectionContext() {
        WorldRenderCollectionContext context;
        context.textRenderer = &renderer;
        context.textDiagnostics = &sink;
        return context;
    }

    // ── 요청 ─────────────────────────────────────────────────────────────
    text::TextLayoutRequest LabelRequest(const std::string& utf8,
                                         const std::string& familyGuid) const {
        text::TextLayoutRequest request;
        request.utf8 = utf8;
        request.style.fontFamilyGuid = familyGuid;
        request.style.shape.fontSize = Fixed26_6::FromRaw(16 * 64);
        request.style.shape.language = "und";
        request.style.analysis.locale = "und";
        request.diagnosticContext.componentType = "UILabel";
        return request;
    }

    std::optional<std::shared_ptr<const text::TextLayout>> LayoutLabel(
        const std::string& familyName, const std::string& utf8) {
        const std::string familyGuid = FamilyGuid(familyName);
        auto layout = LayoutFor(LabelRequest(utf8, familyGuid), utf8);
        if (layout) RegisterResources(**layout);
        return layout;
    }

    std::shared_ptr<const text::TextLayout> ValidLayout() {
        auto layout = LayoutLabel("family-a", u8"Aa");
        REQUIRE(layout.has_value());
        return *layout;
    }

    // ── 수집 ─────────────────────────────────────────────────────────────
    CollectedDraw CollectLabelWithFamily(const std::string& utf8,
                                         const std::string& familyName) {
        return CollectWith(LabelRequest(utf8, FamilyGuid(familyName)), utf8,
                           UiCollectContext());
    }

    CollectedDraw CollectLabel(const std::string& utf8) {
        return CollectLabelWithFamily(utf8, "family-a");
    }

    CollectedDraw CollectWorldText(const std::string& utf8) {
        text::TextLayoutRequest request =
            LabelRequest(utf8, std::string(kPrimaryFamilyGuid));
        request.diagnosticContext.componentType = "TextRenderer2D";
        // 월드 텍스트는 폭/높이 제약을 만들지 않는다(설계 7.3).
        request.style.wrap = text::TextWrapMode::NoWrap;
        request.style.overflow = text::TextOverflowMode::Overflow;
        return CollectWith(request, utf8, WorldCollectContext());
    }

    CollectedDraw CollectLabelWithAffine(FixedPoint origin) {
        TextCollectContext context = UiCollectContext();
        context.layoutToOutput.tx = origin.x.ToFloat();
        context.layoutToOutput.ty = origin.y.ToFloat();
        return CollectWith(LabelRequest(u8"Ag", std::string(kPrimaryFamilyGuid)),
                           u8"Ag", context);
    }

    // 같은 배치를 원점에서 수집한 뒤 평행이동을 픽스처가 직접 더한다. affine이
    // tx/ty를 실제로 적용하지 않으면 두 결과가 갈린다.
    CollectedDraw CollectLabelReferenceTranslation(FixedPoint origin) {
        CollectedDraw base = CollectWith(
            LabelRequest(u8"Ag", std::string(kPrimaryFamilyGuid)), u8"Ag",
            UiCollectContext());
        std::vector<molga::RenderCommand> shifted = base.TextCommands();
        for (molga::RenderCommand& command : shifted) {
            for (molga::Vertex2D& vertex : command.vertices) {
                vertex.x += origin.x.ToFloat();
                vertex.y += origin.y.ToFloat();
            }
        }
        return CollectedDraw(
            std::shared_ptr<const text::TextLayout>(base.LayoutShare()),
            std::move(shifted));
    }

    // 논리 사각형 하나만 담은 배치를 월드 컴포넌트의 affine으로 변환한 결과.
    //
    // 합성 배치를 쓰는 이유는 하나다: 확인하려는 것이 affine이므로, 사각형이
    // 폰트마다 달라지면 기대값이 폰트의 성질이 된다. affine 자체와 네 꼭짓점
    // 변환은 프로덕션 코드 그대로다 — 이 헬퍼는 그 둘을 부를 뿐이다.
    std::array<Vector2, 4> CollectOneLogicalQuad(TextRenderer2D& component,
                                                 FixedRect logical) {
        const auto context =
            component.BuildWorldTextContext(WorldCollectionContext(), sink);
        REQUIRE(context.has_value());
        auto layout = std::make_shared<text::TextLayout>();
        text::TextLine line;
        // Step 5a의 tofu 기하가 이 사각형을 그대로 내도록 지표를 고른다:
        // {origin.x, baseline-ascent, |advanceX|, ascent+descent}.
        line.baseline = Fixed26_6::FromRaw(logical.y.Raw() + logical.height.Raw());
        line.ascent = logical.height;
        line.descent = Fixed26_6::FromRaw(0);
        text::VisualRun run;
        text::PositionedGlyph positioned;
        positioned.glyph.missing = true;
        positioned.glyph.advanceX = logical.width;
        positioned.glyph.fontSize = Fixed26_6::FromRaw(16 * 64);
        positioned.origin = FixedPoint{logical.x, line.baseline};
        run.glyphs.push_back(std::move(positioned));
        line.visualRuns.push_back(std::move(run));
        layout->lines.push_back(std::move(line));

        const std::size_t before = queue.Raw().GetCommands().size();
        {
            auto scope = renderer.BeginGlyphCollection(++frameIndex_);
            renderer.CollectLayout(queue, *layout, *context, sink);
        }
        REQUIRE(queue.Raw().GetCommands().size() == before + 1U);
        const molga::RenderCommand& command = queue.Raw().GetCommands().back();
        return {Vector2(command.vertices[0].x, command.vertices[0].y),
                Vector2(command.vertices[1].x, command.vertices[1].y),
                Vector2(command.vertices[2].x, command.vertices[2].y),
                Vector2(command.vertices[3].x, command.vertices[3].y)};
    }

    AABB LastCommandWorldBounds() const {
        REQUIRE_FALSE(queue.Raw().GetCommands().empty());
        const auto& bounds = queue.Raw().GetCommands().back().worldBounds;
        REQUIRE(bounds.has_value());
        return *bounds;
    }

    // ── 관찰 ─────────────────────────────────────────────────────────────
    const std::vector<text::TextDiagnostic>& Diagnostics() const {
        return sink.Diagnostics();
    }

    // 이 원문이 실제로 문단 셰이핑을 거친 횟수. 두 소비자가 하나의 결과를
    // 나눠 쓰면 첫 번째만 1이고 두 번째는 캐시 적중으로 0이다.
    std::size_t ShapeCountForText(const std::string& utf8) const {
        const auto found = shapePasses_.find(utf8);
        return found == shapePasses_.end() ? 0U : found->second;
    }

    std::size_t RasterizationCountForSourceSha(const std::string& sha) const {
        const auto found = resourcesBySha_.find(sha);
        if (found == resourcesBySha_.end()) return 0U;
        if (!found->second->rasterFace) return 0U;
        return static_cast<std::size_t>(
            found->second->rasterFace->RasterizeCallCountForTest());
    }

    // ── hot reload ───────────────────────────────────────────────────────
    // 폰트 원본 바이트만 갈아 끼우고 데이터베이스로 다시 가져온다. 렌더러에게
    // 직접 알리는 통로는 없다 — 새 세대는 resolver/layout/resource 캐시 정체성의
    // 일부이므로 다음 요청이 스스로 갈린다.
    void ReplaceFamilyFaceWithVerifiedBytes(const std::string& familyName,
                                            const fs::path& replacement) {
        REQUIRE(familyName == "family-a");
        const fs::path target = tree_.AssetsRoot() / kPrimaryFirstFaceSource;
        const std::vector<std::uint8_t> bytes = ReadAllBytes(replacement);
        {
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            REQUIRE(output.good());
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        std::string error;
        REQUIRE_MESSAGE(
            database.TryReimport(std::string(kPrimaryFirstFaceGuid), &error),
            error);
    }

    std::string FamilyGuid(const std::string& familyName) const {
        if (familyName == "family-a") return std::string(kPrimaryFamilyGuid);
        return familyName;
    }

    // ── 상태 ─────────────────────────────────────────────────────────────
    molga::AssetDatabase database;
    text::VectorTextDiagnosticSink initSink;
    text::VectorTextDiagnosticSink sink;
    ObservedRenderQueue queue;
    TextRenderer renderer;
    molga::GlyphAtlasCache& glyphAtlas;
    RepositoryLoadWindow repository;
    ImmediateDrawCounter immediateRenderer;
    std::shared_ptr<GameObject> worldObject;
    std::vector<std::shared_ptr<GameObject>> worldObjects;
    TextRenderer2D& worldText;
    WorldTransformView worldTransform;

    CollectedDraw CollectWith(const text::TextLayoutRequest& request,
                              const std::string& utf8,
                              const TextCollectContext& context) {
        auto layout = LayoutFor(request, utf8);
        REQUIRE(layout.has_value());
        RegisterResources(**layout);
        const std::size_t before = queue.Raw().GetCommands().size();
        {
            auto scope = renderer.BeginGlyphCollection(++frameIndex_);
            renderer.CollectLayout(queue, **layout, context, sink);
        }
        const auto& commands = queue.Raw().GetCommands();
        std::vector<molga::RenderCommand> produced(
            commands.begin() + static_cast<std::ptrdiff_t>(before),
            commands.end());
        return CollectedDraw(*layout, std::move(produced));
    }

    std::optional<std::shared_ptr<const text::TextLayout>> LayoutFor(
        const text::TextLayoutRequest& request, const std::string& utf8) {
        auto layout = renderer.Layout(request, sink);
        shapePasses_[utf8] +=
            text::detail::CurrentLayoutObservations().paragraphShapePasses;
        return layout;
    }

private:
    void RegisterResources(const text::TextLayout& layout) {
        for (const text::TextLine& line : layout.lines) {
            for (const text::VisualRun& run : line.visualRuns) {
                for (const text::PositionedGlyph& positioned : run.glyphs) {
                    const auto& resource = positioned.glyph.faceResource;
                    if (!resource) continue;
                    resourcesBySha_[resource->sourceSha256] = resource;
                }
            }
        }
    }

    std::unordered_map<std::string, std::size_t> shapePasses_;
    std::unordered_map<std::string, text::FontFaceResourcePtr> resourcesBySha_;
    std::uint64_t frameIndex_ = 0;
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Step 0: 이 실행 파일 뒤에 텍스트 런타임 세션이 정확히 하나 서 있는가
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("exactly one text runtime session backs the shared consumer fixture") {
    // molga_add_text_test가 세운 세션이다. 두 번째 수명은 만들어질 수 없다 —
    // ICU 수명은 종결적이므로, 픽스처가 자기 몫의 런타임을 몰래 세우면
    // 아래 Create가 성공하거나 이 프로세스가 이미 깨져 있다.
    CHECK(text::TextRuntimeDependencies::Get().IsReady());
    CHECK_FALSE(text::TextRuntimeDependencies::Get().WasTerminallyCleaned());
    text::VectorTextDiagnosticSink secondSink;
    const auto second = text::TextRuntimeLifetimeGuard::Create(
        text::TextDependencyConfig::FromEngineTextRoot(
            TextRuntimeTestSession::Current().EngineTextRoot(),
            /*packagedRuntime=*/false),
        secondSink);
    CHECK_FALSE(second.has_value());
    CHECK(HasDiagnostic(secondSink, TextDiagnosticCode::DependencyInvalid));

    // 그리고 그 하나의 세션 위에서 공유 서비스가 실제로 선다.
    SharedTextConsumerFixture fixture;
    CHECK(text::TextRuntimeDependencies::Get().IsReady());
    CHECK(fixture.LayoutLabel("family-a", u8"A").has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 1: UI와 월드가 하나의 셰이핑 결과를 나눠 쓴다
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UI and world consumers share one shaping result") {
    SharedTextConsumerFixture f;
    const auto ui = f.CollectLabel(u8"سلام हिन्दी");
    const auto world = f.CollectWorldText(u8"سلام हिन्दी");
    CHECK(ui.CanonicalGlyphRecords() == world.CanonicalGlyphRecords());
    CHECK(f.ShapeCountForText(u8"سلام हिन्दी") == 1);
}

// 위 케이스의 성공 증인. 기록이 비어 있으면 두 빈 벡터가 같으므로, "하나를
// 나눠 쓴다"가 "아무것도 셰이핑하지 않았다"로 조용히 참이 된다.
TEST_CASE("the shared shaping result is not empty") {
    SharedTextConsumerFixture f;
    const auto ui = f.CollectLabel(u8"سلام हिन्दी");
    CHECK(ui.CanonicalGlyphRecords().size() > 3U);
    CHECK_FALSE(ui.TextCommands().empty());
    bool sawArabic = false;
    bool sawDevanagari = false;
    for (const auto& record : ui.CanonicalGlyphRecords()) {
        CHECK_FALSE(record.missing);
        // 두 script는 서로 다른 face에서 온다: 하나의 face가 둘 다 그리면
        // fallback을 통째로 지운 회귀가 이 케이스를 통과한다.
        if (record.fontGuid == "66666666666666666666666666666666") sawArabic = true;
        if (record.fontGuid == "12121212121212121212121212121212") sawDevanagari = true;
    }
    CHECK(sawArabic);
    CHECK(sawDevanagari);
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 1a: hot reload가 옛 배치 밑에서 폰트를 다시 열지 않는다
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("old cached layout never reopens a font after hot reload") {
    SharedTextConsumerFixture f;
    const auto oldLayout = f.LayoutLabel("family-a", u8"ffi");
    REQUIRE(oldLayout);
    const std::string oldSha = FirstGlyph(**oldLayout).faceResource->sourceSha256;
    f.ReplaceFamilyFaceWithVerifiedBytes(
        "family-a", MOLGA_TEXT_INTER_FONT);
    const auto newLayout = f.LayoutLabel("family-a", u8"ffi");
    REQUIRE(newLayout);
    const std::string newSha = FirstGlyph(**newLayout).faceResource->sourceSha256;
    REQUIRE(newSha != oldSha);
    const auto loadsBeforeCollection = f.repository.ByteLoadCount();
    {
        auto scope = f.renderer.BeginGlyphCollection(71);
        f.renderer.CollectLayout(
            f.queue, **oldLayout, f.UiCollectContext(), f.sink);
        f.renderer.CollectLayout(
            f.queue, **newLayout, f.WorldCollectContext(), f.sink);
    }
    CHECK(f.repository.ByteLoadCount() == loadsBeforeCollection);
    CHECK(f.RasterizationCountForSourceSha(oldSha) > 0);
    CHECK(f.RasterizationCountForSourceSha(newSha) > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 1b: 없는 family는 atlas에 닿지 않고 결정적인 두부를 낸다
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("missing family renders deterministic tofu without atlas access") {
    SharedTextConsumerFixture f;
    f.glyphAtlas.SetResidentBudget(0);
    const auto draw = f.CollectLabelWithFamily(u8"A👩‍🚀", "missing-family");
    REQUIRE(draw.TextCommands().size() == 2); // two extended graphemes
    CHECK(draw.CanonicalTofuRects() ==
          TofuRectsFromLayoutMetrics(draw.Layout()));
    CHECK(AllUseInvalidTextureHandleWhiteFallback(draw.TextCommands()));
    CHECK(f.glyphAtlas.LookupCountForTest() == 0);
    CHECK(f.glyphAtlas.Telemetry().uploads == 0);
    CHECK(HasDiagnostic(f.Diagnostics(),
          molga::text::TextDiagnosticCode::MissingGlyph));
    CHECK_FALSE(draw.UsedBuiltinAsciiTexture());
}

// 위 두 술어의 성공 증인. 둘 다 "언제나 참/거짓"으로 stub될 수 있으므로,
// 손으로 만든 명령으로 반대쪽을 요구한다.
TEST_CASE("texture-provenance predicates discriminate both ways") {
    molga::RenderCommand atlasCommand;
    atlasCommand.isBatchableSprite = true;
    atlasCommand.batchKey.textureStableId = 7U;
    atlasCommand.resourceLifetimeIdentity = 5U;
    molga::RenderCommand strayCommand;
    strayCommand.isBatchableSprite = true;
    strayCommand.batchKey.textureStableId = 7U;
    strayCommand.resourceLifetimeIdentity = 0U;
    molga::RenderCommand tofuCommand;
    tofuCommand.isBatchableSprite = true;

    CHECK_FALSE(CommandUsesNonAtlasTexture(atlasCommand));
    CHECK(CommandUsesNonAtlasTexture(strayCommand));
    CHECK_FALSE(CommandUsesNonAtlasTexture(tofuCommand));

    CHECK(AllUseInvalidTextureHandleWhiteFallback({tofuCommand, tofuCommand}));
    CHECK_FALSE(AllUseInvalidTextureHandleWhiteFallback({tofuCommand, strayCommand}));
    // 빈 목록은 "전부 흰색 대체"가 아니라 "그릴 것이 하나도 없다"이다.
    CHECK_FALSE(AllUseInvalidTextureHandleWhiteFallback({}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 1c: 즉시 그리기 진입점이 사라졌다
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRenderer2D has no immediate RenderSprite text path") {
    SharedTextConsumerFixture f;
    Component* component = &f.worldText;
    component->RenderSprite(&f.immediateRenderer);
    component->CollectRender(f.queue);
    CHECK(f.immediateRenderer.DrawCallCount() == 0);
    CHECK(f.queue.TextCommandCount() == 0);
    component->CollectRender(f.queue, f.WorldCollectionContext());
    CHECK(f.queue.TextCommandCount() > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 1d/1e/1f: 출력 affine
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("world text applies nonuniform negative scale then rotation") {
    SharedTextConsumerFixture f;
    f.worldText.SetComponentScale(1.0f);
    f.worldTransform.SetWorldScale({-2.0f, 3.0f});
    f.worldTransform.SetWorldRotationDegrees(90.0f);
    f.worldTransform.SetWorldPosition({10.0f, 20.0f});
    const auto vertices = f.CollectOneLogicalQuad(
        f.worldText, FixedRect{Fixed26_6::FromRaw(0),
                               Fixed26_6::FromRaw(0),
                               Fixed26_6::FromRaw(2 * 64),
                               Fixed26_6::FromRaw(1 * 64)});
    CHECK(vertices[0] == Vector2(10.0f, 20.0f));
    CHECK(vertices[1] == Vector2(10.0f, 16.0f));
    CHECK(vertices[2] == Vector2(7.0f, 16.0f));
    CHECK(vertices[3] == Vector2(7.0f, 20.0f));
    const AABB bounds = f.LastCommandWorldBounds();
    CHECK(bounds.x == doctest::Approx(7.0f));
    CHECK(bounds.y == doctest::Approx(16.0f));
    CHECK(bounds.width == doctest::Approx(3.0f));
    CHECK(bounds.height == doctest::Approx(4.0f));
}

TEST_CASE("UI identity affine plus translation preserves canonical vertices") {
    SharedTextConsumerFixture f;
    const FixedPoint origin{Fixed26_6::FromRaw(5 * 64),
                            Fixed26_6::FromRaw(7 * 64)};
    CHECK(f.CollectLabelWithAffine(origin).CanonicalVertices() ==
          f.CollectLabelReferenceTranslation(origin).CanonicalVertices());
}

// 위 케이스의 성공 증인. 두 결과가 비어 있으면 평행이동을 통째로 버려도 같다.
TEST_CASE("the UI affine parity fixture actually produces vertices") {
    SharedTextConsumerFixture f;
    const FixedPoint origin{Fixed26_6::FromRaw(5 * 64),
                            Fixed26_6::FromRaw(7 * 64)};
    const auto translated = f.CollectLabelWithAffine(origin);
    const auto reference = f.CollectLabelReferenceTranslation(FixedPoint{});
    REQUIRE(translated.CanonicalVertices().size() >= 2U);
    // 평행이동이 실제로 좌표를 옮겼는가. 옮기지 않았다면 위 케이스는
    // "0을 더한 것과 같다"만 말한다.
    CHECK_FALSE(translated.CanonicalVertices() == reference.CanonicalVertices());
}

TEST_CASE("non-finite text affine produces no render command") {
    SharedTextConsumerFixture f;
    TextCollectContext context = f.UiCollectContext();
    context.layoutToOutput.m00 =
        std::numeric_limits<float>::quiet_NaN();
    f.renderer.CollectLayout(f.queue, *f.ValidLayout(), context, f.sink);
    CHECK(f.queue.TextCommandCount() == 0);
    CHECK(HasDiagnostic(f.sink,
          molga::text::TextDiagnosticCode::LayoutInvalid));
}

TEST_CASE("every non-finite affine component is rejected and a finite one is not") {
    SharedTextConsumerFixture f;
    auto layout = f.ValidLayout();
    // 성공 증인이 먼저다. 이 경로가 통째로 막혀 있으면 아래 여섯 거절은
    // "아무것도 그리지 않는 구현"과 구분되지 않는다.
    {
        auto scope = f.renderer.BeginGlyphCollection(900);
        f.renderer.CollectLayout(f.queue, *layout, f.UiCollectContext(), f.sink);
    }
    REQUIRE(f.queue.TextCommandCount() > 0);

    const float bad[] = {std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity()};
    for (const float value : bad) {
        for (int component = 0; component < 6; ++component) {
            SharedTextConsumerFixture local;
            TextCollectContext context = local.UiCollectContext();
            float* fields[] = {&context.layoutToOutput.m00,
                               &context.layoutToOutput.m01,
                               &context.layoutToOutput.m10,
                               &context.layoutToOutput.m11,
                               &context.layoutToOutput.tx,
                               &context.layoutToOutput.ty};
            *fields[component] = value;
            local.renderer.CollectLayout(local.queue, *local.ValidLayout(),
                                         context, local.sink);
            CHECK(local.queue.TextCommandCount() == 0);
            CHECK(HasDiagnostic(local.sink, TextDiagnosticCode::LayoutInvalid));
            CHECK(local.glyphAtlas.LookupCountForTest() == 0);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 1h: 래스터 배율의 권한
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("raster scale key drives the atlas pixel height, not the advance") {
    SharedTextConsumerFixture f;
    text::TextLayoutRequest request =
        f.LabelRequest(u8"A", std::string(kPrimaryFamilyGuid));
    // 정수가 아닌 크기. 반올림 규칙이 사라지면 두 키가 같은 픽셀 높이로 접힌다.
    request.style.shape.fontSize = Fixed26_6::FromRaw(16 * 64 + 32);  // 16.5px
    const auto layout = f.LayoutFor(request, u8"A@16.5");
    REQUIRE(layout.has_value());

    molga::GlyphAtlasKey keyAt1x;
    molga::GlyphAtlasKey keyAt2x;
    std::vector<molga::RenderCommand> at1x;
    std::vector<molga::RenderCommand> at2x;
    for (const std::uint16_t rasterScaleKey : {std::uint16_t{64},
                                               std::uint16_t{128}}) {
        TextCollectContext context = f.UiCollectContext();
        context.rasterPolicy.rasterScaleKey = rasterScaleKey;
        const std::size_t before = f.queue.Raw().GetCommands().size();
        {
            auto scope = f.renderer.BeginGlyphCollection(rasterScaleKey);
            f.renderer.CollectLayout(f.queue, **layout, context, f.sink);
        }
        const auto& commands = f.queue.Raw().GetCommands();
        std::vector<molga::RenderCommand> produced(
            commands.begin() + static_cast<std::ptrdiff_t>(before),
            commands.end());
        REQUIRE_FALSE(produced.empty());
        if (rasterScaleKey == 64) {
            keyAt1x = f.glyphAtlas.LastUploadedKeyForTest();
            at1x = std::move(produced);
        } else {
            keyAt2x = f.glyphAtlas.LastUploadedKeyForTest();
            at2x = std::move(produced);
        }
    }

    // ── 계획서에서 벗어난 한 줄 (설계 결정 2, 2026-09-07) ───────────────────
    // 계획서의 이 자리는 keyAt2x.rasterScaleKey == 128을 요구했다. 그것은
    // 채택된 표기와 모순이다: pixelSize가 이미 정책 배율을 접은 최종 래스터
    // 높이이므로, 키의 배율 자리에 정책 배율을 한 번 더 실으면 atlas가
    // pixelSize * rasterScaleKey / 64로 다시 곱해 배율이 제곱된다(16.5px가
    // 2배 정책에서 33px이 아니라 66px). 그래서 키의 배율 자리에는 항등원만
    // 들어가고, 정책은 pixelSize로만 나타난다.
    //
    // 그래도 이 케이스가 증명하려던 것은 그대로 남는다: 정책 배율을 떨어뜨린
    // 구현은 두 pixelSize를 같게 만들므로 아래 세 줄에서 걸린다.
    CHECK(keyAt1x.rasterScaleKey == 64);
    CHECK(keyAt2x.rasterScaleKey == 64);
    // Step 4의 정확한 양자화: roundHalfAway(fontSize.Raw() * key / 4096).
    CHECK(keyAt1x.pixelSize == 17);   // 1056*64/4096 = 16.5 -> 17
    CHECK(keyAt2x.pixelSize == 33);   // 1056*128/4096 = 33
    CHECK(keyAt1x.pixelSize != keyAt2x.pixelSize);
    CHECK_FALSE(keyAt1x == keyAt2x);
    CHECK(keyAt1x.glyphId == keyAt2x.glyphId);

    // 같은 불변 배치이므로 논리 좌표는 같아야 한다. 래스터 픽셀을 논리 26.6으로
    // 되돌리는 변환이 배율을 무시하면 2x 인용이 두 배 크기로 나온다.
    REQUIRE(at1x.size() == at2x.size());
    for (std::size_t index = 0; index < at1x.size(); ++index) {
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            CHECK(std::abs(ToRaw26_6(at1x[index].vertices[corner].x) -
                           ToRaw26_6(at2x[index].vertices[corner].x)) <= 1);
            CHECK(std::abs(ToRaw26_6(at1x[index].vertices[corner].y) -
                           ToRaw26_6(at2x[index].vertices[corner].y)) <= 1);
        }
    }
}

TEST_CASE("raster policy quantizes UI and world scale with half away from zero") {
    text::VectorTextDiagnosticSink sink;
    // UI: physicalPixels * 4096 / logicalRawExtent, X/Y의 최댓값.
    const auto ui = TextRasterPolicy::FromUiScale(
        FixedSize{Fixed26_6::FromRaw(800 * 64), Fixed26_6::FromRaw(600 * 64)},
        molga::PixelSize{1600, 600}, sink);
    REQUIRE(ui.has_value());
    CHECK(ui->rasterScaleKey == 128);

    const auto uniform = TextRasterPolicy::FromUiScale(
        FixedSize{Fixed26_6::FromRaw(800 * 64), Fixed26_6::FromRaw(600 * 64)},
        molga::PixelSize{800, 600}, sink);
    REQUIRE(uniform.has_value());
    CHECK(uniform->rasterScaleKey == 64);

    // 반올림은 0에서 먼 쪽이다. 1.5 -> 96이 아니라 정확히 96, 그리고
    // 64*1.0078125 = 64.5 -> 65.
    const auto half = TextRasterPolicy::FromWorldPixelsPerUnit(
        64.5 / 64.0, sink);
    REQUIRE(half.has_value());
    CHECK(half->rasterScaleKey == 65);

    // 0/비유한/넘침은 거절이고 진단 하나가 남는다.
    for (const double bad : {0.0, -1.0,
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             1.0e9}) {
        text::VectorTextDiagnosticSink badSink;
        CHECK_FALSE(
            TextRasterPolicy::FromWorldPixelsPerUnit(bad, badSink).has_value());
        CHECK(HasDiagnostic(badSink, TextDiagnosticCode::LayoutInvalid));
    }
    CHECK_FALSE(TextRasterPolicy::FromUiScale(
        FixedSize{}, molga::PixelSize{800, 600}, sink).has_value());
    CHECK_FALSE(TextRasterPolicy::FromUiScale(
        FixedSize{Fixed26_6::FromRaw(800 * 64), Fixed26_6::FromRaw(600 * 64)},
        molga::PixelSize{0, 600}, sink).has_value());
}

TEST_CASE("world transform scale multiplies the frame raster policy") {
    text::VectorTextDiagnosticSink sink;
    const auto base = TextRasterPolicy::FromWorldPixelsPerUnit(2.0, sink);
    REQUIRE(base.has_value());
    CHECK(base->rasterScaleKey == 128);

    // 명시된 프레임 정책 × max(|sx|,|sy|). 음수 배율은 크기를 바꾸지 않는다.
    const auto scaled = base->ScaledForWorldTransform(-2.0f, 3.0f, sink);
    REQUIRE(scaled.has_value());
    CHECK(scaled->rasterScaleKey == 384);   // 128 * 3

    const auto mirrored = base->ScaledForWorldTransform(-3.0f, 2.0f, sink);
    REQUIRE(mirrored.has_value());
    CHECK(mirrored->rasterScaleKey == 384);

    for (const std::pair<float, float> bad :
         {std::pair<float, float>{0.0f, 0.0f},
          std::pair<float, float>{std::numeric_limits<float>::quiet_NaN(), 1.0f},
          std::pair<float, float>{1.0f, std::numeric_limits<float>::infinity()},
          std::pair<float, float>{1.0e6f, 1.0f}}) {
        text::VectorTextDiagnosticSink badSink;
        CHECK_FALSE(
            base->ScaledForWorldTransform(bad.first, bad.second, badSink)
                .has_value());
        CHECK(HasDiagnostic(badSink, TextDiagnosticCode::LayoutInvalid));
    }
}

TEST_CASE("an out-of-range raster scale key emits LayoutInvalid and no lookup") {
    SharedTextConsumerFixture f;
    auto layout = f.ValidLayout();
    TextCollectContext context = f.UiCollectContext();
    // pixelSize = roundHalfAway(16*64 * 65535 / 4096) = 16384 > ... 아니라
    // 유효하지만, 0은 언제나 거절이다.
    context.rasterPolicy.rasterScaleKey = 0;
    {
        auto scope = f.renderer.BeginGlyphCollection(1234);
        f.renderer.CollectLayout(f.queue, *layout, context, f.sink);
    }
    CHECK(f.queue.TextCommandCount() == 0);
    CHECK(f.glyphAtlas.LookupCountForTest() == 0);
    CHECK(HasDiagnostic(f.sink, TextDiagnosticCode::LayoutInvalid));
}

// ═══════════════════════════════════════════════════════════════════════════
// Step 1i: 월드 수집의 권한
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("world traversal hands text components the caller-owned authority") {
    SharedTextConsumerFixture f;
    f.worldText.SetText("Wa");
    WorldRenderCollectionContext context = f.WorldCollectionContext();
    REQUIRE(context.textRenderer == &f.renderer);
    REQUIRE(context.textDiagnostics == &f.sink);
    {
        auto scope = f.renderer.BeginGlyphCollection(500);
        molga::CollectWorldRender(f.worldObjects, f.queue, context);
    }
    CHECK(f.queue.TextCommandCount() > 0);
    // 그 명령들이 이 renderer가 소유한 atlas를 실제로 지났는가. 컴포넌트가
    // 몰래 두 번째 서비스를 세우면 이 계수기는 움직이지 않는다.
    CHECK(f.glyphAtlas.LookupCountForTest() > 0);
    // 성공 경로는 조용하다. 진단이 남았다면 그것은 이 문맥이 권한을 제대로
    // 넘기지 못했다는 뜻이다.
    CHECK(f.Diagnostics().empty());
}

TEST_CASE("a null text context is refused when a text component is reachable") {
    SharedTextConsumerFixture f;
    f.worldText.SetText("Wa");
    molga::CollectWorldRender(f.worldObjects, f.queue,
                              WorldRenderCollectionContext::NonTextOnlyForTesting());
    // 널 권한으로는 어떤 atlas 작업도 어떤 명령도 만들어지지 않는다.
    CHECK(f.queue.TextCommandCount() == 0);
    CHECK(f.glyphAtlas.LookupCountForTest() == 0);
}

TEST_CASE("TextRenderer::Init stores no diagnostic sink") {
    SharedTextConsumerFixture f;
    const std::size_t afterInit = f.initSink.Diagnostics().size();
    f.CollectLabel(u8"Ag");
    // Init에 건넨 sink는 그 호출 안에서만 빌려진다. 참조를 보관했다면 이후의
    // 배치/수집 진단이 그쪽으로도 흘러든다.
    CHECK(f.initSink.Diagnostics().size() == afterInit);
}

// ═══════════════════════════════════════════════════════════════════════════
// 런타임 문자열의 입력 상한 (Task 8.2가 문지기인 Utf8Invalid 스트림)
// ═══════════════════════════════════════════════════════════════════════════
//
// 이 태스크가 런타임 문자열을 이 파이프라인에 처음 들여보낸다. TextLayout의
// validationFacts는 설계상 상한이 없다 — 패키지 검증이 ill-formed 바이트 범위를
// 하나도 빠짐없이 봐야 하고, TextDiagnosticRateLimitKey는 원본 바이트 범위를
// 키에 접으므로 진단 쪽 rate limit이 사실 쪽에는 닿지 않는다. 그래서 상한은
// 사실이 아니라 입력에 건다.
//
// 자르지 않고 거절한다: UTF-8 한가운데를 자르면 원문에 없던 ill-formed 바이트를
// 우리가 만들어 내고, 그 바이트에 대한 사실이 저작자에게 보고된다.

TEST_CASE("a paragraph at the production byte cap lays out and one byte over is refused") {
    SharedTextConsumerFixture f;
    // 정확히 상한. ASCII이므로 바이트 수가 곧 문자 수이고, 경계에서 잘리는
    // 코드 단위가 없다.
    const std::string atCap(kMaxProductionTextBytes, 'A');
    REQUIRE(atCap.size() == kMaxProductionTextBytes);
    const auto accepted =
        f.renderer.Layout(f.LabelRequest(atCap, kPrimaryFamilyGuid), f.sink);
    CHECK(accepted.has_value());
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::LayoutInvalid));

    // 한 바이트 더. 거절이고, 어떤 배치도 만들어지지 않는다.
    text::VectorTextDiagnosticSink overSink;
    const std::string overCap(kMaxProductionTextBytes + 1U, 'A');
    const auto refused =
        f.renderer.Layout(f.LabelRequest(overCap, kPrimaryFamilyGuid), overSink);
    CHECK_FALSE(refused.has_value());
    CHECK(HasDiagnostic(overSink, TextDiagnosticCode::LayoutInvalid));
}

// 상한이 실제로 막는 것은 "깨진 바이트마다 사실 하나"의 무한 성장이다. 상한
// 아래의 최악 입력이 내는 사실 수는 입력 바이트 수로 묶이고, 상한 위의 같은
// 입력은 사실을 하나도 만들지 않는다.
TEST_CASE("the byte cap bounds the unbounded validation-fact stream") {
    SharedTextConsumerFixture f;
    // maximal subpart마다 사실 하나가 나오는 최악의 입력: 이어지는 바이트만
    // 늘어놓으면 바이트마다 하나씩이다.
    const std::string worstUnderCap(kMaxProductionTextBytes,
                                    static_cast<char>(0x80));
    const auto underCap = f.renderer.Layout(
        f.LabelRequest(worstUnderCap, kPrimaryFamilyGuid), f.sink);
    REQUIRE(underCap.has_value());
    // 사실 수는 입력 바이트 수를 넘지 않는다. 이것이 상한이 사는 이유다:
    // 입력이 묶이지 않으면 이 수도 묶이지 않는다.
    CHECK((*underCap)->validationFacts.size() <= kMaxProductionTextBytes);
    CHECK((*underCap)->validationFacts.size() > 0U);

    text::VectorTextDiagnosticSink overSink;
    const std::string worstOverCap(kMaxProductionTextBytes * 4U,
                                   static_cast<char>(0x80));
    CHECK_FALSE(
        f.renderer
            .Layout(f.LabelRequest(worstOverCap, kPrimaryFamilyGuid), overSink)
            .has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// TextRenderer2D 스키마 (Task 8.1에서 이어짐)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRenderer2D: properties and getters/setters") {
    TextRenderer2D textComp;

    // Default values
    CHECK(textComp.GetText() == "Text");
    CHECK(textComp.GetColor().r == doctest::Approx(1.0f));
    CHECK(textComp.GetColor().g == doctest::Approx(1.0f));
    CHECK(textComp.GetColor().b == doctest::Approx(1.0f));
    CHECK(textComp.GetColor().a == doctest::Approx(1.0f));
    CHECK(textComp.GetComponentScale() == doctest::Approx(1.0f));
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
    textComp.SetComponentScale(2.5f);
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
    CHECK(textComp.GetComponentScale() == doctest::Approx(2.5f));
    CHECK(textComp.GetAlignment() == TextRenderer2D::Alignment::Center);
    CHECK(textComp.GetFontGuid() == "0123456789abcdef0123456789abcdef");
    CHECK(textComp.GetFontSizePx() == doctest::Approx(28.0f));
    CHECK(textComp.GetLineSpacing() == doctest::Approx(1.5f));
    CHECK(textComp.GetFontName() == "custom_font");
    CHECK(textComp.GetSortingOrder() == 15);
    CHECK(!textComp.IsEnabled());
}

TEST_CASE("TextRenderer2D: serialization and deserialization roundtrip") {
    // Register TextRenderer2D in factory is done by REGISTER_COMPONENT macro
    auto original = std::make_shared<GameObject>("TextObj");
    TextRenderer2D* tr = original->AddComponent<TextRenderer2D>();
    tr->SetText("Testing\nMultiline\nText");
    tr->SetColor(Color(0.1f, 0.2f, 0.3f, 0.4f));
    tr->SetComponentScale(1.5f);
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
    CHECK(restoredTr->GetComponentScale() == doctest::Approx(1.5f));
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
    CHECK(component.GetComponentScale() == doctest::Approx(2.0f));
    CHECK(component.GetLineSpacing() == doctest::Approx(1.2f));
}
