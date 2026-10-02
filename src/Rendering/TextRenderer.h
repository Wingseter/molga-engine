#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "../Common/Types.h"
#include "Common/Fixed26_6.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/PixelSize.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutTypes.h"
#include "UI/UILayoutTypes.h"

class Renderer;
namespace molga {
class AssetDatabase;
class RenderQueue;
}  // namespace molga
namespace molga::text {
class TextLayoutService;
}

// ── Task 8.2 Step 3: 배치 좌표를 출력 좌표로 옮기는 affine ────────────────────
// 텍스트 배치는 언제나 논리 26.6 좌표로 나오고, UI와 월드는 그것을 서로 다른
// 방식으로 화면에 놓는다. 그 차이 전부가 이 여섯 float이다: UI는 항등 + 평행
// 이동, 월드는 scale-rotate-translate. 두 소비자가 각자 정점을 계산하면 같은
// 배치가 두 벌의 반올림을 갖게 되므로, 변환은 여기 한 벌만 있다.
struct TextAffine2D {
    float m00 = 1.0f, m01 = 0.0f;
    float m10 = 0.0f, m11 = 1.0f;
    float tx = 0.0f, ty = 0.0f;
    Vector2 Apply(molga::FixedPoint point) const;
};

// ── Task 8.2 Step 3a: 래스터 배율 ────────────────────────────────────────────
// 26.6과 같은 규약의 부호 없는 Q10.6이다(64가 1배). glyph advance나 수집
// affine에서 유도하지 않는다: 둘 다 폰트와 배치에 따라 달라지므로, 그렇게
// 유도한 배율은 같은 화면에서 글자마다 다른 래스터 높이를 만든다.
struct TextRasterPolicy {
    std::uint16_t rasterScaleKey = 64;  // unsigned Q10.6; 64 == 1x

    // 논리 뷰포트 대 물리 픽셀. X/Y를 각각 정수로 재고 그 최댓값을 쓴다 —
    // 작은 쪽을 쓰면 한 축이 항상 흐려진다.
    static std::optional<TextRasterPolicy> FromUiScale(
        molga::FixedSize logicalViewport, molga::PixelSize physicalViewport,
        molga::text::TextDiagnosticSink&);
    // 활성 카메라의 world-to-physical 출력 변환에서 온 값 하나.
    static std::optional<TextRasterPolicy> FromWorldPixelsPerUnit(
        double pixelsPerWorldUnit, molga::text::TextDiagnosticSink&);
    // 프레임 정책 × max(|scaleX|,|scaleY|). 음수 배율은 거울일 뿐 크기가
    // 아니므로 절댓값을 쓴다.
    std::optional<TextRasterPolicy> ScaledForWorldTransform(
        float scaleX, float scaleY, molga::text::TextDiagnosticSink&) const;
};

// 한 배치를 한 출력에 놓는 데 필요한 전부. Task 11.2가 여기에 옮겨진 UI draw
// order와 최종 물리 scissor를 더했으며, sink 없는 오버로드는 없다.
//
// 세 UI 필드는 **옮겨진 값이지 유도할 값이 아니다.** glyph 루프는 이것들을
// 복사만 하고 컴포넌트나 affine에서 다시 계산하지 않는다 — 다시 계산하는
// 순간 스냅샷이 얼려 둔 순서/클립과 화면이 갈릴 수 있고, 그 차이는 스냅샷을
// 다시 지어야만 보인다.
//
// 월드 텍스트는 uiDrawOrder/scissor를 비운 채로 온다. 비어 있는 uiDrawOrder는
// "이 명령은 월드 정렬 키로 정렬된다"는 뜻이고, 비어 있는 scissor는 "패스
// 전체"라는 뜻이다.
struct TextCollectContext {
    TextAffine2D layoutToOutput;
    Color color = Color::White();
    int cameraPass = 0;
    int sortingLayer = 0;
    int sortingOrder = 0;
    float depthOrYSort = 0.0f;
    TextRasterPolicy rasterPolicy;
    std::optional<molga::ui::UIDrawOrderKey> uiDrawOrder;
    // 이 배치가 예약한 구간의 첫 번호. 명령마다의 최종 stableSubmissionIndex는
    // 이 값에 **위치 기록 서수**를 더한 것이다 — 그릴 수 있는 명령 수가 아니다.
    // 공백 glyph는 명령을 내지 않지만 자기 서수를 소비한다.
    std::uint64_t stableSubmissionBase = 0;
    std::optional<molga::PixelRectU32> scissor;
};

// ── Task 11.2: 하나의 확정된 배치가 예약하는 위치 기록 수 ────────────────────
// 줄 수도 grapheme 수도 아니고, 그릴 수 있는 명령 수도 아니다. 배치된
// glyph/tofu 기록 전부다. 규칙이 한 벌인 것이 요점이다: 예약하는 쪽
// (UILayoutSystem)과 소비하는 쪽(CollectLayout)이 각자 세면, 합자와 공백에서
// 두 수가 어긋나고 그 어긋남은 다음 항목의 정렬 키가 이미 쓰인 뒤에 드러난다.
namespace molga::text {
std::uint64_t TextRenderCommandSpan(const TextLayout&) noexcept;
} // namespace molga::text

class TextRenderer;

// ── Task 8.2 Step 3a/7c: 월드 순회가 텍스트 컴포넌트에 건네는 권한 ───────────
// 컴포넌트가 TextRenderer::Get()을 스스로 부르면 프로세스에 두 번째 서비스가
// 생길 수 있고, 그 순간 UI와 월드가 서로 다른 캐시를 보게 된다. 그래서 바깥
// 소유자가 정확히 하나의 renderer/서비스/sink/기본 래스터 정책을 여기 담아
// 순회에 넘긴다.
struct WorldRenderCollectionContext {
    TextRenderer* textRenderer = nullptr;
    molga::text::TextDiagnosticSink* textDiagnostics = nullptr;
    TextRasterPolicy baseTextRasterPolicy;
    // 널 권한은 "이 world에 텍스트 컴포넌트가 없음을 이미 증명한" 픽스처에만
    // 허용된다. 프로덕션 출력은 언제나 진짜 권한을 채운다.
    static WorldRenderCollectionContext NonTextOnlyForTesting();
};

// ── Task 8.2: 한 문단이 프로덕션 경로로 들어갈 수 있는 원문 바이트 상한 ──────
// 런타임 문자열이 이 파이프라인에 들어오기 시작하는 것이 이 태스크다. 상한이
// 없으면 깨진 바이트 1 MB짜리 문자열 하나가 TextLayout::validationFacts에
// maximal subpart마다 사실 하나씩(문자열 셋을 단) 담아 수백 MB가 된다 —
// 진단 스트림은 kMaxLayoutDiagnosticsPerParagraph로 이미 묶여 있지만
// 사실 기록은 설계상 묶여 있지 않다(패키지 검증이 전부를 보아야 하므로).
//
// 그래서 상한은 사실 쪽이 아니라 입력 쪽에 건다. 잘라 내지 않고 거절한다:
// UTF-8 한가운데를 자르면 없던 ill-formed 바이트를 우리가 만들어 내게 된다.
inline constexpr std::size_t kMaxProductionTextBytes = 16U * 1024U;

// 한 번의 CollectLayout이 낼 수 있는 진단의 상한. 이웃한
// kMaxLayoutDiagnosticsPerParagraph / kMaxAtlasDiagnosticsPerCollection과 같은
// 규약이고 같은 이유다: glyph마다 진단을 내면 깨진 라벨 하나가 프레임마다
// 로그를 glyph 수만큼 채운다.
inline constexpr std::size_t kMaxCollectDiagnosticsPerLayout = 8;

// 셰이핑된 텍스트를 그리는 프로덕션 경로. UILabel과 TextRenderer2D가 공유하는
// 단 하나의 진입점이며, 레거시 codepoint/ASCII 경로는 존재하지 않는다.
class TextRenderer {
public:
    class GlyphCollectionScope;

    TextRenderer();
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // 준비된 텍스트 런타임 위에서 소유된 텍스트 서비스 한 벌을 세운다.
    // database는 프로세스가 소유하는 그 권한이며 이 renderer보다 오래 산다.
    //
    // sink는 이 호출 동안만 빌린다. 참조를 보관하면 나중의 배치가 초기화
    // 시점의 sink로 흘러들어, 프레임 진단이 엉뚱한 곳에 쌓인다.
    bool Init(const molga::AssetDatabase&, molga::text::TextDiagnosticSink&);

    // Task 6의 renderer idle/반납 drain이 성공한 다음에만 부른다. 수집이
    // 열려 있거나 밖으로 나간 page 토큰이 남아 있으면 아무것도 바꾸지 않고
    // ReferenceInvalid와 false다.
    bool ShutdownAfterGpuIdle(molga::text::TextDiagnosticSink&);

    // 이 renderer가 소유한 그 서비스. 두 번째 서비스를 세우면 UI와 월드가
    // 서로 다른 캐시를 보게 되므로, 소비자는 이 참조를 밖에서 받아 쓴다.
    molga::text::TextLayoutService& LayoutService() noexcept;

    std::optional<std::shared_ptr<const molga::text::TextLayout>> Layout(
        const molga::text::TextLayoutRequest&,
        molga::text::TextDiagnosticSink&);

    void CollectLayout(molga::RenderQueue&,
                       const molga::text::TextLayout&,
                       const TextCollectContext&,
                       molga::text::TextDiagnosticSink&);

    // ── Task 6.3: 한 프레임의 glyph 수집 ────────────────────────────────────
    // 프레임 번호가 정해진 다음, 월드/UI 텍스트를 큐에 담기 전에 연다. 이
    // scope가 살아 있는 동안 atlas가 내주는 page는 현재 수집의 pin을 받아
    // 축출되지 않고, scope가 닫히는 순간 밖으로 나간 page는 쓰기 봉인된다.
    // 그러므로 어휘적 범위는 모든 이른 return을 지나 "모든 명령이 page 토큰을
    // 복사한 뒤"까지 덮어야 한다.
    //
    // 중첩은 std::logic_error다. 두 번째 BeginFrame은 첫 수집의 pin 집합을
    // 통째로 지우므로, 이미 큐에 들어간 명령이 가리키는 page가 그 자리에서
    // 축출 대상이 된다 — 중첩을 조용히 무시하면 그 손실이 진단 없이 화면에만
    // 나타난다.
    GlyphCollectionScope BeginGlyphCollection(std::uint64_t frameIndex);

    // 이 renderer가 소유한 단 하나의 glyph 캐시. 두 접근자는 같은 객체를
    // 돌려주며, 프로세스 전역의 두 번째 캐시를 만들지 않는다.
    //
    // 가변 접근자는 시작 시 예산 설정과 초점 픽스처의 것이다. 보통의 렌더링은
    // CollectLayout을 통해서만 atlas에 닿는다. 자격/성능 쪽은 const 쪽으로
    // 텔레메트리만 읽는다.
    molga::GlyphAtlasCache& GlyphAtlas() noexcept;
    const molga::GlyphAtlasCache& GlyphAtlas() const noexcept;

    // ── Task 8.2 Step 3b: 프로세스 합성 루트, 정적 저장 수명이 아닌 소유자 ──
    // Meyers 싱글턴이면 이 객체의 소멸자가 main보다 뒤, 즉 텍스트 런타임
    // guard의 u_cleanup 다음에 돈다. 이 renderer는 이제 ICU/HarfBuzz를 쓰는
    // 서비스를 소유하므로 그것은 Task 2.2 Step 10a 위반이다. 그래서 인스턴스는
    // heap에 있고, 그 서비스는 main 안에서 ShutdownAfterGpuIdle이 부순다.
    //
    // UI/월드/Game View/런타임은 이것을 자기 바깥 소유자에서 한 번만 해석하고
    // 정확한 참조를 아래로 넘긴다. 컴포넌트나 시스템이 복제본을 만들거나
    // 두 번째 서비스를 캐시하거나 안에서 다시 싱글턴을 찾지 않는다.
    static TextRenderer& Get();
    // Get()이 만든 인스턴스를 놓는다. 프로세스 진입점이 GPU teardown 뒤,
    // guard가 u_cleanup을 돌리기 전에 정확히 한 번 부른다. 그 인스턴스가
    // 아닌 renderer에는 아무 영향이 없다.
    static void DestroyProcessInstance() noexcept;
    // ── Task 11.2 close-out: 만들지 않고 묻는다 ─────────────────────────────
    // Get()은 없으면 만든다. 그래서 종료 경로가 "부술 텍스트 renderer가
    // 있는가"를 Get()으로 물으면, 없던 프로세스에 하나를 만들어 놓고 그것을
    // 부수게 된다. 이 접근자는 만들지 않는다 — 그리고 등록되지 않은 GPU
    // 소비자를 종료가 찾아내는 유일한 길이다.
    static TextRenderer* ProcessInstanceOrNull() noexcept;

private:
    struct TextServices;

    // Init이 만들고 ShutdownAfterGpuIdle이 부순다. 멤버 역순 파괴가
    // layout/cache/shaper/resolver/repository 순서를 강제한다.
    std::unique_ptr<TextServices> services_;
    molga::GlyphAtlasCache atlas_;
    bool glyphCollectionActive_ = false;
};

// 한 프레임의 glyph 수집을 여는 RAII 손잡이. 옮길 수 있고 복사할 수 없다:
// 지분이 둘로 늘어나는 순간 EndCollection이 두 번 불리고, 그 두 번째가 *다음*
// 수집의 pin을 지운다.
class TextRenderer::GlyphCollectionScope {
public:
    GlyphCollectionScope(GlyphCollectionScope&&) noexcept;
    ~GlyphCollectionScope();
    GlyphCollectionScope(const GlyphCollectionScope&) = delete;
    GlyphCollectionScope& operator=(const GlyphCollectionScope&) = delete;
private:
    friend class TextRenderer;
    GlyphCollectionScope(TextRenderer&, std::uint64_t);
    TextRenderer* owner_ = nullptr;
    std::uint64_t frameIndex_ = 0;
};

// ── Task 6.3: 프로덕션 GPU teardown 순서는 여기 한 곳에만 있다 ──────────────
// 제출된 명령이 아직 읽고 있을지 모르는 텍스처를 부수지 않으려면 순서가
// 하나뿐이다: renderer가 GPU idle을 증명하고(Task 6.2 Step 8a) 반납 큐를
// 비운 다음에(Step 8b) 텍스트/atlas GPU 자원을 부수고, GraphicsDevice는 그
// 뒤에 죽는다.
//
// 두 진입점(src/main.cpp, src/runtime_main.cpp)이 이 함수만 부른다. 순서를
// 각자 적어 두면 한쪽만 되돌아가도 아무도 알아채지 못하지만, 여기 한 벌만
// 있으면 그 변경은 test_rendering_sdlgpu의 순서 케이스에서 실패한다.
//
// Task 8.2: 텍스트 서비스 파괴도 이 안에서 일어난다. 성공하면 그 renderer가
// 프로세스 인스턴스일 때 인스턴스 자체도 여기서 놓인다 — guard의 u_cleanup
// 뒤에 도는 소멸자를 남기지 않기 위해서다.
// ── Task 11.2 Step 7g: 순서는 여전히 이 함수 한 곳에만 있다 ────────────────
// 단계는 정확히 이 순서다.
//
//   1. 제출된 프레임을 닫고 GPU idle을 증명한다. 실패하면 **아무것도 부수지
//      않고** 거짓이다(Task 6.2의 std::abort()는 Task 11.2에서 사라졌다).
//   2. 완료된 GPU 수명 지분을 반납한다.
//   3. releaseInternalOwnersAfterDrain — host가 소유하는 엔진 쪽 강한 소유자
//      해제와 외부 소유자 검사. 이 자리가 계약이다: drain 뒤여야 하고
//      텍스트 teardown 앞이어야 한다. 거짓이면 아무것도 부수지 않고 거짓.
//      비어 있으면 건너뛴다(host 없는 픽스처가 그 모양이다).
//   4. 텍스트/atlas GPU 자원과 텍스트 서비스를 부순다.
//   5. 은퇴한 텍스처 바인딩을 부순다.
//   6. renderer의 GPU 자원을 부순다.
//
// GraphicsDevice 파괴는 여기 없다. 장치의 소유자는 host이고, host가 이
// 함수가 참을 돌려준 다음에만 부순다.
//
// 이미 idle을 증명한 renderer로 다시 부르면 1과 2를 건너뛴다 — 성공한 drain은
// 재시도를 건너 유지된다.
//
// deviceGeneration은 **호출자가 소유한 장치의 세대**다. 이 함수는 그 값을
// GraphicsDevice::Current()에서 다시 읽지 않는다: host는 impl.graphics로
// 세대를 이름하는데 여기서 프로세스 전역 Current()로 다시 읽으면 한 값에
// 두 권위가 생기고, Current()가 널이거나 다른 장치를 가리키는 순간 바인딩
// teardown 전체가 **조용히** 건너뛰어진다(그리고 host는 살아 있는 기록 위에서
// 자기 장치를 부순다). 0을 넘기는 것은 "장치 없음"이고, 그때는 부술 바인딩도
// 없다.
bool ShutdownRendererThenTextGpuResources(
    Renderer& renderer, TextRenderer& textRenderer,
    std::uint64_t deviceGeneration,
    molga::text::TextDiagnosticSink&,
    const std::function<bool()>& releaseInternalOwnersAfterDrain = nullptr);

namespace molga {
namespace detail {

// ── Task 6.3: scope가 수집을 정확히 한 번 닫는지 밖에서 관찰한다 ────────────
// GlyphAtlasCache::EndCollection은 멱등이다 — 두 번 부른 캐시와 한 번 부른
// 캐시는 상태가 같다. 그런데 scope가 지켜야 하는 계약은 바로 그 횟수다:
// 옮겨진 scope가 소유권을 놓지 않으면 두 번째 EndCollection이 *다음* 수집의
// pin을 지워, 그 프레임이 이미 가리키는 page가 축출 가능해진다. 그 차이는
// 캐시 상태로 드러나지 않으므로 호출 자체를 세야 한다.
//
// Renderer.h의 종료 단계 hook과 같은 이유로 출하되는 빌드에 남는다: 관찰
// 대상이 프로덕션 경로 그 자체다. 프로세스 전역이고 되돌릴 수 있으며,
// 기본값은 널이고 널이면 아무 일도 하지 않는다.
using GlyphCollectionEndHook = void (*)(std::uint64_t frameIndex);
void SetGlyphCollectionEndHookForTest(GlyphCollectionEndHook hook) noexcept;

// ── Task 8.2 Step 7f: ShutdownAfterGpuIdle 안의 순서를 밖에서 관찰한다 ──────
// 계약은 atlas_cleared < text_services_destroyed < u_cleanup이다. 앞의 두
// 사건은 함수 안에서만 일어나므로, 밖에서 두 번 표시를 찍으면 순서가 아니라
// 표시를 찍은 순서를 재게 된다. 위 hook과 같은 규칙이다.
using TextRendererShutdownStageHook = void (*)(const char* stage);
void SetTextRendererShutdownStageHookForTest(
    TextRendererShutdownStageHook hook) noexcept;

} // namespace detail
} // namespace molga
