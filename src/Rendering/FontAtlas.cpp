#include "Rendering/FontAtlas.h"

#include "Core/AssetDatabase.h"
#include "Rendering/Texture.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace molga {
namespace {

struct CacheKey {
    std::string guid;
    int pixelSize = 0;

    bool operator==(const CacheKey& other) const {
        return pixelSize == other.pixelSize && guid == other.guid;
    }
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey& key) const {
        const std::size_t guidHash = std::hash<std::string>{}(key.guid);
        const std::size_t sizeHash = std::hash<int>{}(key.pixelSize);
        return guidHash ^ (sizeHash + 0x9e3779b9U + (guidHash << 6U) + (guidHash >> 2U));
    }
};

int NormalizePixelSize(int pixelSize) {
    return std::max(1, std::min(pixelSize, 512));
}

bool CanCreateTextures() {
    return GraphicsDevice::Current() != nullptr;
}

// 두 캐시가 같은 shelf packer와 같은 RGBA page 표현을 공유한다. 레거시
// codepoint 어댑터가 Task 8.2에서 사라질 때 남는 것은 이 한 벌이다.
struct AtlasPage {
    explicit AtlasPage(int requestedSize)
        : size(requestedSize), pixels(static_cast<std::size_t>(size) *
                                      static_cast<std::size_t>(size) * 4U, 0U) {}

    bool TryPlace(int width, int height, int& outX, int& outY) {
        constexpr int padding = 1;
        if (width + padding * 2 > size || height + padding * 2 > size) {
            return false;
        }
        if (cursorX + width + padding > size) {
            cursorX = padding;
            cursorY += rowHeight + padding;
            rowHeight = 0;
        }
        if (cursorY + height + padding > size) return false;

        outX = cursorX;
        outY = cursorY;
        cursorX += width + padding;
        rowHeight = std::max(rowHeight, height);
        return true;
    }

    void CopyCoverage(int x, int y, const FontGlyphBitmap& bitmap) {
        for (int row = 0; row < bitmap.height; ++row) {
            for (int column = 0; column < bitmap.width; ++column) {
                const std::size_t source = static_cast<std::size_t>(row) *
                                           static_cast<std::size_t>(bitmap.width) +
                                           static_cast<std::size_t>(column);
                const std::size_t destination =
                    (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(size) +
                     static_cast<std::size_t>(x + column)) * 4U;
                pixels[destination + 0U] = 255U;
                pixels[destination + 1U] = 255U;
                pixels[destination + 2U] = 255U;
                pixels[destination + 3U] = bitmap.coverage[source];
            }
        }

        if (texture) {
            std::vector<unsigned char> rgba(
                static_cast<std::size_t>(bitmap.width) *
                static_cast<std::size_t>(bitmap.height) * 4U);
            for (std::size_t index = 0; index < bitmap.coverage.size(); ++index) {
                rgba[index * 4U + 0U] = 255U;
                rgba[index * 4U + 1U] = 255U;
                rgba[index * 4U + 2U] = 255U;
                rgba[index * 4U + 3U] = bitmap.coverage[index];
            }
            texture->UpdateSubData(x, y, bitmap.width, bitmap.height, rgba.data(), 4);
        }
    }

    Texture* EnsureTexture() {
        if (!texture && CanCreateTextures()) {
            texture = std::make_unique<Texture>(size, size, pixels.data(), 4);
        }
        return texture.get();
    }

    int size = 0;
    int cursorX = 1;
    int cursorY = 1;
    int rowHeight = 0;
    std::vector<unsigned char> pixels;
    std::unique_ptr<Texture> texture;
};

// 레거시 항목 하나. GlyphInfo가 논리 advance를 담지 않으므로, 아직 셰이퍼가
// 없는 codepoint 경로를 위해 래스터 advance를 옆에 둔다.
struct LegacyGlyphEntry {
    FontAtlasGlyph info;
    float xAdvance = 0.0f;
};

struct CachedFontSize {
    FontFace face;
    FontFaceMetrics metrics;
    std::vector<AtlasPage> pages;
    std::unordered_map<std::uint32_t, LegacyGlyphEntry> glyphs;
};

// ── Glyph atlas page ownership ──────────────────────────────────────────────
// page 자원의 소유자는 정확히 둘이고 서로 독립이다.
//
//   1) PageRecord::resource   — 캐시 자신의 소유권. 축출을 막지 않는다.
//   2) PageLifetimeToken      — page당 정확히 하나인 외부 소유권.
//
// 기록은 토큰을 weak_ptr로만 붙든다. 그래서 "밖에서 아직 쓰고 있는가"는
// 토큰의 만료 여부 하나로 판정되고, PageResource의 shared_ptr use_count()를
// 읽을 필요도 이유도 없다 — use_count()는 캐시 자신의 지분까지 세므로 언제나
// 1 이상이고, 그것을 pin으로 오해하면 어떤 page도 영원히 축출되지 않는다.
struct PageLifetimeToken {
    explicit PageLifetimeToken(std::shared_ptr<AtlasPage> owned)
        : page(std::move(owned)) {}
    std::shared_ptr<AtlasPage> page;
};

struct PageRecord {
    std::shared_ptr<AtlasPage> resource;
    std::weak_ptr<const PageLifetimeToken> externalToken;
    std::uint64_t lastTouch = 0;
    std::size_t glyphsPlaced = 0;
    // 조밀한 자리 번호(GlyphInfo::pageIndex). 축출된 page의 자리는 다음 page가
    // 물려받는다 — 절대 재사용되지 않는 page 정체성과 반대다. 자리 번호를
    // 정체성으로 오해하면 늦게 도착한 fence가 같은 자리에 앉은 다른 page를
    // 반납한다.
    std::size_t slot = 0;
    // 이 page가 죽을 때 함께 무효화되어야 하는 항목들. 이것이 없으면 축출된
    // page를 가리키는 항목이 캐시에 남아 다음 hit에서 죽은 텍스처를 돌려준다.
    std::vector<GlyphAtlasKey> keys;
};

struct GlyphEntry {
    GlyphInfo info;
    std::uint64_t pageIdentity = 0;
};

// FNV-1a를 손으로 접는다. std::hash와 달리 값이 표준 라이브러리 구현이나
// 실행에 따라 달라지지 않으므로, 같은 입력이 언제나 같은 버킷 배치를 만든다 —
// 이 하위 시스템의 계약이 결정성이고, 캐시 구조에 얽힌 버그는 재현 실행이
// 같은 배치를 가져야만 재현된다.
constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void FoldByte(std::uint64_t& state, std::uint8_t byte) {
    state ^= static_cast<std::uint64_t>(byte);
    state *= kFnvPrime;
}

void FoldInteger(std::uint64_t& state, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        FoldByte(state, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

// 길이를 먼저 접는다. 그러지 않으면 인접한 두 문자열의 경계가 사라져
// ("ab","c")와 ("a","bc")가 같은 값이 된다.
void FoldString(std::uint64_t& state, const std::string& text) {
    FoldInteger(state, static_cast<std::uint64_t>(text.size()));
    for (char character : text) {
        FoldByte(state, static_cast<std::uint8_t>(character));
    }
}

} // namespace

// ── GlyphAtlasKey ───────────────────────────────────────────────────────────

bool GlyphAtlasKey::operator==(const GlyphAtlasKey& other) const {
    return faceIndex == other.faceIndex && pixelSize == other.pixelSize &&
           rasterScaleKey == other.rasterScaleKey &&
           variationKey == other.variationKey && renderMode == other.renderMode &&
           glyphId == other.glyphId && fontGuid == other.fontGuid &&
           fontRevision == other.fontRevision;
}

std::size_t GlyphAtlasKeyHash::operator()(const GlyphAtlasKey& key) const noexcept {
    std::uint64_t state = kFnvOffsetBasis;
    FoldString(state, key.fontGuid);
    FoldString(state, key.fontRevision);
    FoldInteger(state, key.faceIndex);
    FoldInteger(state, key.pixelSize);
    FoldInteger(state, key.rasterScaleKey);
    FoldInteger(state, key.variationKey);
    FoldInteger(state, static_cast<std::uint64_t>(key.renderMode));
    FoldInteger(state, key.glyphId);
    return static_cast<std::size_t>(state);
}

// ── GlyphAtlasCache ─────────────────────────────────────────────────────────

struct GlyphAtlasCache::Impl {
    explicit Impl(int requestedPageSize)
        : pageSize(std::max(16, requestedPageSize)),
          pageBytes(static_cast<std::uint64_t>(pageSize) *
                    static_cast<std::uint64_t>(pageSize) *
                    static_cast<std::uint64_t>(kBytesPerPixel)) {}

    void ReportExhausted(text::TextDiagnosticSink& sink,
                         const GlyphAtlasKey& key, const char* reason) {
        if (diagnosticsThisCollection >= kMaxAtlasDiagnosticsPerCollection) return;
        ++diagnosticsThisCollection;
        text::TextDiagnostic diagnostic;
        diagnostic.code = text::TextDiagnosticCode::AtlasExhausted;
        diagnostic.severity = text::TextSeverity::Warning;
        diagnostic.subsystem = "glyph-atlas";
        diagnostic.message =
            std::string("no atlas page could hold glyph ") +
            std::to_string(key.glyphId) + " of face " +
            std::to_string(key.faceIndex) + " at " +
            std::to_string(key.pixelSize) + "px in frame " +
            std::to_string(currentFrame) + ": " + reason;
        diagnostic.remediation =
            "raise the glyph atlas resident budget, or reduce the number of "
            "distinct fonts, sizes and glyphs drawn in one frame";
        diagnostic.assetGuid = key.fontGuid;
        diagnostic.componentType = "FontAsset";
        sink.Report(std::move(diagnostic));
    }

    bool AtCellLimit(const PageRecord& record) const {
        return glyphsPerPage != 0U && record.glyphsPlaced >= glyphsPerPage;
    }

    // 축출 가능한 조건은 정확히 둘이다: 이번 수집이 pin하지 않았고, 외부 토큰이
    // 만료되었다. 캐시 자신의 소유권은 조건에 없다.
    bool IsEvictable(std::uint64_t pageIdentity, const PageRecord& record) const {
        return currentCollectionPins.count(pageIdentity) == 0U &&
               record.externalToken.expired();
    }

    bool EvictLeastRecentlyUsed() {
        auto victim = pages.end();
        for (auto iterator = pages.begin(); iterator != pages.end(); ++iterator) {
            if (!IsEvictable(iterator->first, iterator->second)) continue;
            if (victim == pages.end() ||
                iterator->second.lastTouch < victim->second.lastTouch) {
                victim = iterator;
            }
        }
        if (victim == pages.end()) return false;

        for (const GlyphAtlasKey& key : victim->second.keys) glyphs.erase(key);
        slots[victim->second.slot] = 0U;
        if (openPage == victim->first) openPage = 0U;
        pages.erase(victim);
        // 파괴 직후에 회계를 갱신한다. 사이에 다른 할당이 끼면 최고 수위가
        // 실제로는 존재하지 않았던 상주량을 기록한다.
        telemetry.residentBytes -= pageBytes;
        ++telemetry.evictions;
        return true;
    }

    bool HasRoomForAnotherPage() const {
        return telemetry.residentBytes < residentBudget &&
               pageBytes <= residentBudget - telemetry.residentBytes;
    }

    std::size_t AllocateSlot(std::uint64_t pageIdentity) {
        for (std::size_t slot = 0; slot < slots.size(); ++slot) {
            if (slots[slot] == 0U) {
                slots[slot] = pageIdentity;
                return slot;
            }
        }
        slots.push_back(pageIdentity);
        return slots.size() - 1U;
    }

    // 새 page 하나. 실패하면 아무것도 바꾸지 않는다.
    PageRecord* CreatePage(text::TextDiagnosticSink& sink,
                           const GlyphAtlasKey& key,
                           std::uint64_t& outIdentity) {
        // 정체성 소진을 증분/wrap보다 먼저 본다. 0으로 되돌리거나 재사용하면
        // 늦게 도착한 fence가 같은 번호를 단 다른 page를 반납하게 된다.
        if (nextPageIdentity == 0U) {
            ReportExhausted(sink, key, "page identities are exhausted");
            return nullptr;
        }
        if (pageBytes > residentBudget) {
            ReportExhausted(sink, key, "one page is larger than the resident budget");
            return nullptr;
        }
        if (!HasRoomForAnotherPage()) {
            // 먼저 자리가 날 수 있는지 세어 본다. 곧바로 축출부터 하면, 결국
            // tofu로 끝나는 요청 하나가 살아 있던 page들을 아무 대가 없이
            // 버리고, 다음 프레임이 그 전부를 다시 올린다.
            //
            // 부족분은 상주량에서 직접 뺀다. 남은 여유(예산 - 상주량)에서 빼면
            // SetResidentBudget이 상주량 밑으로 예산을 낮춘 뒤 그 여유가 0으로
            // 바닥나서, 실제로는 여러 page가 모자란 상황을 "한 page 모자람"으로
            // 과소평가한다. 그러면 위 판정이 통과해 버리고, 정확히 막으려던
            // 그 일 — 결국 거절될 요청이 살아 있던 page를 버리는 것 — 이 난다.
            const std::uint64_t neededBytes =
                (telemetry.residentBytes + pageBytes) - residentBudget;
            std::uint64_t reclaimableBytes = 0U;
            for (const auto& entry : pages) {
                if (IsEvictable(entry.first, entry.second)) reclaimableBytes += pageBytes;
            }
            if (reclaimableBytes < neededBytes) {
                ReportExhausted(sink, key,
                                reclaimableBytes == 0U
                                    ? "every resident page is pinned"
                                    : "freeing every unpinned page would still "
                                      "not fit this page inside the resident "
                                      "budget");
                return nullptr;
            }
            while (!HasRoomForAnotherPage()) {
                // page 크기가 균일하므로 위 계산이 이미 충분함을 보장한다.
                // 그래도 실패를 닫아 둔다: 열어 두면 예산이나 축출 규칙이
                // 바뀌는 날 이 루프가 끝나지 않는다.
                if (!EvictLeastRecentlyUsed()) {
                    ReportExhausted(sink, key, "every resident page is pinned");
                    return nullptr;
                }
            }
        }

        const std::uint64_t identity = nextPageIdentity;
        if (identity == UINT64_MAX) {
            nextPageIdentity = 0U; // 0은 유효한 정체성이 아니므로 소진 표식이 된다.
        } else {
            ++nextPageIdentity;
        }

        PageRecord record;
        record.resource = std::make_shared<AtlasPage>(pageSize);
        record.lastTouch = ++touchCounter;
        record.slot = AllocateSlot(identity);
        telemetry.residentBytes += pageBytes;
        telemetry.peakResidentBytes =
            std::max(telemetry.peakResidentBytes, telemetry.residentBytes);
        outIdentity = identity;
        return &pages.emplace(identity, std::move(record)).first->second;
    }

    // page당 정확히 하나인 외부 토큰을 돌려주거나 새로 만든다. 같은 프레임에서
    // 같은 page를 쓰는 glyph 수백 개가 저마다 토큰을 만들면, Task 6.2가 fence
    // 하나에 매달아야 할 반납 대상이 page 수가 아니라 glyph 수가 된다.
    std::shared_ptr<const void> PinPage(std::uint64_t pageIdentity,
                                        PageRecord& record) {
        if (collectionActive) currentCollectionPins.insert(pageIdentity);
        std::shared_ptr<const PageLifetimeToken> token = record.externalToken.lock();
        if (!token) {
            token = std::make_shared<const PageLifetimeToken>(record.resource);
            record.externalToken = token;
        }
        return token;
    }

    int pageSize = GlyphAtlasCache::kDefaultPageSize;
    std::uint64_t pageBytes = 0;
    std::uint64_t residentBudget = GlyphAtlasCache::DefaultResidentBudgetBytes;
    std::size_t glyphsPerPage = 0;
    std::uint64_t nextPageIdentity = 1;
    std::uint64_t touchCounter = 0;
    std::uint64_t currentFrame = 0;
    // 아직 선반이 남은 page. 프레임 경계에서 비우지 않는다 — 비우면 프레임마다
    // 새 page가 열려 1024짜리 page가 glyph 몇 개만 담고 예산을 태운다.
    //
    // 그 대가로: pin은 page를 살려 둘 뿐 바꾸지 않게 하지는 않는다. 프레임 N이
    // 제출한 page에 프레임 N+1의 새 glyph가 곧바로 써 넣어질 수 있고,
    // CopyCoverage는 살아 있는 텍스처에 즉시 업로드한다. 제출된 프레임 밑에서
    // 일어나는 그 write-after-read를 막는 것(제출 중인 page는 더 채우지 않거나
    // 프레임별 쓰기 영역을 분리하는 것)은 GPU 제출 소유권을 맡는 Task 6.2의
    // 몫이며, GlyphHandle::pageLifetime 하나로는 막히지 않는다.
    std::uint64_t openPage = 0;
    bool collectionActive = false;
    std::size_t diagnosticsThisCollection = 0;
    GlyphAtlasTelemetry telemetry;
    GlyphAtlasKey lastUploadedKey;
    // page 정체성 순서로 정렬된 컨테이너. LiveExternalPagePinCount와 LRU 선택이
    // 해시 순회 순서에 흔들리면 같은 입력이 실행마다 다른 page를 버린다.
    std::map<std::uint64_t, PageRecord> pages;
    std::vector<std::uint64_t> slots;
    std::set<std::uint64_t> currentCollectionPins;
    std::unordered_map<GlyphAtlasKey, GlyphEntry, GlyphAtlasKeyHash> glyphs;
};

GlyphAtlasCache::GlyphAtlasCache(int pageSize)
    : impl_(std::make_unique<Impl>(pageSize)) {}
GlyphAtlasCache::~GlyphAtlasCache() = default;
GlyphAtlasCache::GlyphAtlasCache(GlyphAtlasCache&&) noexcept = default;
GlyphAtlasCache& GlyphAtlasCache::operator=(GlyphAtlasCache&&) noexcept = default;

void GlyphAtlasCache::SetResidentBudget(std::uint64_t bytes) {
    impl_->residentBudget = bytes;
}

void GlyphAtlasCache::BeginFrame(std::uint64_t frameIndex) {
    impl_->currentFrame = frameIndex;
    impl_->collectionActive = true;
    impl_->currentCollectionPins.clear();
    impl_->diagnosticsThisCollection = 0U;
}

void GlyphAtlasCache::EndCollection(std::uint64_t frameIndex) {
    // 번호가 어긋나도 수집은 닫는다. 열어 둔 채로 두면 이 프레임의 pin이
    // 영원히 축출을 막아, 예산이 강제되지 않는 상태로 조용히 넘어간다.
    impl_->currentFrame = frameIndex;
    impl_->collectionActive = false;
    impl_->currentCollectionPins.clear();
}

GlyphHandle GlyphAtlasCache::GetGlyph(const GlyphAtlasKey& key,
                                      const FontFace& face,
                                      text::TextDiagnosticSink& sink) {
    Impl& state = *impl_;
    GlyphHandle handle;

    auto found = state.glyphs.find(key);
    if (found != state.glyphs.end()) {
        ++state.telemetry.hits;
        GlyphEntry& entry = found->second;
        handle.glyph = entry.info;
        if (entry.pageIdentity != 0U) {
            PageRecord& record = state.pages.at(entry.pageIdentity);
            record.lastTouch = ++state.touchCounter;
            handle.glyph.texture = record.resource->EnsureTexture();
            entry.info.texture = handle.glyph.texture;
            handle.pageIdentity = entry.pageIdentity;
            handle.pageLifetime = state.PinPage(entry.pageIdentity, record);
        }
        return handle;
    }

    ++state.telemetry.misses;
    // 래스터와 업로드는 이 호출 안에서 끝난다. 원시 FontFace& 를 나중에 쓰려고
    // 큐에 넣지 않는다 — face는 이 캐시의 것이 아니고, hot reload가 그 밑에서
    // 바이트를 갈아 끼울 수 있다. 지연 작업이 필요한 backend가 생기면 그쪽
    // 대기 기록이 FontFaceResourcePtr을 직접 붙들어야 한다.
    //
    // renderMode는 아직 Monochrome 하나뿐이라 래스터 경로를 가르지 않는다.
    // 그래도 키에는 남아 있다: 두 번째 모드가 생기는 날 서로 다른 그림이 같은
    // 항목을 덮어쓰지 않기 위해서다.
    const FontGlyphBitmap bitmap =
        face.RasterizeGlyph(key.glyphId, key.pixelSize, key.rasterScaleKey);

    GlyphEntry entry;
    entry.info.glyphId = key.glyphId;
    entry.info.width = bitmap.width;
    entry.info.height = bitmap.height;
    entry.info.xOffset = bitmap.xOffset;
    entry.info.yOffset = bitmap.yOffset;

    if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.coverage.empty()) {
        // 공백 glyph(space, 기본 무시 문자)에는 올릴 픽셀이 없다. page도
        // upload도 없지만 조회 결과는 유효하므로 tofu가 아니다.
        //
        // 어떤 page에도 속하지 않으므로 LRU가 이 항목을 거두지 않고, 상주
        // 예산도 이것을 세지 않는다. 캐시에서 상한 없는 부분은 여기뿐이다.
        // 실질적으로는 (폰트 x 크기)마다 공백 문자 몇 개이므로 수백 KB를 넘지
        // 않고, ReleaseAfterGpuIdle이 함께 지운다.
        state.glyphs.emplace(key, entry);
        handle.glyph = entry.info;
        return handle;
    }

    auto tofu = [&key]() {
        // 포화 tofu. 항목을 남기지 않으므로, 예산이 풀린 다음 프레임이 같은
        // glyph를 다시 시도할 수 있다.
        GlyphHandle saturated;
        saturated.glyph.glyphId = key.glyphId;
        saturated.proceduralTofu = true;
        return saturated;
    };

    constexpr int kPagePadding = 1;
    if (bitmap.width + kPagePadding * 2 > state.pageSize ||
        bitmap.height + kPagePadding * 2 > state.pageSize) {
        // page보다 큰 glyph는 새 page를 만들어도 들어가지 않는다. 만들기 전에
        // 거절해야 상주 page 하나를 헛되이 버리지 않는다.
        state.ReportExhausted(sink, key, "the glyph is larger than one atlas page");
        return tofu();
    }

    std::uint64_t pageIdentity = 0U;
    PageRecord* target = nullptr;
    int x = 0;
    int y = 0;
    if (state.openPage != 0U) {
        auto open = state.pages.find(state.openPage);
        if (open != state.pages.end() && !state.AtCellLimit(open->second) &&
            open->second.resource->TryPlace(bitmap.width, bitmap.height, x, y)) {
            pageIdentity = open->first;
            target = &open->second;
        }
    }
    if (!target) {
        target = state.CreatePage(sink, key, pageIdentity);
        if (!target) return tofu();
        if (!target->resource->TryPlace(bitmap.width, bitmap.height, x, y)) {
            state.ReportExhausted(sink, key, "the glyph is larger than one atlas page");
            return tofu();
        }
        state.openPage = pageIdentity;
    }

    target->resource->CopyCoverage(x, y, bitmap);
    ++target->glyphsPlaced;
    target->keys.push_back(key);
    target->lastTouch = ++state.touchCounter;

    const float pageExtent = static_cast<float>(target->resource->size);
    entry.pageIdentity = pageIdentity;
    entry.info.pageIndex = static_cast<int>(target->slot);
    entry.info.u0 = static_cast<float>(x) / pageExtent;
    entry.info.v0 = static_cast<float>(y) / pageExtent;
    entry.info.u1 = static_cast<float>(x + bitmap.width) / pageExtent;
    entry.info.v1 = static_cast<float>(y + bitmap.height) / pageExtent;
    entry.info.texture = target->resource->EnsureTexture();
    entry.info.drawable = true;

    ++state.telemetry.uploads;
    state.lastUploadedKey = key;
    state.glyphs.emplace(key, entry);

    handle.glyph = entry.info;
    handle.pageIdentity = pageIdentity;
    handle.pageLifetime = state.PinPage(pageIdentity, *target);
    return handle;
}

const GlyphAtlasTelemetry& GlyphAtlasCache::Telemetry() const noexcept {
    return impl_->telemetry;
}

std::size_t GlyphAtlasCache::LiveExternalPagePinCount() const noexcept {
    std::size_t live = 0U;
    for (const auto& entry : impl_->pages) {
        if (!entry.second.externalToken.expired()) ++live;
    }
    return live;
}

bool GlyphAtlasCache::ReleaseAfterGpuIdle() noexcept {
    // 판정이 먼저, 변이는 그다음이다. 거절된 호출이 절반만 해제하면 호출자는
    // false를 보고도 이미 사라진 텍스처를 가리키고 있게 된다.
    if (impl_->collectionActive) return false;
    if (LiveExternalPagePinCount() != 0U) return false;

    impl_->glyphs.clear();
    impl_->pages.clear();
    impl_->slots.clear();
    impl_->currentCollectionPins.clear();
    impl_->openPage = 0U;
    impl_->telemetry.residentBytes = 0U;
    // 단조 telemetry(hits/misses/uploads/evictions/peak)와 절대 재사용되지 않는
    // page 정체성 할당기는 남긴다. 해제는 프로세스 재시작이 아니다.
    return true;
}

bool GlyphAtlasCache::IsPageResident(std::uint64_t pageIdentity) const noexcept {
    return impl_->pages.find(pageIdentity) != impl_->pages.end();
}

std::size_t GlyphAtlasCache::ResidentPageCount() const noexcept {
    return impl_->pages.size();
}

std::uint64_t GlyphAtlasCache::PageBytes() const noexcept {
    return impl_->pageBytes;
}

const GlyphAtlasKey& GlyphAtlasCache::LastUploadedKeyForTest() const noexcept {
    return impl_->lastUploadedKey;
}

unsigned char GlyphAtlasCache::PageCoverageForTest(std::uint64_t pageIdentity,
                                                   int x, int y) const noexcept {
    const auto found = impl_->pages.find(pageIdentity);
    if (found == impl_->pages.end()) return 0U;
    const AtlasPage& page = *found->second.resource;
    if (x < 0 || y < 0 || x >= page.size || y >= page.size) return 0U;
    // 알파 채널이 coverage다. RGB는 언제나 255로 채워진다.
    return page.pixels[(static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(page.size) +
                        static_cast<std::size_t>(x)) *
                           4U +
                       3U];
}

namespace detail {

void SetGlyphAtlasNextPageIdentityForTest(
    GlyphAtlasCache& cache, std::uint64_t nextPageIdentity) noexcept {
    cache.impl_->nextPageIdentity = nextPageIdentity;
}

void SetGlyphAtlasGlyphsPerPageForTest(GlyphAtlasCache& cache,
                                       std::size_t glyphsPerPage) noexcept {
    cache.impl_->glyphsPerPage = glyphsPerPage;
}

} // namespace detail

// ── Legacy codepoint adapter ────────────────────────────────────────────────

struct FontAtlasCache::Impl {
    explicit Impl(int requestedPageSize)
        : pageSize(std::max(16, requestedPageSize)) {}

    CachedFontSize* FindOrLoad(const std::string& guid, int requestedPixelSize) {
        if (guid.empty()) return nullptr;
        const CacheKey key{guid, NormalizePixelSize(requestedPixelSize)};
        auto found = caches.find(key);
        if (found != caches.end()) return found->second.get();

        const std::filesystem::path path = AssetDatabase::Get().AbsoluteSourcePath(guid);
        if (path.empty()) {
            // Cache failures as well: a broken scene must not hit the file
            // system twice per label on every frame. Asset reimport/project
            // scans invalidate the renderer cache before retrying.
            caches.emplace(key, nullptr);
            return nullptr;
        }

        auto cached = std::make_unique<CachedFontSize>();
        if (!cached->face.LoadFromFile(path)) {
            caches.emplace(key, nullptr);
            return nullptr;
        }
        cached->metrics = cached->face.Metrics(static_cast<float>(key.pixelSize));
        CachedFontSize* result = cached.get();
        caches.emplace(key, std::move(cached));
        return result;
    }

    const CachedFontSize* Find(const std::string& guid, int requestedPixelSize) const {
        const CacheKey key{guid, NormalizePixelSize(requestedPixelSize)};
        const auto found = caches.find(key);
        return found == caches.end() ? nullptr : found->second.get();
    }

    // 두 공개 진입점이 같은 항목을 본다. 갈라지면 measure와 draw가 서로 다른
    // advance를 쓰게 되고, 그 어긋남은 커서 위치로만 드러난다.
    LegacyGlyphEntry* FindOrRasterize(const std::string& fontGuid, int pixelSize,
                                      std::uint32_t codepoint) {
        CachedFontSize* cached = FindOrLoad(fontGuid, pixelSize);
        if (!cached) return nullptr;

        auto found = cached->glyphs.find(codepoint);
        if (found != cached->glyphs.end()) {
            LegacyGlyphEntry& entry = found->second;
            if (entry.info.pageIndex >= 0) {
                entry.info.texture =
                    cached->pages[static_cast<std::size_t>(entry.info.pageIndex)]
                        .EnsureTexture();
            }
            return &entry;
        }

        const FontGlyphBitmap bitmap = cached->face.Rasterize(
            codepoint, static_cast<float>(NormalizePixelSize(pixelSize)));
        LegacyGlyphEntry entry;
        entry.info.width = bitmap.width;
        entry.info.height = bitmap.height;
        entry.info.xOffset = bitmap.xOffset;
        entry.info.yOffset = bitmap.yOffset;
        entry.xAdvance = bitmap.xAdvance;

        if (bitmap.width > 0 && bitmap.height > 0 && !bitmap.coverage.empty()) {
            if (cached->pages.empty()) cached->pages.emplace_back(pageSize);
            int x = 0;
            int y = 0;
            if (!cached->pages.back().TryPlace(bitmap.width, bitmap.height, x, y)) {
                cached->pages.emplace_back(pageSize);
                if (!cached->pages.back().TryPlace(bitmap.width, bitmap.height, x, y)) {
                    cached->pages.pop_back();
                    return &cached->glyphs.emplace(codepoint, entry).first->second;
                }
            }

            AtlasPage& page = cached->pages.back();
            page.CopyCoverage(x, y, bitmap);
            entry.info.pageIndex = static_cast<int>(cached->pages.size() - 1U);
            entry.info.u0 = static_cast<float>(x) / static_cast<float>(page.size);
            entry.info.v0 = static_cast<float>(y) / static_cast<float>(page.size);
            entry.info.u1 =
                static_cast<float>(x + bitmap.width) / static_cast<float>(page.size);
            entry.info.v1 =
                static_cast<float>(y + bitmap.height) / static_cast<float>(page.size);
            entry.info.texture = page.EnsureTexture();
            entry.info.drawable = true;
        }

        return &cached->glyphs.emplace(codepoint, entry).first->second;
    }

    int pageSize = FontAtlasCache::kDefaultPageSize;
    std::unordered_map<CacheKey, std::unique_ptr<CachedFontSize>, CacheKeyHash> caches;
};

FontAtlasCache::FontAtlasCache(int pageSize)
    : impl_(std::make_unique<Impl>(pageSize)) {}
FontAtlasCache::~FontAtlasCache() = default;
FontAtlasCache::FontAtlasCache(FontAtlasCache&&) noexcept = default;
FontAtlasCache& FontAtlasCache::operator=(FontAtlasCache&&) noexcept = default;

bool FontAtlasCache::GetMetrics(const std::string& fontGuid, int pixelSize,
                                FontFaceMetrics& outMetrics) {
    CachedFontSize* cached = impl_->FindOrLoad(fontGuid, pixelSize);
    if (!cached) return false;
    outMetrics = cached->metrics;
    return true;
}

bool FontAtlasCache::GetGlyph(const std::string& fontGuid, int pixelSize,
                              std::uint32_t codepoint, FontAtlasGlyph& outGlyph) {
    const LegacyGlyphEntry* entry =
        impl_->FindOrRasterize(fontGuid, pixelSize, codepoint);
    if (!entry) return false;
    outGlyph = entry->info;
    return true;
}

float FontAtlasCache::GetAdvance(const std::string& fontGuid, int pixelSize,
                                 std::uint32_t codepoint) {
    const LegacyGlyphEntry* entry =
        impl_->FindOrRasterize(fontGuid, pixelSize, codepoint);
    return entry ? entry->xAdvance : 0.0f;
}

float FontAtlasCache::GetKerning(const std::string& fontGuid, int pixelSize,
                                 std::uint32_t left, std::uint32_t right) {
    CachedFontSize* cached = impl_->FindOrLoad(fontGuid, pixelSize);
    return cached ? cached->face.Kerning(
                        left, right, static_cast<float>(NormalizePixelSize(pixelSize)))
                  : 0.0f;
}

void FontAtlasCache::Invalidate(const std::string& fontGuid) {
    for (auto iterator = impl_->caches.begin(); iterator != impl_->caches.end();) {
        if (iterator->first.guid == fontGuid) {
            iterator = impl_->caches.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void FontAtlasCache::Clear() {
    impl_->caches.clear();
}

std::size_t FontAtlasCache::PageCount(const std::string& fontGuid, int pixelSize) const {
    const CachedFontSize* cached = impl_->Find(fontGuid, pixelSize);
    return cached ? cached->pages.size() : 0U;
}

std::size_t FontAtlasCache::GlyphCount(const std::string& fontGuid, int pixelSize) const {
    const CachedFontSize* cached = impl_->Find(fontGuid, pixelSize);
    return cached ? cached->glyphs.size() : 0U;
}

std::size_t FontAtlasCache::CachedFontSizeCount() const {
    return impl_->caches.size();
}

} // namespace molga
