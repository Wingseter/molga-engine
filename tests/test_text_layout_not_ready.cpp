// The fresh-process not-ready paragraph-layout observation.
//
// This executable is deliberately generic — no text session main, no staged
// Engine/Text root, no initialization — so the process begins and stays in
// NeverInitialized and the layout service's ready gate is the only thing a case
// here can be observing. It never calls Initialize, Shutdown,
// hb_icu_get_unicode_funcs or u_cleanup; process exit supplies the isolation
// that an in-process stop/restore seam is not allowed to.
//
// The counters' positive witnesses live in test_text_layout, which lays out on a
// ready runtime and requires both counts to move. Without that companion case a
// counter stubbed to return zero would satisfy everything here.

#include "Common/Fixed26_6.h"
#include "Core/AssetDatabase.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextRuntimeDependencies.h"
#include "Text/TextShapingService.h"
#include "doctest.h"

#include <cstdint>
#include <string>

namespace {

namespace text = molga::text;

// 준비되지 않은 프로세스에서 배치 하나를 요청하는 데 필요한 최소 구성. 어떤
// 폰트도 열지 않고 어떤 아티팩트 저장소도 묶지 않는다: 준비 판정이 그 앞에
// 있으므로, 이 케이스가 관찰하는 것은 오직 그 문지기다.
struct NotReadyLayoutRequest {
    molga::AssetDatabase     database;
    text::FontRepository     repository{database};
    text::FontFamilyResolver resolver{database, repository};
    text::TextShapingService shaper;
    text::TextLayoutCache    cache{text::TextLayoutCacheLimits::Production()};
    text::TextLayoutService  service{resolver, shaper, cache};
    text::VectorTextDiagnosticSink sink;

    text::TextLayoutRequest Request() const {
        text::TextLayoutRequest request;
        // 빈 문자열이 아니라 진짜 글자다. 아무 일도 없는 요청이면 계수기가
        // 0인 것이 문지기 때문인지 할 일이 없어서인지 구분되지 않는다.
        request.utf8                 = "A";
        request.style.fontFamilyGuid = "11111111111111111111111111111111";
        request.style.shape.fontSize = molga::Fixed26_6::FromRaw(16 * 64);
        request.diagnosticContext.assetGuid     = "not-ready-asset";
        request.diagnosticContext.sceneObjectId = 3;
        request.diagnosticContext.componentType = "UILabel";
        return request;
    }
};

}  // namespace

TEST_CASE("layout creates no ICU or HarfBuzz object before runtime ready") {
    REQUIRE_FALSE(text::TextRuntimeDependencies::Get().IsReady());
    NotReadyLayoutRequest fixture;
    text::detail::ResetLayoutIcuObjectCreationCount();
    text::detail::ResetIcuObjectCreationCount();
    text::detail::ResetHarfBuzzObjectCreationCount();

    const auto layout = fixture.service.Layout(fixture.Request(), fixture.sink);
    CHECK_FALSE(layout.has_value());
    CHECK(text::detail::LayoutIcuObjectCreationCount() == 0U);
    CHECK(text::detail::IcuObjectCreationCount() == 0U);
    CHECK(text::detail::HarfBuzzObjectCreationCount() == 0U);

    REQUIRE(fixture.sink.Diagnostics().size() == 1U);
    const text::TextDiagnostic& diagnostic = fixture.sink.Diagnostics()[0];
    CHECK(diagnostic.code == text::TextDiagnosticCode::DependencyInvalid);
    CHECK(diagnostic.severity == text::TextSeverity::Error);
    // 종결 진단도 호출자의 문맥을 달고 나온다. 이것이 없으면 저자는 어느
    // 라벨이 실패했는지 알 수 없다.
    CHECK(diagnostic.assetGuid == std::string("not-ready-asset"));
    CHECK(diagnostic.sceneObjectId == 3U);
    CHECK(diagnostic.componentType == std::string("UILabel"));
    CHECK_FALSE(diagnostic.message.empty());
    CHECK_FALSE(diagnostic.remediation.empty());
    // 그리고 이 진단이 준비 판정에서 나왔다는 것까지 못 박는다. 배치가 부르는
    // 다른 문지기들 — 묶이지 않은 아티팩트 저장소, 분석기 자신의 준비 판정,
    // client handle 획득 — 도 전부 fail-closed라, 준비 판정을 통째로 지워도
    // nullopt와 DependencyInvalid 하나는 그대로 나온다. code만 보는 단언으로는
    // 그 회귀가 보이지 않고, 저자는 "폰트 설정이 잘못됐다"는 엉뚱한 안내를
    // 받게 된다.
    CHECK(diagnostic.message.find("ready text runtime") != std::string::npos);
}
