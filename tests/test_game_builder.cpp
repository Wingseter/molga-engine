#include "Assets/FontArtifactStore.h"
#include "Core/AssetDatabase.h"
#include "Core/BuildManifest.h"
#include "Core/PackageLayout.h"
#include "Core/PathConstants.h"
#include "Core/PathService.h"
#include "Editor/GameBuilder.h"
#include "Editor/Profiling/ProfilerReportSink.h"
#include "Editor/Project.h"
#include "Scripting/ScriptApi.h"
#include "ShaderPackageTestSupport.h"
#include "SmokeTestSupport.h"
#include "doctest.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct CapturingSink : molga::IProfilerReportSink {
    std::vector<std::string> labels;
    void ReportTiming(const std::string& label, double, const std::string&) override {
        labels.push_back(label);
    }
};

TEST_CASE("report sink can be swapped and captures timings") {
    CapturingSink sink;
    molga::SetReportSink(&sink);
    molga::ActiveReportSink().ReportTiming("Build: Total", 12.5, "Game");
    molga::SetReportSink(nullptr);  // 폴백 복귀
    REQUIRE(sink.labels.size() == 1);
    CHECK(sink.labels[0] == "Build: Total");
}

TEST_CASE("BuildManifest fails and names a missing required file") {
    BuildManifest m;
    m.requiredFiles = { "/nonexistent/molga_missing_zzz.txt" };
    std::string err;
    CHECK_FALSE(m.Validate(err));
    CHECK(err.find("molga_missing_zzz") != std::string::npos);
}

TEST_CASE("BuildManifest passes when every required file exists") {
    fs::path tmp = fs::temp_directory_path() / "molga_manifest_present.txt";
    { std::ofstream f(tmp); f << "x"; }
    BuildManifest m;
    m.requiredFiles = { tmp.string() };
    std::string err;
    CHECK(m.Validate(err));
    fs::remove(tmp);
}

TEST_CASE("Package constants use runtime package casing") {
    CHECK(std::string(Paths::Build::ASSETS) == "Assets");
    CHECK(std::string(Paths::Build::SCENES) == "Scenes");
    CHECK(std::string(Paths::Build::SHADER_BUNDLE) == "ShaderBundle");
}

TEST_CASE("PackageLayout executable name is platform aware") {
#if defined(_WIN32)
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game.exe");
#else
    CHECK(PackageLayout::ExecutableNameFor("Game") == "Game");
#endif
}

TEST_CASE("PackageLayout script manifest validation") {
    fs::path tmpDir = fs::temp_directory_path() / "molga_pkg_layout_test";
    fs::create_directories(tmpDir);

    std::string exeName = PackageLayout::ExecutableNameFor("TestGame");
    
    { std::ofstream(tmpDir / exeName); }
    fs::create_directories(tmpDir / "Scenes");
    { std::ofstream(tmpDir / "Scenes/main.json"); }
    fs::create_directories(tmpDir / "Assets");
    const std::string shaderHash =
        test_support::WriteMinimalMslShaderBundle(tmpDir);
    const auto baseConfig = test_support::MinimalPackageGameConfig(shaderHash);
    { std::ofstream(tmpDir / "game.json") << baseConfig.dump(2); }
    { std::ofstream(tmpDir / "asset_catalog.json") << "{\"schemaVersion\":1,\"records\":[]}"; }
    fs::create_directories(tmpDir / "Resources");
    { std::ofstream(tmpDir / "Resources/missing_texture.png") << "placeholder"; }

    std::string error;
    bool valid = PackageLayout::Validate(tmpDir, exeName, error);
    INFO("initial package validation error: " << error);
    CHECK(valid);
    CHECK(error.empty());

    {
        std::ofstream(tmpDir / "ShaderBundle/artifacts/forbidden.spv")
            << "forbidden";
        CHECK_FALSE(PackageLayout::Validate(tmpDir, exeName, error));
        CHECK(error.find("forbidden non-MSL") != std::string::npos);
        fs::remove(tmpDir / "ShaderBundle/artifacts/forbidden.spv");
    }
    {
        std::ofstream(tmpDir / "ShaderBundle/artifacts/test.fragment.msl",
                      std::ios::app) << "tampered";
        CHECK_FALSE(PackageLayout::Validate(tmpDir, exeName, error));
        CHECK(error.find("SHA-256 mismatch") != std::string::npos);
        CHECK(test_support::WriteMinimalMslShaderBundle(tmpDir) == shaderHash);
    }
    {
        std::ofstream(tmpDir / "ShaderBundle/manifest.json", std::ios::app)
            << ' ';
        CHECK_FALSE(PackageLayout::Validate(tmpDir, exeName, error));
        CHECK(error.find("manifest SHA-256 mismatch") != std::string::npos);
        CHECK(test_support::WriteMinimalMslShaderBundle(tmpDir) == shaderHash);
    }

    {
        std::ofstream f(tmpDir / "game.json");
        auto config = baseConfig;
        config["scripts"] = {
            {"enabled", false},
            {"library", "Scripts/libUserScripts.dylib"},
            {"apiVersion", molga::ScriptApiVersion},
        };
        f << config.dump(2);
    }
    valid = PackageLayout::Validate(tmpDir, exeName, error);
    CHECK(valid);
    CHECK(error.empty());

    {
        std::ofstream f(tmpDir / "game.json");
        auto config = baseConfig;
        config["scripts"] = {
            {"enabled", true},
            {"library", "Scripts/libUserScripts.dylib"},
            {"apiVersion", molga::ScriptApiVersion},
        };
        f << config.dump(2);
    }
    valid = PackageLayout::Validate(tmpDir, exeName, error);
    CHECK_FALSE(valid);
    CHECK(error.find("missing from package") != std::string::npos);

    fs::create_directories(tmpDir / "Scripts");
    { std::ofstream(tmpDir / "Scripts/libUserScripts.dylib"); }
    valid = PackageLayout::Validate(tmpDir, exeName, error);
    CHECK(valid);
    CHECK(error.empty());

    fs::remove_all(tmpDir);
}

// ═══════════════════════════════════════════════════════════════════════════
// Task 8.2: 검증된 폰트 산출물이 패키지 경계를 넘는다 — 그리고 없으면 멈춘다
// ═══════════════════════════════════════════════════════════════════════════
//
// 패키지된 런타임은 폰트 바이트를 원본 .ttf 경로가 아니라 카탈로그의
// ProjectLibrary locator로만 연다. 그 locator가 가리키는 자리에 바이트가
// 없으면 패키지된 모든 텍스트가 두부가 되므로, EmitAssetCatalog는 거기서
// 빌드를 멈춘다.
//
// 그 멈춤은 지금까지 아무 테스트도 목격하지 않았다: 복사 자체는 패키지된 e2e
// smoke가 잡지만(폰트를 안 옮기면 런타임이 종료 코드 4를 낸다), 바이트가
// 없을 때의 fail-fast는 어떤 픽스처도 만들어 내지 않았다. 반쪽은 증명되고
// 반쪽은 장식이었다.

namespace {

// 카탈로그가 이름을 대는 폰트. 이름이 아니라 이 GUID가 계약이다.
constexpr const char* kPackagedFontGuid = "44444444444444444444444444444444";
constexpr const char* kPackagedFontRelative = "fonts/NotoSans-Regular.ttf";

void CopyFixturePair(const fs::path& sourceRoot, const fs::path& assetsRoot,
                     const std::string& relative) {
    // 원본과 .meta는 언제나 한 쌍이다. 한쪽만 옮기면 ScanProject가 새 GUID를
    // 만들어 버리고 위의 GUID 계약이 조용히 무너진다.
    for (const std::string suffix : {std::string(), std::string(".meta")}) {
        const fs::path source = sourceRoot / (relative + suffix);
        const fs::path destination = assetsRoot / (relative + suffix);
        REQUIRE_MESSAGE(fs::is_regular_file(source), source.string());
        fs::create_directories(destination.parent_path());
        REQUIRE(fs::copy_file(source, destination));
    }
}

// project.molga 하나가 Project::Open의 전부다.
fs::path MakeFontProject(const fs::path& root, const std::string& name) {
    fs::create_directories(root / "Assets");
    std::ofstream(root / "project.molga") << "{\"name\":\"" << name << "\"}";
    return root;
}

}  // namespace

TEST_CASE("EmitAssetCatalog packages verified font artifacts and refuses without them") {
    // ── 경고: 이 케이스를 SUBCASE로 가르지 말 것 ─────────────────────────────
    // 하나의 프로세스에 하나의 폰트 산출물 권한이다. AssetDatabase 싱글턴은
    // store를 한 번만 묶고 두 번째 바인딩을 거절하므로, SUBCASE로 가르면
    // 실패가 아니라 **무한 루프**가 된다: doctest는 SUBCASE 하나마다 본문을
    // 다시 들어가는데, 두 번째 진입은 어떤 SUBCASE 표식에도 닿기 전에 아래
    // 재바인딩 REQUIRE에서 던진다. 그러면 방문된 SUBCASE가 하나도 없으므로
    // doctest는 아직 안 가 본 SUBCASE를 찾아 본문을 영원히 다시 돈다 —
    // 조용히 매달리고, 어떤 단언도 실패로 보고되지 않는다.
    //
    // 싱글턴에 기대는 케이스는 이 스위트에서 언제나 한 줄기로 남는다.
    test_support::TempDirectory temp{"game-builder-font-artifacts"};

    // 카탈로그의 권한이 되는 프로젝트: Assets에 폰트 한 쌍, 스캔이 그것을
    // Library/Imported 아래 불변 산출물로 발행한다.
    const fs::path authored = MakeFontProject(temp.Path() / "Authored",
                                              "AuthoredFonts");
    CopyFixturePair(fs::path(MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT),
                    authored / "Assets", kPackagedFontRelative);

    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    std::string bindError;
    REQUIRE_MESSAGE(
        database.BindFontArtifactStore(
            std::make_shared<const molga::FontArtifactStore>(
                molga::FontArtifactStore::ForProject(authored)),
            &bindError),
        bindError);
    database.ScanProject(authored / "Assets");

    const molga::AssetRecord* record =
        database.Find(std::string(kPackagedFontGuid));
    REQUIRE(record != nullptr);
    REQUIRE(record->fontArtifact.has_value());
    const fs::path locator = record->fontArtifact->locator.relativePath;
    const std::uint64_t artifactBytes = record->fontArtifact->byteSize;
    REQUIRE_FALSE(locator.empty());
    REQUIRE(fs::exists(authored / locator));

    // ── 성공 경로: 산출물이 패키지 경계를 넘는다 ────────────────────────────
    Project::Get().Close();
    REQUIRE(Project::Get().Open(authored.string()));
    const fs::path complete = temp.Path() / "package-complete";
    fs::create_directories(complete);
    REQUIRE(molga::detail::EmitAssetCatalogForTest(GameBuilder::Get(),
                                                   complete.string()));
    CHECK(fs::exists(complete / "asset_catalog.json"));
    // 참조된 폰트 바이트가 locator 그대로 실행 파일 옆에 놓인다.
    REQUIRE(fs::exists(complete / locator));
    CHECK(fs::file_size(complete / locator) == artifactBytes);

    // ── 실패 경로: 카탈로그가 이름을 댄 바이트가 없다 ───────────────────────
    // 카탈로그는 여전히 위 locator를 대지만, 패키지되는 프로젝트 루트 아래에는
    // 그 바이트가 없다. 스캔은 자기 루트의 산출물을 그 자리에서 다시 발행
    // 하므로 바이트를 지우는 것으로는 이 상태를 만들 수 없다 — 카탈로그의
    // 권한과 패키지되는 루트가 갈린 상태가 이 실패의 실제 모양이고, 그것이
    // 정확히 이 fail-fast가 막는 것이다.
    const fs::path emptied = MakeFontProject(temp.Path() / "Emptied",
                                             "EmptiedFonts");
    REQUIRE_FALSE(fs::exists(emptied / locator));
    Project::Get().Close();
    REQUIRE(Project::Get().Open(emptied.string()));

    const fs::path tofu = temp.Path() / "package-tofu";
    fs::create_directories(tofu);
    CHECK_FALSE(molga::detail::EmitAssetCatalogForTest(GameBuilder::Get(),
                                                       tofu.string()));
    // 그리고 무엇이 없는지 이름을 댄다.
    const std::string error = GameBuilder::Get().GetLastError();
    CHECK(error.find("missing from the project library") != std::string::npos);
    CHECK(error.find(locator.filename().string()) != std::string::npos);
    // 멈췄으므로 그 폰트는 패키지에 실리지 않았다. 계속 진행하는 구현은
    // 카탈로그만 써 놓고 성공을 보고한다 — 그 패키지의 모든 텍스트가 두부다.
    CHECK_FALSE(fs::exists(tofu / locator));

    Project::Get().Close();
}
