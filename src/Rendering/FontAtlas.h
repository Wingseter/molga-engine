#pragma once

#include "Rendering/FontFace.h"
#include "Text/TextDiagnostic.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class Texture;

namespace molga {

// ── The glyph-ID atlas ──────────────────────────────────────────────────────
// 캐시 키는 codepoint가 아니라 셰이핑된 glyph ID + face 정체성이다. codepoint를
// 키로 쓰면 합자와 문맥 이형이 서로를 덮어쓴다: "fi" 합자도, 아랍어의 어두/어중
// 이형도 자기 codepoint를 갖고 있지 않고, 같은 codepoint가 face마다 전혀 다른
// 그림이 된다. 이 파일에서 그 대체가 일어난다.

// 지금은 하나뿐이지만 키에 남아 있다. 빼는 순간, 두 번째 모드가 추가될 때 서로
// 다른 그림이 같은 항목을 덮어쓰게 된다.
enum class GlyphRenderMode : std::uint8_t { Monochrome };
// pixelSize와 rasterScaleKey는 곱해져서 하나의 래스터 높이가 된다(64가 1.0인
// 26.6 배율). 따라서 (23,64)와 (46,32)는 바이트까지 같은 그림을 내면서 서로
// 다른 항목으로 저장된다. 키 모양은 설계가 못 박은 것이고 여기서 정규화하지
// 않으므로, 키를 만드는 쪽(Task 8.2의 레이아웃)이 한 가지 표기만 써야 한다:
// 같은 글자를 두 표기로 부르면 캐시와 예산이 조용히 두 배로 든다.
struct GlyphAtlasKey {
    std::string fontGuid;
    std::string fontRevision;
    std::uint32_t faceIndex = 0;
    std::uint16_t pixelSize = 0;
    std::uint16_t rasterScaleKey = 64;
    std::uint64_t variationKey = 0;
    GlyphRenderMode renderMode = GlyphRenderMode::Monochrome;
    std::uint32_t glyphId = 0;
    bool operator==(const GlyphAtlasKey&) const;
};
struct GlyphAtlasKeyHash {
    std::size_t operator()(const GlyphAtlasKey&) const noexcept;
};

// ── GlyphInfo가 무엇인지 (Task 6.2에서 확정) ────────────────────────────────
// GlyphInfo는 래스터 배치를 읽어 가는 값 view일 뿐, 수명 단위가 아니다.
// 이 하위 시스템에서 수명 단위는 오직 page 하나이며, 이름은
// GlyphHandle::pageIdentity이고 지분은 GlyphHandle::pageLifetime이다.
//
// 이 타입에는 한때 서로 다른 방향의 보증이 두 개 붙어 있었다. 지금은 지워진
// 레거시 codepoint 어댑터의 "page는 캐시 수명 내내 Texture를 유지한다"는
// 보증은 그 클래스의 성질이었지 이 타입의 성질이 아니었다 — 별칭 하나가
// 그렇게 읽히게 만들었을 뿐이고, 그 어댑터도 별칭도 Task 8.2에서 사라졌다.
// GlyphAtlasCache 쪽에는 그런 보증이 없다:
//
//  - `texture`는 page가 소유하는 원시 포인터다. 그것을 살려 두는 것은 이
//    구조체가 아니라 GlyphHandle::pageLifetime이다. handle을 버리고
//    GlyphInfo만 복사해 두면, 바로 그 순간 축출 조건(외부 토큰 만료)이
//    성립하므로 다음 할당이 그 텍스처를 해제할 수 있다. 새 소비자는 아래
//    RetainedTexture(handle)로만 텍스처를 꺼낸다 — 지분 없이 얻은 포인터는
//    돌려주지 않는다.
//  - `pageIndex`는 재사용되는 조밀한 slot 번호다(텍스처 배열/배치 바인딩용).
//    축출된 page의 자리는 다음 page가 그대로 물려받는다. 프로세스 수명 동안
//    재사용되지 않는 유일한 page 정체성은 GlyphHandle::pageIdentity이며,
//    fence 반납(Task 6.2/6.3)은 반드시 그쪽을 키로 써야 한다.
struct GlyphInfo {
    Texture* texture = nullptr;
    std::uint32_t glyphId = 0;
    int pageIndex = -1;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int width = 0, height = 0, xOffset = 0, yOffset = 0;
    bool drawable = false;
};

// 한 glyph 조회의 결과이자, 그 glyph가 놓인 page의 지분.
//
// `pageLifetime`은 이 handle을 들고 있는 쪽이 page를 살려 두는 유일한 수단이다.
// 캐시 자신의 소유권과 분리되어 있으므로, 캐시가 예산 때문에 page를 버려도
// 아직 제출되지 않은 명령이 가리키던 텍스처는 살아 있다. `pageIdentity`는
// 프로세스 수명 동안 재사용되지 않으므로, 늦게 도착한 fence가 같은 번호를 단
// 다른 page를 반납하는 일이 없다(Task 6.2/6.3).
//
// pin이 page를 *살려 둘* 뿐 *바꾸지 않게* 하지는 않던 구멍은 Task 6.2에서
// 닫혔다: 수집이 끝나는 순간 밖으로 나가 있는 page는 쓰기 봉인되고
// (IsPageWritable 참조), 그 프레임의 fence가 신호해 토큰이 풀릴 때까지
// GetGlyph는 그 텍스처에 새 glyph를 써 넣지 않는다.
//
// 세 가지 모양뿐이고 셋 다 pageIdentity와 pageLifetime이 짝을 이룬다:
// 그릴 수 있는 glyph(둘 다 있음), 공백 glyph와 포화 tofu(둘 다 없음).
// 한쪽만 있는 handle은 반납할 수 없거나 이름이 없는 지분이므로 만들지 않는다.
struct GlyphHandle {
    GlyphInfo glyph;
    std::uint64_t pageIdentity = 0;
    std::shared_ptr<const void> pageLifetime;
    bool proceduralTofu = false;
};

// handle이 실제로 page 지분을 들고 있을 때만 텍스처를 돌려준다. GlyphInfo만
// 복사해 둔 소비자는 이 질문을 할 수 없다 — 그것이 GlyphInfo가 수명 단위가
// 아니라는 말의 실제 내용이다.
Texture* RetainedTexture(const GlyphHandle& handle) noexcept;

struct GlyphAtlasTelemetry {
    std::uint64_t hits = 0, misses = 0, uploads = 0, evictions = 0;
    std::uint64_t residentBytes = 0, peakResidentBytes = 0;
};

// 한 수집(BeginFrame..EndCollection)이 atlas 자신의 이름으로 낼 수 있는
// AtlasExhausted 진단의 상한. 포화한 화면은 glyph마다 실패하므로, 상한이 없으면
// 라벨 하나가 프레임마다 로그를 glyph 수만큼 채운다.
//
// 상한을 넘겨도 정보는 잃지 않는다: 권한 있는 기록은 진단 스트림이 아니라
// 돌려주는 GlyphHandle의 proceduralTofu이고 그쪽에는 상한이 없다. 소비자와
// 패키지 검증은 그쪽을 읽는다(FontFamilyResolver.h의
// kMaxFamilyDiagnosticsPerResolve, TextShapingService.h의
// kMaxShapingDiagnosticsPerItem과 같은 규칙이다).
inline constexpr std::size_t kMaxAtlasDiagnosticsPerCollection = 8;

class GlyphAtlasCache;

namespace detail {

// 아래 두 개는 캐시의 불변식을 깨뜨릴 수 있는 변이자다: 정체성 할당기를 0으로
// 되돌리면 그 뒤의 모든 조회가 tofu가 되고, page당 cell 수를 묶으면 page 수와
// 메모리가 glyph 수만큼 불어난다. FontFace.h의 레거시 계수기와 같은 이유로
// 출하되는 빌드에 남지만(관찰 대상이 프로덕션 경로이므로), 클래스의 공개
// 표면이 아니라 detail에 둔다 — 프로덕션 코드가 실수로 손을 뻗을 자리에
// 놓아 둘 이유가 없다.
void SetGlyphAtlasNextPageIdentityForTest(GlyphAtlasCache& cache,
                                          std::uint64_t nextPageIdentity) noexcept;
void SetGlyphAtlasGlyphsPerPageForTest(GlyphAtlasCache& cache,
                                       std::size_t glyphsPerPage) noexcept;

} // namespace detail

// 셰이핑된 glyph ID로 주소를 매기는, 예산이 강제되는 page 캐시.
//
// 단일 thread 전용이다. 텍스트 처리는 main-thread deterministic CPU phase로
// 유지되고, 아래 상태에는 잠금이 없다.
class GlyphAtlasCache {
public:
    static constexpr std::uint64_t DefaultResidentBudgetBytes =
        64ULL * 1024ULL * 1024ULL;
    // 단색 coverage를 RGBA로 펼쳐 올린다. 배치 렌더러가 하나의 sprite 셰이더로
    // atlas quad를 그리므로 채널 수는 그 경로와 같아야 한다. 예산이 사는 것은
    // 픽셀 수가 아니라 바이트 수이므로, 64 MiB는 A8이었을 때의 4분의 1인
    // 1600만 glyph 픽셀이다.
    //
    // 상주 회계는 GPU page 한 벌만 센다. page는 그 위에 같은 크기의 CPU 원본
    // (AtlasPage::pixels)도 수명 내내 들고 있으므로 — 텍스처 생성이 지연되고
    // 모든 복사가 그 원본을 거치기 때문이다 — 예산이 가득 찬 프로세스의 실제
    // 발자국은 예산의 두 배다. 패키지 메모리 예산을 여기서 유도할 때 주의.
    static constexpr int kBytesPerPixel = 4;
    static constexpr int kDefaultPageSize = 1024;

    explicit GlyphAtlasCache(int pageSize = kDefaultPageSize);
    ~GlyphAtlasCache();

    GlyphAtlasCache(GlyphAtlasCache&&) noexcept;
    GlyphAtlasCache& operator=(GlyphAtlasCache&&) noexcept;

    GlyphAtlasCache(const GlyphAtlasCache&) = delete;
    GlyphAtlasCache& operator=(const GlyphAtlasCache&) = delete;

    // 0은 무제한이 아니라 용량 0이다. 무제한 모드는 존재하지 않는다.
    // 예산을 낮춰도 이미 상주하는 page를 그 자리에서 버리지는 않는다: 다음
    // 할당이 예산에 걸릴 때 LRU가 정리한다. 수집 중에 눈앞에서 page를 없애면
    // 이번 프레임이 이미 가리키고 있는 텍스처가 사라진다.
    //
    // 그래서 이 호출 직후에는 Telemetry().residentBytes가 새 예산보다 클 수
    // 있고, 모든 page가 pin되어 있으면 그 상태가 이어진다. 상한은 "새 page를
    // 들이는 순간"에 강제되는 것이지 여기서가 아니다.
    void SetResidentBudget(std::uint64_t bytes);
    void BeginFrame(std::uint64_t frameIndex);
    GlyphHandle GetGlyph(const GlyphAtlasKey&, const FontFace&,
                         molga::text::TextDiagnosticSink&);
    void EndCollection(std::uint64_t frameIndex);
    const GlyphAtlasTelemetry& Telemetry() const noexcept;

    // 비어 있지 않은 외부 page 토큰의 수. 캐시 자신의 소유권과 현재 수집의
    // pin은 세지 않는다: 그 둘은 "GPU가 아직 쓰고 있다"의 증거가 아니다.
    std::size_t LiveExternalPagePinCount() const noexcept;

    // 장치 안전 해제. 수집이 열려 있거나 외부 pin이 하나라도 남아 있으면
    // 아무것도 건드리지 않고 false다. Task 11은 device idle/fence drain이
    // 성공하고 엔진의 명령/스냅숏 소유자가 풀린 다음에만 이것을 부른다.
    bool ReleaseAfterGpuIdle() noexcept;

    // ── 관찰용 접근자 ────────────────────────────────────────────────────
    // page 회계와 정체성 할당기는 이 접근자들 없이는 밖에서 관찰되지 않는다.
    // 관찰이 없으면 "예산을 넘지 않는다"와 "정체성을 재사용하지 않는다"는
    // 주석에 적힌 주장으로만 남는다.
    bool IsPageResident(std::uint64_t pageIdentity) const noexcept;

    // ── Task 8.2: GetGlyph가 불린 횟수 ───────────────────────────────────
    // "없는 family는 atlas에 닿지 않는다"는 주장은 이 값으로만 관찰된다.
    // Telemetry의 hits/misses로는 부족하다 — 조회가 캐시 적중도 실패도 아닌
    // 이른 반환으로 끝나면 두 값 다 움직이지 않으므로, 닿았는지 자체가
    // 보이지 않는다. 호출 직전에 올린다.
    std::uint64_t LookupCountForTest() const noexcept;

    // 이 page에 새 glyph를 써 넣어도 되는가. 상주하지 않으면 false다.
    //
    // 끝난 수집이 밖으로 내보낸 page는 그 토큰이 풀릴 때까지 false다: 제출된
    // 명령이 읽고 있는 텍스처에 in-place로 쓰면 화면에 뜯긴 glyph가 나오고,
    // 어떤 진단도 그것을 보고하지 않는다. 이 값은 상주/축출과 무관하다 —
    // 봉인된 page도 계속 캐시 hit을 낸다.
    bool IsPageWritable(std::uint64_t pageIdentity) const noexcept;
    std::size_t ResidentPageCount() const noexcept;
    std::uint64_t PageBytes() const noexcept;
    const GlyphAtlasKey& LastUploadedKeyForTest() const noexcept;

    // page의 CPU 원본에서 한 픽셀의 coverage(알파)를 읽는다. 상주하지 않는
    // page나 범위 밖 좌표는 0이다. 이것 없이는 이 캐시의 유일한 책임 — 래스터
    // 이미지를 page에 담는 것 — 만 관찰되지 않는다: 헤드리스 테스트에는
    // GraphicsDevice가 없어 Texture가 만들어지지 않으므로, GPU 쪽으로는 복사가
    // 통째로 사라져도 어떤 단언도 움직이지 않는다.
    unsigned char PageCoverageForTest(std::uint64_t pageIdentity, int x,
                                      int y) const noexcept;

private:
    friend void detail::SetGlyphAtlasNextPageIdentityForTest(
        GlyphAtlasCache&, std::uint64_t) noexcept;
    friend void detail::SetGlyphAtlasGlyphsPerPageForTest(GlyphAtlasCache&,
                                                          std::size_t) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace molga
