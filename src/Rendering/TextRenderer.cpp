#include "TextRenderer.h"

#include "Common/Log.h"
#include "Core/AssetDatabase.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/Texture.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextRuntimeDependencies.h"
#include "Text/TextShapingService.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using molga::Fixed26_6;
using molga::FixedPoint;
using molga::text::TextDiagnostic;
using molga::text::TextDiagnosticCode;
using molga::text::TextDiagnosticSink;
using molga::text::TextSeverity;
using molga::text::SourceByteRange;

// Task 6.3: 널이면 아무 일도 하지 않는다. Renderer.h의 종료 단계 hook과 같은
// 모양이고, 같은 이유로 출하되는 빌드에 남는다.
molga::detail::GlyphCollectionEndHook g_glyphCollectionEndHook = nullptr;
molga::detail::TextRendererShutdownStageHook g_shutdownStageHook = nullptr;

// Task 8.2 Step 3b: 정적 저장 수명이 아닌 프로세스 인스턴스. 자세한 이유는
// TextRenderer::Get()의 선언 옆에 있다.
TextRenderer* g_processInstance = nullptr;

// 한 번의 수집이 낼 수 있는 진단의 남은 몫.
struct CollectDiagnosticBudget {
    std::size_t remaining = kMaxCollectDiagnosticsPerLayout;
};

void ReportBounded(TextDiagnosticSink& sink, CollectDiagnosticBudget& budget,
                   TextDiagnosticCode code, std::string message,
                   std::string remediation, SourceByteRange bytes) {
    if (budget.remaining == 0U) return;
    --budget.remaining;
    TextDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = TextSeverity::Error;
    diagnostic.subsystem = "text-renderer";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    diagnostic.sourceByteRange = bytes;
    sink.Report(std::move(diagnostic));
}

// 상한 밖의 종결 진단. 곧바로 반환으로 이어져 호출당 많아야 하나이고, 예산에
// 밀려 사라지면 "아무것도 그리지 않았는데 이유를 말하는 진단이 없다"가 된다.
void ReportTerminal(TextDiagnosticSink& sink, TextDiagnosticCode code,
                    std::string message, std::string remediation) {
    TextDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = TextSeverity::Error;
    diagnostic.subsystem = "text-renderer";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    sink.Report(std::move(diagnostic));
}

bool AllFinite(const TextAffine2D& affine) {
    return std::isfinite(affine.m00) && std::isfinite(affine.m01) &&
           std::isfinite(affine.m10) && std::isfinite(affine.m11) &&
           std::isfinite(affine.tx) && std::isfinite(affine.ty);
}

// Q10.6 배율의 정의역. 0은 "배율 없음"이 아니라 "높이 0"이므로 거절한다.
constexpr std::int64_t kMinRasterScaleKey = 1;
constexpr std::int64_t kMaxRasterScaleKey = 65535;

std::optional<std::uint16_t> QuantizeRasterScale(
    std::optional<Fixed26_6> quantized) {
    if (!quantized) return std::nullopt;
    const std::int64_t raw = quantized->Raw();
    if (raw < kMinRasterScaleKey || raw > kMaxRasterScaleKey) return std::nullopt;
    return static_cast<std::uint16_t>(raw);
}

void ReportRasterPolicy(TextDiagnosticSink& sink, std::string message) {
    ReportTerminal(sink, TextDiagnosticCode::LayoutInvalid, std::move(message),
                   "supply a finite positive output scale; text raster scale "
                   "is an unsigned Q10.6 value in [1, 65535] where 64 is 1x");
}

// 양수 실수 하나를 0에서 먼 쪽으로 반올림한 Q10.6 정수로. 실수 경로는 UI의
// 정수 비율과 달리 카메라 변환에서 오므로 여기서만 부동소수를 받는다.
std::optional<std::uint16_t> QuantizePositiveScale(double scale) {
    if (!std::isfinite(scale) || scale <= 0.0) return std::nullopt;
    const double key = std::floor(scale * 64.0 + 0.5);
    if (!(key >= static_cast<double>(kMinRasterScaleKey)) ||
        key > static_cast<double>(kMaxRasterScaleKey)) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(key);
}

void SetVertex(molga::Vertex2D& vertex, Vector2 position, float u, float v,
               const Color& color) {
    vertex = {position.x, position.y, u, v, color.r, color.g, color.b, color.a};
}

AABB BoundsOfQuad(const std::array<Vector2, 4>& corners) {
    float minX = corners[0].x, maxX = corners[0].x;
    float minY = corners[0].y, maxY = corners[0].y;
    for (std::size_t index = 1; index < corners.size(); ++index) {
        minX = std::min(minX, corners[index].x);
        maxX = std::max(maxX, corners[index].x);
        minY = std::min(minY, corners[index].y);
        maxY = std::max(maxY, corners[index].y);
    }
    return AABB{minX, minY, maxX - minX, maxY - minY};
}

}  // namespace

namespace molga {
namespace detail {

void SetGlyphCollectionEndHookForTest(
    GlyphCollectionEndHook hook) noexcept {
    g_glyphCollectionEndHook = hook;
}

void SetTextRendererShutdownStageHookForTest(
    TextRendererShutdownStageHook hook) noexcept {
    g_shutdownStageHook = hook;
}

} // namespace detail
} // namespace molga

// ── Step 3: affine 적용 ──────────────────────────────────────────────────────

Vector2 TextAffine2D::Apply(molga::FixedPoint point) const {
    const float x = point.x.ToFloat();
    const float y = point.y.ToFloat();
    return Vector2(m00 * x + m01 * y + tx, m10 * x + m11 * y + ty);
}

// ── Step 3a: 래스터 배율 ─────────────────────────────────────────────────────

std::optional<TextRasterPolicy> TextRasterPolicy::FromUiScale(
    molga::FixedSize logicalViewport, molga::PixelSize physicalViewport,
    molga::text::TextDiagnosticSink& sink) {
    if (logicalViewport.width.Raw() <= 0 || logicalViewport.height.Raw() <= 0 ||
        physicalViewport.width <= 0 || physicalViewport.height <= 0) {
        ReportRasterPolicy(sink,
                           "the UI viewport has a non-positive logical or "
                           "physical extent, so no text raster scale exists");
        return std::nullopt;
    }
    // 정수 비율이다. physicalPixels * 4096 / logicalRawExtent는 (물리/논리)를
    // Q10.6으로 옮긴 값이고, 반올림은 Fixed26_6의 "0에서 먼 쪽" 규칙 하나만
    // 쓴다 — 축마다 다른 규칙을 쓰면 정사각 뷰포트가 축에 따라 갈린다.
    const auto keyX = QuantizeRasterScale(Fixed26_6::CheckedMulDiv(
        Fixed26_6::FromRaw(physicalViewport.width), 4096,
        logicalViewport.width.Raw()));
    const auto keyY = QuantizeRasterScale(Fixed26_6::CheckedMulDiv(
        Fixed26_6::FromRaw(physicalViewport.height), 4096,
        logicalViewport.height.Raw()));
    if (!keyX || !keyY) {
        ReportRasterPolicy(sink,
                           "the UI viewport scale does not fit the unsigned "
                           "Q10.6 text raster scale range");
        return std::nullopt;
    }
    // 두 축 중 큰 쪽. 작은 쪽을 고르면 늘어난 축이 늘 흐리게 래스터된다.
    TextRasterPolicy policy;
    policy.rasterScaleKey = std::max(*keyX, *keyY);
    return policy;
}

std::optional<TextRasterPolicy> TextRasterPolicy::FromWorldPixelsPerUnit(
    double pixelsPerWorldUnit, molga::text::TextDiagnosticSink& sink) {
    const auto key = QuantizePositiveScale(pixelsPerWorldUnit);
    if (!key) {
        ReportRasterPolicy(sink,
                           "the active camera's world-to-physical output "
                           "transform yields no usable text raster scale");
        return std::nullopt;
    }
    TextRasterPolicy policy;
    policy.rasterScaleKey = *key;
    return policy;
}

std::optional<TextRasterPolicy> TextRasterPolicy::ScaledForWorldTransform(
    float scaleX, float scaleY, molga::text::TextDiagnosticSink& sink) const {
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY)) {
        ReportRasterPolicy(sink,
                           "a world text transform carries a non-finite scale");
        return std::nullopt;
    }
    // 음수 배율은 거울일 뿐 크기가 아니다. 절댓값을 쓰지 않으면 뒤집힌 글자가
    // 언제나 최소 배율로 래스터된다.
    const double magnitude = std::max(std::fabs(static_cast<double>(scaleX)),
                                      std::fabs(static_cast<double>(scaleY)));
    const auto key = QuantizePositiveScale(
        static_cast<double>(rasterScaleKey) * magnitude / 64.0);
    if (!key) {
        ReportRasterPolicy(sink,
                           "a world text transform scales the frame raster "
                           "policy outside the unsigned Q10.6 range");
        return std::nullopt;
    }
    TextRasterPolicy policy;
    policy.rasterScaleKey = *key;
    return policy;
}

WorldRenderCollectionContext
WorldRenderCollectionContext::NonTextOnlyForTesting() {
    // 널 권한이다. 이 값을 받은 순회는 텍스트 컴포넌트를 만나면 아무것도
    // 만들지 않고 거절한다 — 픽스처가 "이 world에는 텍스트가 없다"를 이미
    // 증명했을 때만 쓰라는 뜻이 그 거절이다.
    return WorldRenderCollectionContext{};
}

// ── Step 3b: 소유된 텍스트 서비스 한 벌 ──────────────────────────────────────
// 선언 순서가 의존 순서이고, 멤버 역순 파괴가 곧 해체 순서다:
// layout -> cache -> shaper -> resolver -> repository. 이 순서가 지켜져야
// 애플리케이션 런타임 guard의 종결 ICU 정리가 살아 있는 서비스 밑에서 돌지
// 않는다.
struct TextRenderer::TextServices {
    explicit TextServices(const molga::AssetDatabase& database)
        : repository(database),
          resolver(database, repository),
          cache(molga::text::TextLayoutCacheLimits::Production()),
          layout(resolver, shaper, cache) {}

    molga::text::FontRepository repository;
    molga::text::FontFamilyResolver resolver;
    molga::text::TextShapingService shaper;
    molga::text::TextLayoutCache cache;
    molga::text::TextLayoutService layout;
};

// ── Task 6.3 Step 6a: 수집 scope의 진입/이탈 ────────────────────────────────

TextRenderer::GlyphCollectionScope::GlyphCollectionScope(
    TextRenderer& owner, std::uint64_t frameIndex)
    : owner_(&owner), frameIndex_(frameIndex) {
    if (owner.glyphCollectionActive_) {
        // 아무것도 바꾸기 전에 던진다. atlas_.BeginFrame을 먼저 부르면 그
        // 호출이 이미 열려 있던 수집의 pin 집합을 지우므로, 거절된 중첩이
        // 오히려 첫 수집의 page들을 축출 가능하게 만든다.
        throw std::logic_error(
            "TextRenderer::BeginGlyphCollection is not nestable");
    }
    owner.glyphCollectionActive_ = true;
    owner.atlas_.BeginFrame(frameIndex);
}

TextRenderer::GlyphCollectionScope::GlyphCollectionScope(
    GlyphCollectionScope&& other) noexcept
    : owner_(other.owner_), frameIndex_(other.frameIndex_) {
    other.owner_ = nullptr;
}

TextRenderer::GlyphCollectionScope::~GlyphCollectionScope() {
    if (owner_ == nullptr) return;
    TextRenderer& owner = *owner_;
    // 소유권을 먼저 놓는다. 두 번 닫히면 두 번째 EndCollection이 다음 수집의
    // pin을 지운다.
    owner_ = nullptr;
    owner.glyphCollectionActive_ = false;
    owner.atlas_.EndCollection(frameIndex_);
    if (g_glyphCollectionEndHook != nullptr) {
        g_glyphCollectionEndHook(frameIndex_);
    }
}

TextRenderer::GlyphCollectionScope TextRenderer::BeginGlyphCollection(
    std::uint64_t frameIndex) {
    return GlyphCollectionScope(*this, frameIndex);
}

molga::GlyphAtlasCache& TextRenderer::GlyphAtlas() noexcept { return atlas_; }

const molga::GlyphAtlasCache& TextRenderer::GlyphAtlas() const noexcept {
    return atlas_;
}

bool ShutdownRendererThenTextGpuResources(
    Renderer& renderer, TextRenderer& textRenderer,
    molga::text::TextDiagnosticSink& sink) {
    // 먼저 일을 멈추고 GPU idle을 증명하고 반납 큐를 비운다. 이 호출이
    // 돌아왔다는 것은 제출된 명령이 하나도 남아 있지 않다는 뜻이고, 그때에야
    // 그 명령들이 읽던 텍스처를 부술 수 있다.
    renderer.Shutdown();
    // GraphicsDevice는 아직 살아 있다(EngineShutdown이 마지막이다). 죽은
    // 장치에 대고 텍스처를 해제하지 않는 것이 이 순서 전부다.
    const bool released = textRenderer.ShutdownAfterGpuIdle(sink);
    if (!released) {
        Log::Error("TextRenderer",
                   "Text GPU teardown was refused; atlas pages or text "
                   "services are still held after the renderer proved idle.");
        // 인스턴스를 남긴다. 붙들려 있는 page 위에서 부수는 것보다 프로세스
        // 종료 때 OS가 회수하게 두는 쪽이 싸다.
        return false;
    }
    if (&textRenderer == g_processInstance) {
        // 프로세스 인스턴스는 여기서만 죽는다. 정적 저장 수명이었다면 이
        // 파괴가 guard의 u_cleanup 다음에 일어난다.
        TextRenderer::DestroyProcessInstance();
    }
    return true;
}

TextRenderer& TextRenderer::Get() {
    if (g_processInstance == nullptr) g_processInstance = new TextRenderer();
    return *g_processInstance;
}

void TextRenderer::DestroyProcessInstance() noexcept {
    delete g_processInstance;
    g_processInstance = nullptr;
}

TextRenderer::TextRenderer() = default;

TextRenderer::~TextRenderer() {
    // 프로세스 인스턴스는 ShutdownRendererThenTextGpuResources가 부수므로 이
    // 소멸자는 픽스처가 소유한 renderer의 것이다. 두 멤버 모두 ICU/HarfBuzz
    // 객체를 담지 않으므로 여기서 놓는 것이 종결 정리와 경합하지 않는다.
    (void)atlas_.ReleaseAfterGpuIdle();
    services_.reset();
}

bool TextRenderer::Init(const molga::AssetDatabase& database,
                        molga::text::TextDiagnosticSink& sink) {
    if (services_) return true;
    if (!molga::text::TextRuntimeDependencies::Get().IsReady()) {
        ReportTerminal(sink, TextDiagnosticCode::DependencyInvalid,
                       "the shared text renderer requires a ready text "
                       "runtime; ICU and HarfBuzz have not been initialized "
                       "in this process",
                       "Create a TextRuntimeLifetimeGuard from a verified "
                       "Engine/Text root before initializing the text "
                       "renderer, and keep it alive for as long as the "
                       "renderer exists.");
        return false;
    }
    // 먼저 통째로 만들고 나서 게시한다. 중간에 실패하면 부분 aggregate도
    // 초기화된 renderer도 남지 않는다.
    auto services = std::make_unique<TextServices>(database);
    services_ = std::move(services);
    return true;
}

bool TextRenderer::ShutdownAfterGpuIdle(
    molga::text::TextDiagnosticSink& sink) {
    // 아무것도 바꾸기 전에 거절한다. 열려 있는 수집이나 아직 반납되지 않은
    // 제출 지분은 "종료 순서가 뒤집혔다"의 증거이므로, 그 위에서 page를
    // 부수면 살아 있는 명령 밑에서 텍스처가 사라진다.
    if (glyphCollectionActive_ || atlas_.LiveExternalPagePinCount() != 0U) {
        ReportTerminal(sink, TextDiagnosticCode::ReferenceInvalid,
                       "text GPU teardown ran while a glyph collection was "
                       "open or submitted atlas pages were still held",
                       "Complete the renderer idle and retirement drain, and "
                       "close every glyph collection scope, before shutting "
                       "the text renderer down.");
        return false;
    }
    if (!atlas_.ReleaseAfterGpuIdle()) {
        ReportTerminal(sink, TextDiagnosticCode::ReferenceInvalid,
                       "the glyph atlas refused to release its pages after "
                       "the renderer reported GPU idle",
                       "Complete the renderer idle and retirement drain "
                       "before shutting the text renderer down.");
        return false;
    }
    if (g_shutdownStageHook != nullptr) g_shutdownStageHook("atlas_cleared");
    services_.reset();
    if (g_shutdownStageHook != nullptr) {
        g_shutdownStageHook("text_services_destroyed");
    }
    return true;
}

molga::text::TextLayoutService& TextRenderer::LayoutService() noexcept {
    return services_->layout;
}

std::optional<std::shared_ptr<const molga::text::TextLayout>>
TextRenderer::Layout(const molga::text::TextLayoutRequest& request,
                     molga::text::TextDiagnosticSink& sink) {
    if (!services_) {
        ReportTerminal(sink, TextDiagnosticCode::ReferenceInvalid,
                       "the shared text renderer has no initialized text "
                       "services",
                       "Call TextRenderer::Init with the process asset "
                       "database before laying out text.");
        return std::nullopt;
    }
    if (request.utf8.size() > kMaxProductionTextBytes) {
        // 잘라 내지 않는다. UTF-8 한가운데를 자르면 원문에 없던 ill-formed
        // 바이트를 우리가 만들어 낸다.
        ReportTerminal(sink, TextDiagnosticCode::LayoutInvalid,
                       "a production text paragraph exceeds the " +
                           std::to_string(kMaxProductionTextBytes) +
                           " byte limit for one layout request",
                       "Split very long runtime text into separate paragraphs; "
                       "one layout keeps an unbounded validation fact per "
                       "ill-formed byte range, so the input is what is "
                       "bounded.");
        return std::nullopt;
    }
    return services_->layout.Layout(request, sink);
}

// ── Step 3d/4/4a/5/5a/5b: 배치 하나를 명령으로 ───────────────────────────────

namespace {

// 래스터 픽셀 하나는 논리 26.6에서 4096/rasterScaleKey 단위다(래스터 높이가
// 논리 높이의 rasterScaleKey/64배이므로). 이 변환이 배율을 무시하면 2배
// 래스터가 두 배 크기로 그려진다.
std::optional<Fixed26_6> RasterPixelsToLayout(std::int64_t pixels,
                                              std::uint16_t rasterScaleKey) {
    if (pixels < std::numeric_limits<std::int32_t>::min() ||
        pixels > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return Fixed26_6::CheckedMulDiv(
        Fixed26_6::FromRaw(static_cast<std::int32_t>(pixels)), 4096,
        static_cast<std::int64_t>(rasterScaleKey));
}

struct LogicalQuad {
    Fixed26_6 left = Fixed26_6::FromRaw(0);
    Fixed26_6 top = Fixed26_6::FromRaw(0);
    Fixed26_6 right = Fixed26_6::FromRaw(0);
    Fixed26_6 bottom = Fixed26_6::FromRaw(0);
};

std::array<Vector2, 4> TransformQuad(const LogicalQuad& quad,
                                     const TextAffine2D& affine) {
    return {affine.Apply(FixedPoint{quad.left, quad.top}),
            affine.Apply(FixedPoint{quad.right, quad.top}),
            affine.Apply(FixedPoint{quad.right, quad.bottom}),
            affine.Apply(FixedPoint{quad.left, quad.bottom})};
}

void FillCommonCommandFields(molga::RenderCommand& command,
                             const TextCollectContext& context) {
    command.sortKey.cameraPass = context.cameraPass;
    command.sortKey.sortingLayer = context.sortingLayer;
    command.sortKey.sortingOrder = context.sortingOrder;
    command.sortKey.depthOrYSort = context.depthOrYSort;
    command.batchKey.shaderName = "batch";
    command.batchKey.isBatchable = true;
    command.isBatchableSprite = true;
}

// Step 5a: 두부의 논리 사각형. 폭은 넓힌 advance의 절댓값이고 최소 raw는 1이다
// — 폭 0짜리 명령은 "없는 glyph가 있었다"를 화면에 남기지 못한다.
std::optional<LogicalQuad> TofuQuad(const molga::text::TextLine& line,
                                    const molga::text::PositionedGlyph& glyph) {
    const std::int64_t advance =
        std::llabs(static_cast<std::int64_t>(glyph.glyph.advanceX.Raw()));
    const std::int64_t width = std::max<std::int64_t>(advance, 1);
    if (width > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    const auto top = Fixed26_6::CheckedSub(line.baseline, line.ascent);
    const auto height = Fixed26_6::CheckedAdd(line.ascent, line.descent);
    if (!top || !height) return std::nullopt;
    const auto right = Fixed26_6::CheckedAdd(
        glyph.origin.x, Fixed26_6::FromRaw(static_cast<std::int32_t>(width)));
    const auto bottom = Fixed26_6::CheckedAdd(*top, *height);
    if (!right || !bottom) return std::nullopt;
    return LogicalQuad{glyph.origin.x, *top, *right, *bottom};
}

void EmitTofu(molga::RenderQueue& queue, const molga::text::TextLine& line,
              const molga::text::PositionedGlyph& positioned,
              const TextCollectContext& context,
              molga::text::TextDiagnosticSink& sink,
              CollectDiagnosticBudget& budget);

void CollectGlyph(molga::GlyphAtlasCache& atlas, molga::RenderQueue& queue,
                  const molga::text::TextLine& line,
                  const molga::text::PositionedGlyph& positioned,
                  const TextCollectContext& context,
                  molga::text::TextDiagnosticSink& sink,
                  CollectDiagnosticBudget& budget);

}  // namespace

void TextRenderer::CollectLayout(molga::RenderQueue& queue,
                                 const molga::text::TextLayout& layout,
                                 const TextCollectContext& context,
                                 molga::text::TextDiagnosticSink& sink) {
    // Step 3c: 명령을 하나라도 만들기 전에 문맥을 거절한다. 유한하지 않은
    // 성분 하나가 네 꼭짓점을 전부 NaN으로 만들고, NaN worldBounds는 어떤
    // 컬링에도 걸리지 않아 배칭이 조용히 망가진다.
    if (!AllFinite(context.layoutToOutput)) {
        ReportTerminal(sink, TextDiagnosticCode::LayoutInvalid,
                       "the layout-to-output affine carries a non-finite "
                       "component, so no text command can be created",
                       "Supply a finite affine; a non-finite world transform "
                       "or UI origin must be fixed at its source.");
        return;
    }
    const std::uint16_t rasterScaleKey = context.rasterPolicy.rasterScaleKey;
    if (rasterScaleKey < kMinRasterScaleKey) {
        ReportTerminal(sink, TextDiagnosticCode::LayoutInvalid,
                       "the text raster scale key is zero, which names a "
                       "raster height of zero rather than an absent scale",
                       "Derive the raster policy from the UI or camera output "
                       "transform; 64 is 1x.");
        return;
    }

    CollectDiagnosticBudget budget;
    for (const molga::text::TextLine& line : layout.lines) {
        for (const molga::text::VisualRun& run : line.visualRuns) {
            for (const molga::text::PositionedGlyph& positioned : run.glyphs) {
                CollectGlyph(atlas_, queue, line, positioned, context, sink,
                             budget);
            }
        }
    }
}

namespace {

void CollectGlyph(molga::GlyphAtlasCache& atlas, molga::RenderQueue& queue,
                  const molga::text::TextLine& line,
                  const molga::text::PositionedGlyph& positioned,
                  const TextCollectContext& context,
                  molga::text::TextDiagnosticSink& sink,
                  CollectDiagnosticBudget& budget) {
    const molga::text::ShapedGlyph& glyph = positioned.glyph;

    // Step 5: 없는 glyph와 face 없는 기록은 atlas에 닿지 않는다. 조회 자체가
    // page를 만들 수 있으므로, 예산이 0인 화면에서도 이 경로는 비용이 없다.
    if (glyph.missing || !glyph.faceResource || !glyph.faceResource->rasterFace) {
        EmitTofu(queue, line, positioned, context, sink, budget);
        return;
    }

    // Step 4: 정상 glyph의 완전한 atlas 키. AssetDatabase를 묻지도, GUID를
    // 다시 열지도, advance/affine에서 크기를 역산하지도 않는다.
    if (glyph.fontSize.Raw() <= 0) {
        ReportBounded(sink, budget, TextDiagnosticCode::LayoutInvalid,
                      "a shaped glyph carries a non-positive font size",
                      "Author a positive font size; the atlas key cannot name "
                      "a raster height without one.",
                      glyph.sourceBytes);
        return;
    }
    const std::uint16_t rasterScaleKey = context.rasterPolicy.rasterScaleKey;
    const auto pixelHeight = Fixed26_6::CheckedMulDiv(
        glyph.fontSize, static_cast<std::int64_t>(rasterScaleKey), 4096);
    if (!pixelHeight || pixelHeight->Raw() < 1 ||
        pixelHeight->Raw() > kMaxRasterScaleKey) {
        ReportBounded(sink, budget, TextDiagnosticCode::LayoutInvalid,
                      "the requested font size and raster scale name a glyph "
                      "raster height outside [1, 65535] pixels",
                      "Lower the authored font size or the output raster "
                      "scale.",
                      glyph.sourceBytes);
        return;
    }

    molga::GlyphAtlasKey key;
    key.fontGuid = glyph.fontGuid;
    key.fontRevision = glyph.fontRevision;
    key.faceIndex = glyph.faceIndex;
    key.glyphId = glyph.glyphId;
    key.pixelSize = static_cast<std::uint16_t>(pixelHeight->Raw());
    // ── 하나의 표기 (FontAtlas.h가 요구하는 그것) ────────────────────────────
    // GlyphAtlasCache는 래스터 높이를 pixelSize * rasterScaleKey / 64로 만든다
    // (Task 6.1). 위 pixelSize는 이미 정책 배율을 접은 최종 높이이므로, 같은
    // 정책 배율을 여기 한 번 더 실으면 배율이 제곱된다. 그래서 키의 배율
    // 자리에는 항등원만 들어간다: 이 프로세스는 "pixelSize가 최종 높이"라는
    // 표기 하나만 쓰고, 그래서 같은 그림을 내는 두 (크기, 배율) 조합이 캐시와
    // 예산을 두 번 먹는 일이 없다.
    key.rasterScaleKey = 64;
    key.variationKey = 0;
    key.renderMode = molga::GlyphRenderMode::Monochrome;

    // Step 4a: page 지분과 함께 handle을 받는다.
    const molga::GlyphHandle handle =
        atlas.GetGlyph(key, *glyph.faceResource->rasterFace, sink);
    if (handle.proceduralTofu) {
        // Step 5b: 포화도 없는 glyph와 같은 기하를 낸다. 어떤 page 정체성도
        // 토큰도 붙들지 않는다.
        EmitTofu(queue, line, positioned, context, sink, budget);
        return;
    }
    if (!handle.glyph.drawable) return;  // 공백 glyph에는 그릴 것이 없다

    // Step 3d: 래스터 픽셀 경계를 논리 26.6으로 되돌린다. advance 경계를
    // 대신 쓰지 않는다 — advance는 다음 pen 자리이지 잉크의 범위가 아니다.
    const auto bearingX = RasterPixelsToLayout(handle.glyph.xOffset,
                                               rasterScaleKey);
    const auto bearingY = RasterPixelsToLayout(handle.glyph.yOffset,
                                               rasterScaleKey);
    const auto width = RasterPixelsToLayout(handle.glyph.width, rasterScaleKey);
    const auto height = RasterPixelsToLayout(handle.glyph.height,
                                             rasterScaleKey);
    if (!bearingX || !bearingY || !width || !height) {
        ReportBounded(sink, budget, TextDiagnosticCode::LayoutInvalid,
                      "a rasterized glyph bitmap does not fit the checked 26.6 "
                      "layout range",
                      "Lower the authored font size or the output raster "
                      "scale.",
                      glyph.sourceBytes);
        return;
    }
    // positioned.origin은 이미 pen + HarfBuzz GPOS 오프셋이다
    // (TextLayoutService: origin.x = pen + offsetX, origin.y = baseline -
    // offsetY). 여기서 오프셋을 한 번 더 더하면 두 번 적용된다.
    const auto left = Fixed26_6::CheckedAdd(positioned.origin.x, *bearingX);
    const auto top = Fixed26_6::CheckedAdd(positioned.origin.y, *bearingY);
    if (!left || !top) {
        ReportBounded(sink, budget, TextDiagnosticCode::LayoutInvalid,
                      "a positioned glyph bitmap overflows the checked 26.6 "
                      "layout range",
                      "Move the text closer to the layout origin.",
                      glyph.sourceBytes);
        return;
    }
    const auto right = Fixed26_6::CheckedAdd(*left, *width);
    const auto bottom = Fixed26_6::CheckedAdd(*top, *height);
    if (!right || !bottom) {
        ReportBounded(sink, budget, TextDiagnosticCode::LayoutInvalid,
                      "a positioned glyph bitmap overflows the checked 26.6 "
                      "layout range",
                      "Move the text closer to the layout origin.",
                      glyph.sourceBytes);
        return;
    }

    const std::array<Vector2, 4> corners =
        TransformQuad(LogicalQuad{*left, *top, *right, *bottom},
                      context.layoutToOutput);

    molga::RenderCommand command;
    FillCommonCommandFields(command, context);
    // ── RetainedTexture만이 텍스처를 내준다 (Task 6.2/6.3에서 온 의무) ───────
    // GlyphInfo::texture는 page가 소유하는 원시 포인터다. 지분 없이 그것을
    // 실으면, 명령이 큐에 있는 동안 page가 축출되어도 아무도 알아채지 못한다.
    if (Texture* texture = molga::RetainedTexture(handle);
        texture != nullptr && texture->IsValid()) {
        command.batchKey.texture = texture->Handle();
        command.batchKey.textureSampler = texture->Sampler();
        command.batchKey.textureStableId = texture->StableId();
    }
    // 이름과 지분을 짝으로 싣는다. 하나만 실으면 반납할 수 없거나 이름 없는
    // 지분이 된다.
    command.resourceLifetimeIdentity = handle.pageIdentity;
    command.resourceLifetime = handle.pageLifetime;
    // stb 비트맵과 atlas CPU page는 위에서 아래로 채워지므로 위쪽 정점이 v0을,
    // 아래쪽 정점이 v1을 표본한다.
    SetVertex(command.vertices[0], corners[0], handle.glyph.u0, handle.glyph.v0,
              context.color);
    SetVertex(command.vertices[1], corners[1], handle.glyph.u1, handle.glyph.v0,
              context.color);
    SetVertex(command.vertices[2], corners[2], handle.glyph.u1, handle.glyph.v1,
              context.color);
    SetVertex(command.vertices[3], corners[3], handle.glyph.u0, handle.glyph.v1,
              context.color);
    command.worldBounds = BoundsOfQuad(corners);
    queue.Submit(command);
}

void EmitTofu(molga::RenderQueue& queue, const molga::text::TextLine& line,
              const molga::text::PositionedGlyph& positioned,
              const TextCollectContext& context,
              molga::text::TextDiagnosticSink& sink,
              CollectDiagnosticBudget& budget) {
    const auto quad = TofuQuad(line, positioned);
    if (!quad) {
        ReportBounded(sink, budget, TextDiagnosticCode::LayoutInvalid,
                      "a missing-glyph rectangle overflows the checked 26.6 "
                      "layout range",
                      "Move the text closer to the layout origin, or lower the "
                      "authored font size.",
                      positioned.glyph.sourceBytes);
        return;
    }
    const std::array<Vector2, 4> corners =
        TransformQuad(*quad, context.layoutToOutput);

    molga::RenderCommand command;
    FillCommonCommandFields(command, context);
    // Step 5/5c: 유효하지 않은 핸들 그대로 나간다. sprite 경로가 그것을
    // renderer 소유의 흰 텍스처로 묶으므로, atlas 예산이 0이어도 없는
    // grapheme마다 그릴 수 있는 명령이 하나씩 남는다. page 정체성도 토큰도
    // 붙들지 않는다 — 붙들 page가 없다.
    command.batchKey.texture = molga::TextureHandle{};
    command.resourceLifetimeIdentity = 0U;
    SetVertex(command.vertices[0], corners[0], 0.0f, 0.0f, context.color);
    SetVertex(command.vertices[1], corners[1], 1.0f, 0.0f, context.color);
    SetVertex(command.vertices[2], corners[2], 1.0f, 1.0f, context.color);
    SetVertex(command.vertices[3], corners[3], 0.0f, 1.0f, context.color);
    command.worldBounds = BoundsOfQuad(corners);
    queue.Submit(command);
}

}  // namespace
