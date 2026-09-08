// Molga Engine Runtime - Standalone game player without editor
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>

#include "Core/Bootstrap.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/CameraOutputLayout.h"
#include "Core/Profiling/ProfileScope.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
#include "ECS/BuiltinComponents.h"
#include "Core/World.h"
#include "Core/SceneRuntime.h"
#include "Rendering/Camera2D.h"
#include "Systems/Audio.h"
#include "Rendering/TextRenderer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/TilemapRenderer.h"
#include "ECS/Components/MarrowRenderer.h"
#include "ECS/Components/ParticleSystem.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/Rigidbody2D.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/TextRenderer2D.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UILabel.h"
#include "Core/SceneSerializer.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/BuiltinScripts.h"
#include "Rendering/RenderPass.h"
#include "Core/PathService.h"
#include "Core/SmokeReport.h"
#include "Core/EventBus.h"
#include "Core/ProjectSettings.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/Physics2D.h"
#include "Core/GameConfig.h"
#include "Assets/FontArtifactStore.h"
#include "Core/AssetDatabase.h"
#include "Core/PersistentStorage.h"
#include "Core/PlayerPrefs.h"
#include "Core/SaveSystem.h"
#include "UI/UISystem.h"
#include "Scripting/ScriptPackageLoader.h"
#include "Common/Fixed26_6.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextRuntimeDependencies.h"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/utsname.h>
#endif



namespace {

// ── Task 11.2 Step 7f: 막힌 종료 위에서는 정상적으로 돌아가지 않는다 ─────────
// 결과가 Complete가 아닌 동안 이 진입점은 돌아가지도, 진단 sink나 텍스트
// 런타임 guard를 파괴하지도, host를 강제로 reset하지도 않는다. 아는 외부
// 소유자를 놓고 같은 host로 다시 시도하되, 계속 막히면 소유자를 전부 든 채
// 실패하는 종료 코드로 프로세스를 끝낸다 — std::_Exit은 소멸자도 atexit도
// 돌리지 않으므로 ICU 종결 정리가 살아 있는 page 밑에서 돌지 않는다.
[[noreturn]] void ExitShutdownBlocked(EngineShutdownStatus status) {
    const char* reason = status == EngineShutdownStatus::GpuDrainFailed
                             ? "the GPU idle/fence drain failed"
                             : "an external GPU lifetime is still held";
    std::fprintf(stderr,
                 "ENGINE_SHUTDOWN_BLOCKED: %s; every owner is retained and no "
                 "normal teardown or ICU cleanup runs\n",
                 reason);
    std::fflush(stderr);
    std::_Exit(70);
}

// 한 번 시도하고, 막히면 소유자를 전부 든 채 실패하는 종료 코드로 끝낸다.
//
// 예전에는 여기서 EngineShutdown을 두 번 불렀다. 재시도처럼 읽혔지만
// 재시도가 아니었다: 두 호출 사이에 **아무것도 놓지 않으므로** 단계 기계는
// 멱등하게 같은 답을 돌려주고, 유일한 관찰 가능한 차이는 같은 blocker
// 진단이 두 번 나가는 것이었다. 그 자리를 디버깅하는 사람은 중복된
// blocker에서 시작해 그 사실을 스스로 유도해야 한다.
//
// 재시도가 의미를 갖는 것은 그 사이에 놓을 외부 소유자가 있을 때뿐이고, 이
// 진입점에는 아직 그런 소유자가 없다(최신 UIFrameResult를 붙드는 자리는
// Task 12.3이 만든다). 그 자리가 생기면 **놓는 코드와 함께** 두 번째 호출을
// 여기 되살린다 — 놓는 코드 없는 재시도는 배선이 아니라 잡음이다.
EngineShutdownStatus ShutdownEngineOrExit(
    std::unique_ptr<EngineHost>& host,
    molga::text::TextDiagnosticSink& sink) {
    const EngineShutdownStatus status = EngineShutdown(host, sink);
    if (status != EngineShutdownStatus::Complete) ExitShutdownBlocked(status);
    return status;
}

} // namespace

namespace {

constexpr int kBenchmarkWarmupFrames = 120;
constexpr int kBenchmarkMeasuredFrames = 600;

std::string RuntimeArchitecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

std::string RuntimeOsVersion() {
#if defined(__APPLE__)
    std::string result = "macOS";
    char version[128]{};
    std::size_t size = sizeof(version);
    if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) == 0 &&
        size > 1U) {
        result += " ";
        result += version;
    }
#elif defined(__linux__)
    std::string result = "Linux";
    utsname name{};
    if (uname(&name) == 0) {
        result += " ";
        result += name.release;
    }
#elif defined(_WIN32)
    std::string result = "Windows";
#else
    std::string result = "unknown";
#endif
    return result;
}

std::uint64_t ResidentMemoryBytes() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<std::uint64_t>(info.resident_size);
    }
#endif
    return 0;
}

std::uint64_t PeakMemoryBytes() {
#if defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<std::uint64_t>(usage.ru_maxrss);
    }
#endif
    return 0;
}

double Percentile(std::vector<double> values, double percentile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        percentile * static_cast<double>(values.size() - 1U));
    return values[index];
}

bool BuildRuntimeSceneCatalog(const GameConfig& config,
                              const std::filesystem::path& packageRoot,
                              SceneRuntime::SceneCatalog& catalog,
                              std::string& errorOut) {
    catalog.clear();
    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(packageRoot, error);
    if (error) {
        errorOut = "Could not canonicalize package root: " + error.message();
        return false;
    }

    for (const auto& entry : config.sceneCatalog) {
        const std::filesystem::path stored(entry.packagePath);
        if (entry.id.empty() || stored.empty() || stored.is_absolute() ||
            stored.has_root_name() || stored.has_root_directory()) {
            errorOut = "Invalid scene catalog entry: " + entry.id;
            return false;
        }
        const auto normalized = stored.lexically_normal();
        for (const auto& part : normalized) {
            if (part == "..") {
                errorOut = "Scene package path escapes package root: " + entry.packagePath;
                return false;
            }
        }

        const auto resolved = std::filesystem::weakly_canonical(
            canonicalRoot / normalized, error);
        if (error) {
            errorOut = "Could not resolve scene package path: " + entry.packagePath;
            return false;
        }
        const auto relative = std::filesystem::relative(resolved, canonicalRoot, error);
        if (error || relative.empty() || relative.is_absolute()) {
            errorOut = "Scene package path is outside package root: " + entry.packagePath;
            return false;
        }
        for (const auto& part : relative) {
            if (part == "..") {
                errorOut = "Scene package path is outside package root: " + entry.packagePath;
                return false;
            }
        }

        if (!catalog.emplace(entry.id, resolved.string()).second) {
            errorOut = "Duplicate scene catalog id: " + entry.id;
            return false;
        }
    }

    if (catalog.find(config.startupSceneId) == catalog.end()) {
        errorOut = "startupSceneId is not present in the scene catalog: " +
                   config.startupSceneId;
        return false;
    }
    errorOut.clear();
    return true;
}

struct RuntimeSmokeOptions {
    bool enabled = false;
    int frames = 3;
    std::filesystem::path reportPath;
};

std::optional<RuntimeSmokeOptions> ParseRuntimeSmoke(int argc, char** argv) {
    RuntimeSmokeOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--smoke") {
            options.enabled = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            try {
                options.frames = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else if (arg == "--report" && i + 1 < argc) {
            options.reportPath = argv[++i];
        } else {
            return std::nullopt;
        }
    }
    if (options.enabled &&
        (options.frames < 1 || options.reportPath.empty())) {
        return std::nullopt;
    }
    return options;
}

struct AssetResolutionSummary {
    int resolved = 0;
    int missing = 0;

    bool ok() const { return missing == 0; }
};

AssetResolutionSummary SummarizeSpriteAssetResolution(const World& world) {
    AssetResolutionSummary summary;
    for (const auto& object : world.Objects()) {
        const auto* sprite = object ? object->GetComponent<SpriteRenderer>() : nullptr;
        if (sprite == nullptr) {
            continue;
        }

        const bool hasGuid = !sprite->GetTextureGuid().empty();
        const bool hasPath = !sprite->GetTexturePath().empty();
        if (!hasGuid && !hasPath) {
            continue;
        }

        bool resolved = false;
        if (hasGuid) {
            const auto path = molga::AssetDatabase::Get().AbsoluteSourcePath(sprite->GetTextureGuid());
            resolved = !path.empty() && std::filesystem::exists(path);
        }
        if (!resolved && hasPath) {
            const auto path = PathService::Get().ResolveAsset(sprite->GetTexturePath());
            resolved = !path.empty() && std::filesystem::exists(path);
        }

        if (resolved) {
            ++summary.resolved;
        } else {
            ++summary.missing;
        }
    }
    return summary;
}

struct FontResolutionSummary {
    int resolved = 0;
    int missing = 0;
    bool ok() const { return missing == 0; }
};

FontResolutionSummary SummarizeFontAssetResolution(const World& world) {
    FontResolutionSummary summary;
    for (const auto& object : world.Objects()) {
        if (!object) continue;
        for (auto* component : object->GetComponents()) {
            std::string guid;
            if (auto* text = dynamic_cast<TextRenderer2D*>(component)) {
                guid = text->GetFontGuid();
            } else if (auto* label = dynamic_cast<UILabel*>(component)) {
                guid = label->GetFontGuid();
            }
            if (guid.empty()) continue;

            const auto* record = molga::AssetDatabase::Get().Find(guid);
            const auto path = molga::AssetDatabase::Get().AbsoluteSourcePath(guid);
            if (record && record->importer == "FontImporter" &&
                !record->importFailed && !path.empty() &&
                std::filesystem::is_regular_file(path)) {
                ++summary.resolved;
            } else {
                ++summary.missing;
            }
        }
    }
    return summary;
}

int CountUIComponents(const World& world) {
    int count = 0;
    for (const auto& object : world.Objects()) {
        if (!object) continue;
        for (auto* component : object->GetComponents()) {
            if (!component) continue;
            const std::string type = component->GetTypeName();
            if (type == "UICanvas" || type == "RectTransform" ||
                type == "UIImage" || type == "UILabel" || type == "UIButton") {
                ++count;
            }
        }
    }
    return count;
}

int CountPlatformerPlayers(const World& world) {
    int count = 0;
    for (const auto& object : world.Objects()) {
        if (!object || !object->IsActive()) continue;
        const auto* body = object->GetComponent<Rigidbody2D>();
        if (!object->GetComponent<PlatformerController>() || !body ||
            body->GetBodyType() != Rigidbody2D::BodyType::Dynamic ||
            !object->GetComponent<BoxCollider2D>() ||
            !object->GetComponent<SpriteRenderer>()) continue;
        ++count;
    }
    return count;
}

enum class SmokeUIAction {
    SaveOption,
    LoadFirstStage,
    LoadSecondStage,
    SaveCompletion
};

constexpr std::array<SmokeUIAction, 4> kSmokeUIActions = {
    SmokeUIAction::SaveOption,
    SmokeUIAction::LoadFirstStage,
    SmokeUIAction::LoadSecondStage,
    SmokeUIAction::SaveCompletion
};

struct SmokeUITarget {
    GameObject* object = nullptr;
    RectTransform* rect = nullptr;
    PlayerPrefsButton* prefs = nullptr;
    SceneLoadButton* scene = nullptr;
    SaveSlotButton* slot = nullptr;
};

SmokeUITarget FindSmokeUITarget(World& world, SmokeUIAction action) {
    for (const auto& object : world.Objects()) {
        if (!object || !object->IsActive() ||
            !object->GetComponent<UIButton>()) continue;
        auto* rect = object->GetComponent<RectTransform>();
        if (!rect) continue;

        SmokeUITarget target;
        target.object = object.get();
        target.rect = rect;
        if (action == SmokeUIAction::SaveOption) {
            target.prefs = object->GetComponent<PlayerPrefsButton>();
            if (target.prefs && target.prefs->IsEnabled()) return target;
        } else if (action == SmokeUIAction::LoadFirstStage ||
                   action == SmokeUIAction::LoadSecondStage) {
            target.scene = object->GetComponent<SceneLoadButton>();
            if (target.scene && target.scene->IsEnabled()) return target;
        } else {
            target.slot = object->GetComponent<SaveSlotButton>();
            if (target.slot && target.slot->IsEnabled()) return target;
        }
    }
    return {};
}

struct KoreanTitleProbe {
    bool textPreserved = false;
    bool fontGlyphsPresent = false;
    bool atlasQuadsCollected = false;
    int glyphQuads = 0;

    bool ok() const {
        return textPreserved && fontGlyphsPresent && atlasQuadsCollected &&
               glyphQuads > 0;
    }
};

// ── Task 8.2 Step 7e: 패키지된 시작 화면의 텍스트 증명 ───────────────────────
// 폰트 GUID/codepoint 단위의 atlas 계수기는 증거가 아니다: 그 계수기는 셰이핑도
// page 소유권도 보지 않으므로, 셰이핑이 통째로 빠져도 같은 값을 낸다. 그래서
// 이 증명은 프로덕션과 정확히 같은 Layout/CollectLayout 경로를 지나 "셰이핑된
// glyph-ID 명령이 0 아닌 page 정체성과 붙든 토큰과 함께 모였다"만 본다.
KoreanTitleProbe ProbeKoreanTitle(World& world,
                                  molga::text::TextDiagnosticSink& sink) {
    static constexpr const char* kExpectedTitle = u8"한글 타이틀 - 시작";
    KoreanTitleProbe result;

    UILabel* title = nullptr;
    for (const auto& object : world.Objects()) {
        auto* label = object ? object->GetComponent<UILabel>() : nullptr;
        if (label && label->IsEnabled() && label->GetText() == kExpectedTitle) {
            title = label;
            break;
        }
    }
    if (!title) return result;
    // 원문이 정확히 위 상수와 같다는 것이 곧 "한글이 보존되었다"이다.
    result.textPreserved = true;

    molga::text::TextLayoutRequest request;
    request.utf8 = title->GetText();
    // 저작된 family가 이기고, 없으면 schema 1의 폰트 지목이 레거시 단일 face
    // 경로로 간다 — 패키지된 이 라벨이 정확히 그 경우다.
    const UILabel::FontFamilyView family = title->ResolveFontFamilyView();
    if (!family.familyGuid.empty()) {
        request.style.fontFamilyGuid = family.familyGuid;
    } else if (!family.faceFontGuids.empty()) {
        request.style.legacyFontGuid = family.faceFontGuids.front();
    }
    if (const auto fontSize =
            molga::Fixed26_6::FromFloat(title->GetFontSizePx())) {
        request.style.shape.fontSize = *fontSize;
    }
    request.style.analysis.locale = title->GetLocale();
    request.style.analysis.baseDirection = title->GetBaseDirection();
    request.style.wrap = title->GetWrapMode();
    request.style.overflow = title->GetOverflowMode();
    request.style.maxLines = title->GetMaxLines();
    if (const auto spacing =
            molga::Fixed26_6::FromFloat(title->GetLineSpacing())) {
        request.style.lineSpacing = *spacing;
    }
    request.diagnosticContext.componentType = "UILabel";

    const auto layout = TextRenderer::Get().Layout(request, sink);
    if (!layout) return result;

    // 없는 glyph가 하나도 없다는 것이 "이 폰트가 이 글자들을 실제로 그린다"의
    // 권한 있는 형태다. HasCodepoint/GlyphId 같은 검사용 seam은 셰이핑도
    // fallback도 대신하지 못하므로 여기서 부르지 않는다.
    int shapedGlyphs = 0;
    bool allGlyphsPresent = true;
    for (const molga::text::TextLine& line : (*layout)->lines) {
        for (const molga::text::VisualRun& run : line.visualRuns) {
            for (const molga::text::PositionedGlyph& positioned : run.glyphs) {
                ++shapedGlyphs;
                allGlyphsPresent =
                    allGlyphsPresent && !positioned.glyph.missing;
            }
        }
    }
    result.fontGlyphsPresent = allGlyphsPresent && shapedGlyphs > 0;

    molga::RenderQueue proofQueue;
    TextCollectContext collect;  // 항등 affine, 1배 래스터 정책
    // 수집 범위를 열지 않는다. 프레임 수집은 프레임 루프의 것 하나뿐이고, 이
    // 증명은 그 루프 밖에서 한 번 도는 시작 검사다. GlyphAtlasCache는 범위와
    // 무관하게 page 정체성과 지분 토큰을 함께 내주므로, 아래 단언이 재는 것은
    // 그대로 남는다 — 범위가 하는 일은 프레임 경계에서 pin 집합을 넘기는
    // 것이지 지분을 만드는 것이 아니다.
    TextRenderer::Get().CollectLayout(proofQueue, **layout, collect, sink);
    result.glyphQuads = static_cast<int>(proofQueue.GetCommands().size());

    bool allQuadsRetainAtlasPages = !proofQueue.GetCommands().empty();
    for (const auto& command : proofQueue.GetCommands()) {
        allQuadsRetainAtlasPages =
            allQuadsRetainAtlasPages &&
            static_cast<bool>(command.batchKey.texture) &&
            command.batchKey.isBatchable &&
            command.resourceLifetimeIdentity != 0U &&
            static_cast<bool>(command.resourceLifetime);
    }
    result.atlasQuadsCollected = allQuadsRetainAtlasPages;
    return result;
}

Vector2 RotateVector(const Vector2& value, float degrees) {
    constexpr float kPi = 3.14159265358979323846f;
    const float radians = degrees * kPi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}

struct SlopeTrialResult {
    bool contactObserved = false;
    float outgoingNormalSpeed = 0.0f;
    float tangentialSpeed = 0.0f;
};

SlopeTrialResult RunSlopeTrial(World& world,
                               Transform& playerTransform,
                               Rigidbody2D& playerBody,
                               BoxCollider2D& playerCollider,
                               BoxCollider2D& terrainCollider,
                               const Vector2& startPosition,
                               float slopeRotation,
                               const Vector2& outwardNormal,
                               const Vector2& tangent,
                               float terrainFriction,
                               float playerFriction) {
    constexpr float kFixedStep = 1.0f / 240.0f;
    constexpr float kIncomingNormalSpeed = 400.0f;
    constexpr float kIncomingTangentialSpeed = 240.0f;

    terrainCollider.SetFriction(terrainFriction);
    playerCollider.SetFriction(playerFriction);

    // Box2D mixes material coefficients when a contact is created. Recreate the
    // player's shape so a broad-phase contact retained from the prior trial
    // cannot keep that trial's friction coefficient.
    playerCollider.SetEnabled(false);
    playerTransform.SetWorldPosition(startPosition + outwardNormal * 160.0f);
    playerTransform.SetWorldRotation(slopeRotation);
    playerBody.SetVelocity(Vector2::Zero());
    world.FixedStep(kFixedStep);
    playerCollider.SetEnabled(true);
    world.FixedStep(kFixedStep);

    playerTransform.SetWorldPosition(startPosition);
    playerTransform.SetWorldRotation(slopeRotation);
    playerBody.SetVelocity(outwardNormal * -kIncomingNormalSpeed +
                           tangent * kIncomingTangentialSpeed);

    SlopeTrialResult result;
    for (int step = 0; step < 180; ++step) {
        world.FixedStep(kFixedStep);
        const Vector2 velocity = playerBody.GetVelocity();
        const float normalSpeed = velocity.Dot(outwardNormal);
        if (normalSpeed > 20.0f) {
            result.contactObserved = true;
            result.outgoingNormalSpeed = normalSpeed;
            result.tangentialSpeed = std::abs(velocity.Dot(tangent));
            break;
        }
    }
    return result;
}

struct PackagedPhysicsProbe {
    bool rotatedTerrainVerified = false;
    bool contactObserved = false;
    bool restitutionResponseObserved = false;
    bool frictionResponseObserved = false;

    bool ok() const {
        return rotatedTerrainVerified && contactObserved &&
               restitutionResponseObserved && frictionResponseObserved;
    }
};

PackagedPhysicsProbe ProbePackagedStagePhysics(World& world) {
    PackagedPhysicsProbe result;
    GameObject* terrain = world.Find("BouncySlope");
    GameObject* player = world.Find("Player");
    if (!terrain || !player) return result;

    auto* terrainTransform = terrain->GetComponent<Transform>();
    auto* terrainCollider = terrain->GetComponent<BoxCollider2D>();
    auto* playerTransform = player->GetComponent<Transform>();
    auto* playerCollider = player->GetComponent<BoxCollider2D>();
    auto* playerBody = player->GetComponent<Rigidbody2D>();
    auto* controller = player->GetComponent<PlatformerController>();
    if (!terrainTransform || !terrainCollider || !playerTransform ||
        !playerCollider || !playerBody || !controller ||
        !terrainCollider->IsEnabled() || !playerCollider->IsEnabled() ||
        !playerBody->IsEnabled() || !controller->IsEnabled() ||
        playerBody->GetBodyType() != Rigidbody2D::BodyType::Dynamic) {
        return result;
    }

    const float slopeRotation = terrainTransform->GetWorldRotation();
    const float authoredTerrainFriction = terrainCollider->GetFriction();
    const float authoredTerrainRestitution = terrainCollider->GetRestitution();
    const float authoredPlayerFriction = playerCollider->GetFriction();
    const float authoredPlayerRestitution = playerCollider->GetRestitution();
    const bool authoredMaterial = std::abs(slopeRotation - (-11.0f)) < 0.001f &&
        std::abs(authoredTerrainFriction - 0.25f) < 0.001f &&
        std::abs(authoredTerrainRestitution - 0.85f) < 0.001f &&
        std::abs(authoredPlayerFriction - 0.4f) < 0.001f &&
        std::abs(authoredPlayerRestitution) < 0.001f;

    const Vector2 terrainPosition = terrainTransform->GetWorldPosition();
    const Vector2 terrainOffset = terrainCollider->GetOffset();
    const Vector2 terrainSize = terrainCollider->GetSize();
    const Vector2 insidePoint = terrainPosition + RotateVector(
        terrainOffset + terrainSize * 0.5f, slopeRotation);
    const AABB terrainBounds = terrainCollider->GetWorldBounds();
    const Vector2 aabbOnlyPoint{terrainBounds.x + 2.0f, terrainBounds.y + 2.0f};
    const bool rotatedBackendQuery =
        Physics2D::OverlapPoint(world, insidePoint) == terrain &&
        terrainBounds.Contains(aabbOnlyPoint) &&
        Physics2D::OverlapPoint(world, aabbOnlyPoint) != terrain;
    result.rotatedTerrainVerified = authoredMaterial && rotatedBackendQuery;

    const Vector2 tangent = RotateVector(Vector2::Right(), slopeRotation).Normalized();
    const Vector2 outwardNormal = RotateVector(Vector2::Up(), slopeRotation).Normalized();
    const Vector2 topPoint = terrainPosition + RotateVector(
        {terrainOffset.x + terrainSize.x * 0.4f, terrainOffset.y}, slopeRotation);
    const Vector2 playerBottomCenter = RotateVector(
        playerCollider->GetOffset() +
            Vector2{playerCollider->GetSize().x * 0.5f,
                    playerCollider->GetSize().y},
        slopeRotation);
    const Vector2 trialStart = topPoint + outwardNormal * 4.0f - playerBottomCenter;

    const Vector2 originalPosition = playerTransform->GetWorldPosition();
    const float originalRotation = playerTransform->GetWorldRotation();
    const Vector2 originalVelocity = playerBody->GetVelocity();
    const float originalGravityScale = playerBody->GetGravityScale();
    const bool originalFreezeRotation = playerBody->IsRotationFrozen();

    controller->SetEnabled(false);
    playerBody->SetGravityScale(0.0f);
    playerBody->SetFreezeRotation(true);

    const SlopeTrialResult zeroFriction = RunSlopeTrial(
        world, *playerTransform, *playerBody, *playerCollider, *terrainCollider,
        trialStart, slopeRotation, outwardNormal, tangent, 0.0f, 0.0f);
    const SlopeTrialResult authoredFriction = RunSlopeTrial(
        world, *playerTransform, *playerBody, *playerCollider, *terrainCollider,
        trialStart, slopeRotation, outwardNormal, tangent,
        authoredTerrainFriction, authoredPlayerFriction);

    result.contactObserved = authoredFriction.contactObserved;
    result.restitutionResponseObserved = authoredFriction.contactObserved &&
        authoredFriction.outgoingNormalSpeed > 200.0f;
    result.frictionResponseObserved = authoredFriction.contactObserved &&
        zeroFriction.contactObserved &&
        authoredFriction.tangentialSpeed + 10.0f < zeroFriction.tangentialSpeed;
    if (!result.frictionResponseObserved) {
        std::cerr << "[RuntimeSmoke] friction probe failed: zero(contact="
                  << zeroFriction.contactObserved << ", tangent="
                  << zeroFriction.tangentialSpeed << "), authored(contact="
                  << authoredFriction.contactObserved << ", tangent="
                  << authoredFriction.tangentialSpeed << ")\n";
    }

    terrainCollider->SetFriction(authoredTerrainFriction);
    terrainCollider->SetRestitution(authoredTerrainRestitution);
    playerCollider->SetFriction(authoredPlayerFriction);
    playerCollider->SetRestitution(authoredPlayerRestitution);
    playerTransform->SetWorldPosition(originalPosition);
    playerTransform->SetWorldRotation(originalRotation);
    playerBody->SetVelocity(originalVelocity);
    playerBody->SetGravityScale(originalGravityScale);
    playerBody->SetFreezeRotation(originalFreezeRotation);
    controller->SetEnabled(true);
    world.FixedStep(1.0f / 240.0f);
    return result;
}

// ── Text runtime lifetime ────────────────────────────────────────────────────

// A present-but-malformed seam value returns this instead of nullopt, so the
// caller fails fast rather than falling through to a real application launch.
constexpr int kTextSeamParseError = 2;

// Test-only startup seam. --text-test-return-after-services=<code> returns
// <code> from the scoped startup function immediately after the text lifetime
// is established, so a test can observe the unwind order without a window.
std::optional<int> ParseTextTestReturnAfterServices(int argc, char* argv[]) {
    static constexpr std::string_view kFlag =
        "--text-test-return-after-services=";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument.rfind(kFlag, 0) != 0) continue;
        const std::string value(argument.substr(kFlag.size()));
        try {
            return std::stoi(value);
        } catch (const std::exception&) {
            // Present but unparseable is not the same as absent. Returning
            // nullopt here would launch the real application instead of the
            // seam, which in CI reads as a mystifying hang rather than a
            // broken argument.
            std::cerr << "Invalid --text-test-return-after-services value: "
                      << value << '\n';
            return kTextSeamParseError;
        }
    }
    return std::nullopt;
}

void EmitTextLifetimeEvent(const char* event) {
    std::cout << "MOLGA_TEXT_LIFETIME " << event << std::endl;
}

// Emits its event when destroyed, so declaration order alone fixes where the
// event lands in the unwind sequence: a marker declared before an owner is
// destroyed after that owner.
class TextLifetimeScopeMarker {
public:
    TextLifetimeScopeMarker(bool enabled, const char* event)
        : enabled_(enabled), event_(event) {}
    TextLifetimeScopeMarker(const TextLifetimeScopeMarker&)            = delete;
    TextLifetimeScopeMarker& operator=(const TextLifetimeScopeMarker&) = delete;
    ~TextLifetimeScopeMarker() {
        if (enabled_) EmitTextLifetimeEvent(event_);
    }

private:
    bool        enabled_ = false;
    const char* event_   = nullptr;
};

// Everything after PathService::InitFromExecutable runs inside this scope. The
// text runtime lifetime guard is created first, ahead of SDL, the window, the
// renderer, scripts, assets and scenes, and every one of those owners is
// declared after it. Every return below — argument, window, renderer, script,
// asset or scene failure — therefore unwinds them in reverse and releases all
// text handles before guard shutdown and u_cleanup. std::exit, _Exit and
// quick_exit are banned in this scope, and no cleanup callback may retain a
// text resource past this function.
int RunRuntimeAfterPaths(int argc, char* argv[], bool textSeamRequested,
                         int textSeamCode) {
    const std::filesystem::path engineTextRoot =
        PathService::Get().EngineResource("Engine/Text");
    // Collected, not logged: Step 11 requires this process to print the stable
    // code, the failed paths and the expected hash to stderr and return 4,
    // where Step 10 has the editor log and keep its shell. The difference from
    // src/main.cpp is required, not drift, and must survive any later
    // extraction of the shared seam.
    molga::text::VectorTextDiagnosticSink textDiagnostics;
    std::optional<molga::text::TextRuntimeLifetimeGuard> textGuard =
        molga::text::TextRuntimeLifetimeGuard::Create(
            molga::text::TextDependencyConfig::FromEngineTextRoot(
                engineTextRoot, /*packagedRuntime=*/false),
            textDiagnostics);
    if (!textGuard) {
        for (const molga::text::TextDiagnostic& diagnostic :
             textDiagnostics.Diagnostics()) {
            std::cerr << molga::text::StableTextDiagnosticCode(diagnostic.code)
                      << ": " << diagnostic.message << '\n'
                      << "  remediation: " << diagnostic.remediation << '\n';
        }
        std::cerr << "  contract: "
                  << (engineTextRoot / "text_dependency_contract.json") << '\n'
                  << "  data: " << (engineTextRoot / "icudt78l.dat") << " ("
                  << molga::text::kPackagedIcuDataBytes << " bytes, SHA-256 "
                  << molga::text::kPackagedIcuDataSha256 << ")\n";
        return 4;
    }

    // Named for what it observes, not for what follows it: this marker is
    // destroyed immediately before the guard, so it marks the instant guard
    // shutdown is about to begin, not its completion.
    const TextLifetimeScopeMarker textGuardShutdownMarker(
        textSeamRequested, "runtime_guard_shutdown_begins");

    if (textSeamRequested) {
        // Stands in for the text services every later milestone declares here:
        // a client handle that must be gone before the guard shuts down.
        const TextLifetimeScopeMarker textHandleMarker(
            true, "last_text_handle_destroyed");
        std::optional<molga::text::TextRuntimeClientHandle> textClient =
            molga::text::TextRuntimeClientHandle::Acquire();
        if (!textClient) {
            std::cerr << "Text runtime is not ready; no client handle\n";
            return 6;
        }
        return textSeamCode;
    }

    const auto smoke = ParseRuntimeSmoke(argc, argv);
    if (!smoke) {
        std::cerr << "Usage: runtime [--smoke --frames N --report PATH]\n";
        return 2;
    }

    // Load game configuration
    GameConfig config;
    Input::InitializeDefaultActions();
    std::string configPath = (PathService::Get().ExecutableDir() / "game.json").string();
    if (!LoadGameConfig(configPath, config)) {
        std::cerr << "Packaged game config is required: " << configPath << std::endl;
        if (smoke->enabled) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = "Missing or invalid game.json";
            report.Save(smoke->reportPath);
        }
        return 4;
    }
    if (!PersistentStorage::ConfigureRuntime(config.companyName, config.gameName)) {
        std::cerr << "Invalid companyName or gameName for persistent storage" << std::endl;
        return 4;
    }

    SceneRuntime::SceneCatalog sceneCatalog;
    std::string sceneCatalogError;
    if (!BuildRuntimeSceneCatalog(config, PathService::Get().ExecutableDir(),
                                  sceneCatalog, sceneCatalogError)) {
        std::cerr << "Invalid scene catalog: " << sceneCatalogError << std::endl;
        return 4;
    }

    // Validate required package directories
    const auto exeDir = PathService::Get().ExecutableDir();
    for (const auto& required : { "Assets", "Scenes", "ShaderBundle" }) {
        if (!std::filesystem::exists(exeDir / required)) {
            std::cerr << "Missing package directory: " << (exeDir / required) << std::endl;
            if (smoke->enabled) {
                SmokeReport report;
                report.executable = "molga_runtime";
                report.status = "error";
                report.message = std::string("Missing package directory: ") + required;
                report.Save(smoke->reportPath);
            }
            return 4;
        }
    }

    WindowConfig wc;
    wc.title = config.gameName;
    wc.width = config.windowWidth;
    wc.height = config.windowHeight;
    wc.fullscreen = config.fullscreen;
    wc.resizable = config.resizable;
    wc.visible = !smoke->enabled;
    wc.graphicsValidation = smoke->enabled;
    auto host = EngineInit(wc);
    if (!host) return -1;

    // Initialize renderer
    auto renderer = std::make_unique<Renderer>();
    std::string rendererError;
    if (!renderer->Init(&rendererError)) {
        std::cerr << "Renderer initialization failed: " << rendererError << '\n';
        ShutdownEngineOrExit(host, textDiagnostics);
        return -1;
    }
    molga::RenderSystem2D::Get().Init();
    Shader* shader = ShaderManager::Get().Get("default");
    if (!shader) {
        std::cerr << "Renderer shader bundle has no default entry\n";
        molga::RenderSystem2D::Get().Shutdown();
        renderer.reset();
        ShutdownEngineOrExit(host, textDiagnostics);
        return -1;
    }
    SceneRuntime sceneRuntime(std::move(sceneCatalog));

    // Initialize scripting
    RegisterBuiltinScripts();

    // Load and validate user script package
    std::string loaderError;
    std::string smokeReportPathStr = smoke->reportPath.string();
    if (!ScriptPackageLoader::Load(config, smoke->enabled, smokeReportPathStr, loaderError)) {
        std::cerr << "Script package initialization failed: " << loaderError << std::endl;
        sceneRuntime.Shutdown();
        PlayerPrefs::Shutdown();
        molga::RenderSystem2D::Get().Shutdown();
        ShaderManager::Get().Shutdown();
        renderer.reset();
        ShutdownEngineOrExit(host, textDiagnostics);
        return 4;
    }

    // ── Task 8.2 Step 3b/7f: 공유 텍스트 서비스 한 벌 ────────────────────────
    // 텍스트 런타임 guard는 이 함수 맨 위에 이미 서 있다. database는 프로세스
    // 소유의 그 권한이고, 바로 아래에서 봉인/프로젝트 폰트 산출물 저장소를
    // 받는다 — 첫 폰트 요청보다 먼저다. 프로세스 인스턴스는 정적 저장 수명이
    // 아니라 heap에 있고, 아래 ShutdownRendererThenTextGpuResources가 guard의
    // u_cleanup 전에 부순다.
    if (!TextRenderer::Get().Init(molga::AssetDatabase::Get(), textDiagnostics)) {
        std::cerr << "Rendered text is unavailable: the shared text services "
                     "could not be initialized." << std::endl;
        for (const molga::text::TextDiagnostic& diagnostic :
             textDiagnostics.Diagnostics()) {
            std::cerr << molga::text::StableTextDiagnosticCode(diagnostic.code)
                      << ": " << diagnostic.message << '\n';
        }
        molga::RenderSystem2D::Get().Shutdown();
        ShaderManager::Get().Shutdown();
        renderer.reset();
        ShutdownEngineOrExit(host, textDiagnostics);
        return 4;
    }

    // ── Task 11.1 Step 3i/A3: 배치의 두 의존물을 진입점이 소유한다 ─────────
    // Build는 이제 편집 상태 제공자와 정확한 공유 TextLayoutService를 요구한다.
    // 그 서비스는 TextRenderer가 초기화된 *뒤에야* 존재하므로 설치 지점은 바로
    // 여기다. 진짜 제공자는 Task 14가 설치하고, 그때까지는 값이 비어 있음을
    // 명시적으로 말하는 Empty 제공자가 그 자리를 지킨다 — 기본 인자로 숨기면
    // 한 표면에서 빠뜨려도 컴파일이 통과한다.
    UISystem::Get().InstallLayoutDependencies(
        molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
        TextRenderer::Get().LayoutService());

    // Load asset catalog if present (runtime mode: read-only, no .meta creation)
    PathService::Get().SetAssetRoot(PathService::Get().ExecutableDir());
    bool assetCatalogLoaded = false;
    int assetCatalogRecords = 0;
    {
        auto catalogPath = PathService::Get().ExecutableDir() / "asset_catalog.json";
        // Step 4e: this is the development/package authority the runtime has
        // today — the catalog beside the executable still carries authoring
        // ProjectLibrary locators, so it loads in Project mode against a store
        // rooted at that same directory. Task 17 replaces only this authority
        // construction with the verified sealed-manifest store; the four
        // argument call below does not change then.
        const std::filesystem::path storageRoot =
            PathService::Get().ExecutableDir();
        std::string bindError;
        const bool authorityBound =
            molga::AssetDatabase::Get().FontArtifacts() != nullptr ||
            molga::AssetDatabase::Get().BindFontArtifactStore(
                std::make_shared<const molga::FontArtifactStore>(
                    molga::FontArtifactStore::ForProject(storageRoot)),
                &bindError);
        if (!authorityBound) {
            std::cerr << "Could not bind the runtime font artifact store: "
                      << bindError << std::endl;
        }
        if (authorityBound && std::filesystem::exists(catalogPath)) {
            std::string catalogError;
            assetCatalogLoaded = molga::AssetDatabase::Get().LoadCatalog(
                catalogPath, storageRoot, molga::AssetCatalogMode::Project,
                &catalogError);
            assetCatalogRecords = static_cast<int>(molga::AssetDatabase::Get().RecordCount());
            if (assetCatalogLoaded) {
                std::cout << "Asset catalog loaded: " << assetCatalogRecords
                          << " records" << std::endl;
            } else {
                std::cerr << "Failed to load asset catalog: " << catalogPath
                          << ": " << catalogError << std::endl;
            }
        }
    }

    // Load the startup scene through the same transactional runtime used for
    // subsequent script-driven transitions.
    std::cout << "Loading scene: " << config.startupSceneId << std::endl;
    if (!sceneRuntime.RequestLoad(config.startupSceneId) ||
        !sceneRuntime.CommitPendingLoad()) {
        std::cerr << "Failed to load startup scene: " << sceneRuntime.LastError() << std::endl;
        if (smoke->enabled) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = "Failed to load startup scene";
            report.Save(smoke->reportPath);
        }
        sceneRuntime.Shutdown();
        PlayerPrefs::Shutdown();
        // Task 6.3/8.2/11.2 Step 7f: 이 실패 경로도 같은 순서를 쓴다. 여기서
        // 아직 제출된 프레임이 없다는 것은 사실이지만, 그것은 논증이지
        // 구조가 아니다.
        molga::RenderSystem2D::Get().Shutdown();
        ShaderManager::Get().Shutdown();
        host->RegisterGpuConsumers(renderer.get(), &TextRenderer::Get());
        ShutdownEngineOrExit(host, textDiagnostics);
        renderer.reset();
        return 4;
    }

    std::cout << "Loaded " << sceneRuntime.ActiveWorld().Objects().size()
              << " game objects" << std::endl;
    // Prove that the packaged startup scene retained its exact Hangul title and
    // that its GUID-backed font can rasterize those codepoints into real atlas
    // quads. Merely finding a font file in the catalog is not sufficient.
    const KoreanTitleProbe koreanTitleProbe =
        ProbeKoreanTitle(sceneRuntime.ActiveWorld(), textDiagnostics);

    int renderedFrames = 0;
    std::vector<double> benchmarkCpuMilliseconds;
    benchmarkCpuMilliseconds.reserve(kBenchmarkMeasuredFrames);
    molga::RenderStats benchmarkBaseStats{};
    molga::FrameTelemetry benchmarkGpuTelemetry{};
    bool benchmarkBaseCaptured = false;
    int successfulSceneTransitions = 0;
    int uiDrivenSceneTransitions = 0;
    std::size_t smokeUIActionIndex = 0;
    bool smokePressPhase = true;
    bool smokeUIFlowOk = true;
    bool smokeUITransitionAwaitingCommit = false;
    std::string expectedPreferenceKey;
    bool expectedPreferenceValue = false;
    std::string expectedSlotName;
    nlohmann::json expectedSlotPayload;
    bool scriptDrivenPrefsSaved = false;
    bool scriptDrivenSlotSaved = false;
    bool rawMouseWasDown = false;
    auto gameOutputRenderer = std::make_unique<molga::GameOutputRenderer>();
    molga::GameOutputResult lastGameOutputResult;
    // Main game loop
    while (!host->ShouldClose()) {
        const auto frameCpuStart = std::chrono::steady_clock::now();
        if (smoke->enabled && renderedFrames == kBenchmarkWarmupFrames &&
            !benchmarkBaseCaptured) {
            benchmarkBaseStats = renderer->Stats();
            benchmarkBaseCaptured = true;
        }
        host->PollEvents();
        if (host->ShouldClose()) break;
        Time::Update();
        // Task 6.3: 프레임 번호가 정해진 바로 다음, 어떤 텍스트도 큐에 담기기
        // 전에 이 프레임의 glyph 수집을 연다. 어휘적 범위가 루프 본문 전체
        // 이므로 아래의 모든 break가 이 안에서 일어나고, 수집은 명령들이 page
        // 토큰을 다 복사한 뒤에야 닫힌다.
        auto glyphCollection = TextRenderer::Get().BeginGlyphCollection(
            static_cast<std::uint64_t>(Time::GetFrameCount()));
        float dt = Time::GetDeltaTime();
        World& world = sceneRuntime.ActiveWorld();

        const molga::WindowMetrics windowMetrics = host->Metrics();
        const int framebufferWidth = windowMetrics.pixelWidth;
        const int framebufferHeight = windowMetrics.pixelHeight;
        const int windowWidth = windowMetrics.logicalWidth;
        const int windowHeight = windowMetrics.logicalHeight;
        const molga::PixelSize framebufferSize{framebufferWidth,
                                                framebufferHeight};
        const molga::PixelSize configuredLogicalSize{config.windowWidth,
                                                      config.windowHeight};
        const molga::OutputPresentationLayout presentation =
            molga::OutputPresentationLayout::Calculate(
                config.outputScaleMode, configuredLogicalSize,
                framebufferSize);

        const molga::WindowPointerState windowPointer = host->Pointer();
        const float windowPointerX = windowPointer.x;
        const float windowPointerY = windowPointer.y;
        const bool windowFocused = windowMetrics.focused && windowPointer.valid;
        const bool canMapWindowPointer = windowFocused && windowWidth > 0 &&
                                         windowHeight > 0 &&
                                         framebufferSize.IsValid();
        const float framebufferPointerX = canMapWindowPointer
            ? static_cast<float>(windowPointerX) *
                  static_cast<float>(framebufferWidth) /
                  static_cast<float>(windowWidth)
            : 0.0f;
        const float framebufferPointerY = canMapWindowPointer
            ? static_cast<float>(windowPointerY) *
                  static_cast<float>(framebufferHeight) /
                  static_cast<float>(windowHeight)
            : 0.0f;
        const auto logicalPointer = canMapWindowPointer
            ? presentation.FramebufferToLogical(framebufferPointerX,
                                                framebufferPointerY)
            : std::nullopt;
        const float mappedMouseX = logicalPointer
            ? static_cast<float>(logicalPointer->x) : 0.0f;
        const float mappedMouseY = logicalPointer
            ? static_cast<float>(logicalPointer->y) : 0.0f;
        InputSnapshot inputSnapshot = Input::CaptureSnapshot(
            host->WindowId(), mappedMouseX, mappedMouseY,
            logicalPointer.has_value());
        const molga::CameraOutputLayout cameraLayout =
            molga::CameraOutputLayout::Build(
                world.Objects(), presentation.logicalSize);
        if (logicalPointer) {
            const auto cameraPointer =
                cameraLayout.LogicalToTopmost(*logicalPointer);
            if (cameraPointer) {
                inputSnapshot.cameraPointerValid = true;
                inputSnapshot.pointerCameraObjectId =
                    cameraPointer->cameraObjectId;
                inputSnapshot.cameraPointerX = cameraPointer->cameraX;
                inputSnapshot.cameraPointerY = cameraPointer->cameraY;
                inputSnapshot.worldPointerX = cameraPointer->worldX;
                inputSnapshot.worldPointerY = cameraPointer->worldY;
            }
        }
        Input::ApplySnapshot(inputSnapshot);

        // UI capture follows the physical button edge, independently from the
        // script snapshot's pointer invalidation. Leaving a bar releases UI
        // capture, and re-entering while still held cannot synthesize a press.
        const bool rawMouseDown = windowPointer.leftDown;

        const Vector2 uiViewport{
            static_cast<float>(presentation.logicalSize.width),
            static_cast<float>(presentation.logicalSize.height)};
        UIPointerState uiPointer{
            {mappedMouseX, mappedMouseY},
            logicalPointer.has_value() && rawMouseDown,
            logicalPointer.has_value() && rawMouseDown && !rawMouseWasDown,
            logicalPointer.has_value() && !rawMouseDown && rawMouseWasDown,
            logicalPointer.has_value()};
        bool releasedSmokeAction = false;
        SmokeUIAction releasedAction = SmokeUIAction::SaveOption;
        if (smoke->enabled && smokeUIActionIndex < kSmokeUIActions.size()) {
            const SmokeUIAction action = kSmokeUIActions[smokeUIActionIndex];
            SmokeUITarget target = FindSmokeUITarget(world, action);
            if (!target.object || !target.rect) {
                smokeUIFlowOk = false;
                uiPointer = {{}, false, false, false, false};
            } else {
                const AABB screenRect = target.rect->GetScreenRect(uiViewport);
                uiPointer.position = {
                    screenRect.x + screenRect.width * 0.5f,
                    screenRect.y + screenRect.height * 0.5f};
                uiPointer.valid = true;
                if (smokePressPhase) {
                    // Establish a clean baseline before the real pointer click;
                    // only the authored script is allowed to create the value.
                    if (target.prefs) {
                        expectedPreferenceKey = target.prefs->key;
                        expectedPreferenceValue = target.prefs->value;
                        PlayerPrefs::DeleteKey(expectedPreferenceKey);
                        if (!PlayerPrefs::Save()) smokeUIFlowOk = false;
                    } else if (target.slot) {
                        expectedSlotName = target.slot->slotName;
                        expectedSlotPayload = target.slot->BuildPayload();
                        if (SaveSystem::SlotExists(expectedSlotName) &&
                            !SaveSystem::DeleteSlot(expectedSlotName)) {
                            smokeUIFlowOk = false;
                        }
                    }
                    uiPointer.down = true;
                    uiPointer.pressedThisFrame = true;
                    uiPointer.releasedThisFrame = false;
                    smokePressPhase = false;
                } else {
                    uiPointer.down = false;
                    uiPointer.pressedThisFrame = false;
                    uiPointer.releasedThisFrame = true;
                    releasedSmokeAction = true;
                    releasedAction = action;
                    smokePressPhase = true;
                }
            }
        } else if (smoke->enabled) {
            uiPointer = {{}, false, false, false, false};
        }

        if (!uiPointer.valid) UISystem::Get().ResetPointerCapture();
        UISystem::Get().ProcessInput(world, uiViewport, uiPointer);
        rawMouseWasDown = rawMouseDown;

        if (releasedSmokeAction) {
            if (releasedAction == SmokeUIAction::SaveOption) {
                scriptDrivenPrefsSaved = !expectedPreferenceKey.empty() &&
                    PlayerPrefs::HasKey(expectedPreferenceKey) &&
                    PlayerPrefs::GetBool(expectedPreferenceKey,
                                         !expectedPreferenceValue) ==
                        expectedPreferenceValue &&
                    !PlayerPrefs::IsDirty();
                if (!scriptDrivenPrefsSaved) smokeUIFlowOk = false;
            } else if (releasedAction == SmokeUIAction::SaveCompletion) {
                nlohmann::json restored;
                scriptDrivenSlotSaved = !expectedSlotName.empty() &&
                    SaveSystem::SlotExists(expectedSlotName) &&
                    SaveSystem::LoadSlot(expectedSlotName, restored) &&
                    restored == expectedSlotPayload;
                if (!scriptDrivenSlotSaved) smokeUIFlowOk = false;
            } else if (sceneRuntime.IsSceneLoadPending()) {
                smokeUITransitionAwaitingCommit = true;
            } else {
                smokeUIFlowOk = false;
            }
            ++smokeUIActionIndex;
        }

        // Fixed Update loop
        Time::AccumulateFixedTime(dt);
        while (Time::HasPendingFixedStep()) {
            world.FixedStep(Time::GetFixedDeltaTime());
            Time::ConsumeFixedStep();
        }

        // Update all game objects
        world.Update(dt);
        world.EvaluateAnimations(dt);
        world.LateUpdate(dt);
        world.FlushDeferred(dt);
        Audio::Update(dt);

        bool frameAvailable = false;
        molga::BeginFrameResult acquired = host->BeginFrame();
        if (acquired.status == molga::FrameAcquireStatus::Fatal) {
            std::cerr << "GPU frame acquisition failed: " << acquired.error << '\n';
            host->RequestClose();
            break;
        }
        if (acquired.status == molga::FrameAcquireStatus::Acquired) {
            frameAvailable = renderer->BeginFrame(
                std::move(acquired.frame), &rendererError);
            if (!frameAvailable) {
                std::cerr << "GPU frame setup failed: " << rendererError << '\n';
                host->RequestClose();
                break;
            }
        }

        {
            MOLGA_PROFILE_SCOPE("GameOutput.Render", molga::ProfileCategory::Rendering);
            if (frameAvailable && framebufferSize.IsValid()) {
                lastGameOutputResult = gameOutputRenderer->Render(
                    world,
                    {framebufferSize, configuredLogicalSize,
                     config.outputScaleMode},
                    *renderer, shader, TextRenderer::Get(), textDiagnostics);
            }
        }

        if (frameAvailable && !renderer->SubmitFrame(&rendererError)) {
            std::cerr << "GPU frame submission failed: " << rendererError << '\n';
            host->RequestClose();
            break;
        }

        EventBus::ProcessQueue();

        if (sceneRuntime.IsSceneLoadPending()) {
            if (sceneRuntime.CommitPendingLoad()) {
                Time::ResetFixedAccumulator();
                Input::ReleaseAll();
                UISystem::Get().ResetPointerCapture();
                if (smoke->enabled) {
                    ++successfulSceneTransitions;
                    if (smokeUITransitionAwaitingCommit) {
                        ++uiDrivenSceneTransitions;
                        smokeUITransitionAwaitingCommit = false;
                    } else {
                        smokeUIFlowOk = false;
                    }
                }
            } else if (smoke->enabled) {
                smokeUIFlowOk = false;
                smokeUITransitionAwaitingCommit = false;
            }
        }

        // ESC to quit
        if (Input::GetKeyDown(Input::KeyCode::Escape)) {
            host->RequestClose();
        }

        if (smoke->enabled && renderedFrames >= kBenchmarkWarmupFrames &&
            static_cast<int>(benchmarkCpuMilliseconds.size()) <
                kBenchmarkMeasuredFrames) {
            const auto elapsed = std::chrono::steady_clock::now() - frameCpuStart;
            benchmarkCpuMilliseconds.push_back(
                std::chrono::duration<double, std::milli>(elapsed).count());
            const auto& telemetry = renderer->LastFrameTelemetry();
            benchmarkGpuTelemetry.copyPasses += telemetry.copyPasses;
            benchmarkGpuTelemetry.renderPasses += telemetry.renderPasses;
            benchmarkGpuTelemetry.drawCalls += telemetry.drawCalls;
            benchmarkGpuTelemetry.uploadBytes += telemetry.uploadBytes;
        }

        ++renderedFrames;
        if (smoke->enabled && renderedFrames >= smoke->frames) {
            break;
        }
    }

    int exitCode = 0;
    if (smoke->enabled) {
        World& world = sceneRuntime.ActiveWorld();
        // Exercise the final authored stage through one deterministic fixed
        // tick. This synchronizes its static terrain and dynamic player into
        // the persistent Box2D backend and runs PlatformerController via the
        // same public lifecycle used during normal play.
        world.FixedStep(Time::GetFixedDeltaTime());
        const PackagedPhysicsProbe packagedPhysicsProbe =
            ProbePackagedStagePhysics(world);
        int scriptComponentCount = 0;
        for (const auto& obj : world.Objects()) {
            if (!obj) continue;
            for (auto* comp : obj->GetComponents()) {
                if (comp && ScriptManager::Get().IsDynamicScript(comp->GetTypeName())) {
                    scriptComponentCount++;
                }
            }
        }

        std::string scriptStatus = config.scripts.enabled ? "loaded" : "none";
        const AssetResolutionSummary assetSummary = SummarizeSpriteAssetResolution(world);
        const FontResolutionSummary fontSummary = SummarizeFontAssetResolution(world);
        const int uiComponentsLoaded = CountUIComponents(world);
        const int platformerPlayersLoaded = CountPlatformerPlayers(world);
        const int physicsBodiesLoaded = world.GetPhysicsWorld()
            ? static_cast<int>(world.GetPhysicsWorld()->BodyCount()) : 0;
        const int physicsShapesLoaded = world.GetPhysicsWorld()
            ? static_cast<int>(world.GetPhysicsWorld()->ShapeCount()) : 0;
        // Re-read preferences from disk so this cannot pass on an unsaved
        // process-local cache value.
        PlayerPrefs::ResetCacheForTesting();
        scriptDrivenPrefsSaved = scriptDrivenPrefsSaved &&
            !expectedPreferenceKey.empty() &&
            PlayerPrefs::HasKey(expectedPreferenceKey) &&
            PlayerPrefs::GetBool(expectedPreferenceKey,
                                 !expectedPreferenceValue) ==
                expectedPreferenceValue;
        nlohmann::json restoredSlot;
        scriptDrivenSlotSaved = scriptDrivenSlotSaved &&
            !expectedSlotName.empty() && SaveSystem::SlotExists(expectedSlotName) &&
            SaveSystem::LoadSlot(expectedSlotName, restoredSlot) &&
            restoredSlot == expectedSlotPayload;
        const bool scriptDrivenPersistence =
            scriptDrivenPrefsSaved && scriptDrivenSlotSaved;
        const bool transitionsOk = smokeUIFlowOk &&
            smokeUIActionIndex == kSmokeUIActions.size() &&
            !smokeUITransitionAwaitingCommit &&
            successfulSceneTransitions == 2 &&
            uiDrivenSceneTransitions == 2;

        SmokeReport report;
        report.executable = "molga_runtime";
        report.status = assetSummary.ok() ? "ok" : "error";
        report.scenePath = sceneRuntime.CurrentScenePath();
        report.objectCount = world.Objects().size();
        report.frames = renderedFrames;
        const auto& deviceInfo = host->Graphics().Info();
        report.graphicsApi = deviceInfo.api;
        report.graphicsDriver = deviceInfo.driver;
        report.osVersion = RuntimeOsVersion();
        report.architecture = RuntimeArchitecture();
        report.swapchainFormat = molga::TextureFormatName(
            deviceInfo.swapchainFormat);
        report.shaderArtifactFormat = config.graphics.shaderFormat;
        report.shaderManifestSha256 =
            ShaderManager::Get().ManifestSha256();
        const auto& gpuTelemetry = renderer->LastFrameTelemetry();
        report.gpuCopyPasses = gpuTelemetry.copyPasses;
        report.gpuRenderPasses = gpuTelemetry.renderPasses;
        report.gpuDrawCalls = gpuTelemetry.drawCalls;
        report.gpuUploadBytes = gpuTelemetry.uploadBytes;
        report.gpuValidationEnabled = deviceInfo.validationEnabled;
        molga::TextureDescriptor outputDescriptor;
        const molga::TextureView outputView =
            gameOutputRenderer->LogicalColorView();
        if (host->Graphics().Describe(outputView.texture, outputDescriptor)) {
            report.outputTextureFormat =
                molga::TextureFormatName(outputDescriptor.format);
            const molga::PixelSize outputSize =
                gameOutputRenderer->LogicalFramebufferSize();
            std::vector<std::uint8_t> pixel;
            std::string pixelError;
            if (outputSize.IsValid() && host->Graphics().ReadbackRGBA8(
                    outputView,
                    {static_cast<std::uint32_t>(outputSize.width / 2),
                     static_cast<std::uint32_t>(outputSize.height / 2), 1, 1},
                    pixel, pixelError) && pixel.size() == 4U) {
                report.finalPixelProbeValid = true;
                report.finalPixelR = pixel[0];
                report.finalPixelG = pixel[1];
                report.finalPixelB = pixel[2];
                report.finalPixelA = pixel[3];
            }
        }
        report.benchmarkWarmupFrames = std::min(
            renderedFrames, kBenchmarkWarmupFrames);
        report.benchmarkMeasuredFrames = static_cast<int>(
            benchmarkCpuMilliseconds.size());
        report.benchmarkCpuP50Ms = Percentile(
            benchmarkCpuMilliseconds, 0.50);
        report.benchmarkCpuP95Ms = Percentile(
            benchmarkCpuMilliseconds, 0.95);
        if (benchmarkBaseCaptured) {
            const auto& finalStats = renderer->Stats();
            report.benchmarkDrawCalls =
                finalStats.drawCalls - benchmarkBaseStats.drawCalls;
            report.benchmarkBatches =
                finalStats.batches - benchmarkBaseStats.batches;
        }
        report.benchmarkRenderPasses = benchmarkGpuTelemetry.renderPasses;
        report.benchmarkUploadBytes = benchmarkGpuTelemetry.uploadBytes;
        report.residentMemoryBytes = ResidentMemoryBytes();
        report.peakMemoryBytes = PeakMemoryBytes();
        report.assetsResolved = assetSummary.ok();
        report.assetCatalogLoaded = assetCatalogLoaded;
        report.assetCatalogRecords = assetCatalogRecords;
        report.spriteAssetsResolved = assetSummary.resolved;
        report.spriteAssetsMissing = assetSummary.missing;
        report.sceneTransitions = successfulSceneTransitions;
        report.uiDrivenSceneTransitions = uiDrivenSceneTransitions;
        report.fontAssetsResolved = fontSummary.ok();
        report.fontAssetsResolvedCount = fontSummary.resolved;
        report.fontAssetsMissing = fontSummary.missing;
        report.koreanTitlePreserved = koreanTitleProbe.textPreserved;
        report.koreanFontGlyphsPresent = koreanTitleProbe.fontGlyphsPresent;
        report.koreanGlyphAtlasReady = koreanTitleProbe.atlasQuadsCollected;
        report.koreanGlyphQuads = koreanTitleProbe.glyphQuads;
        report.uiComponentsLoaded = uiComponentsLoaded;
        report.platformerPlayersLoaded = platformerPlayersLoaded;
        report.physicsBodiesLoaded = physicsBodiesLoaded;
        report.physicsShapesLoaded = physicsShapesLoaded;
        report.rotatedTerrainVerified = packagedPhysicsProbe.rotatedTerrainVerified;
        report.physicsContactObserved = packagedPhysicsProbe.contactObserved;
        report.restitutionResponseObserved =
            packagedPhysicsProbe.restitutionResponseObserved;
        report.frictionResponseObserved = packagedPhysicsProbe.frictionResponseObserved;
        report.saveRoundtrip = scriptDrivenPersistence;
        report.scriptDrivenPrefsSaved = scriptDrivenPrefsSaved;
        report.scriptDrivenSlotSaved = scriptDrivenSlotSaved;
        report.scriptDrivenPersistence = scriptDrivenPersistence;
        report.postProcessed = lastGameOutputResult.postProcessed;
        report.postProcessFallback = lastGameOutputResult.postProcessFallback;
        report.postProcessPasses = lastGameOutputResult.postProcessPasses;
        report.selectedCameraCount = static_cast<int>(
            lastGameOutputResult.cameraResults.size());
        for (const molga::CameraOutputResult& cameraResult :
             lastGameOutputResult.cameraResults) {
            if (cameraResult.rendered) ++report.renderedCameraCount;
            if (cameraResult.postProcessed) ++report.postProcessedCameraCount;
            if (cameraResult.postProcessFallback) {
                ++report.postProcessFallbackCameraCount;
            }
            if (cameraResult.lightingApplied)
                ++report.lightingAppliedCameraCount;
            if (cameraResult.lightingFallback)
                ++report.lightingFallbackCameraCount;
            if (cameraResult.shadowFallback)
                ++report.shadowFallbackCameraCount;
            report.selectedLightCount += cameraResult.selectedLightCount;
            report.shadowedLightCount += cameraResult.shadowedLightCount;
            report.shadowCasterDrawCount +=
                cameraResult.shadowCasterDrawCount;
            report.lightingPasses += cameraResult.lightingPasses;
            report.shadowPasses += cameraResult.shadowPasses;
        }
        // This is the exact final-frame output pass count. Renderer stats can
        // aggregate across several smoke frames, so they are not suitable for
        // the fixture's deterministic camera contract.
        report.outputCameraPasses = report.renderedCameraCount;
        if (lastGameOutputResult.mainCamera) {
            report.postProcessProfileGuid =
                lastGameOutputResult.mainCamera->GetPostProcessProfileGuid();
        }
        if (renderer) {
            auto& stats = renderer->Stats();
            report.drawCalls = stats.drawCalls;
            report.batches = stats.batches;
            report.textureBinds = stats.textureBinds;
            report.shaderSwitches = stats.shaderSwitches;
            report.submittedSprites = stats.submittedSprites;
            report.submittedCommands = stats.submittedCommands;
            report.batchFlushes = stats.batchFlushes;
            report.batchBreaks = stats.batchBreaks;
            report.maxSpritesPerBatch = stats.maxSpritesPerBatch;
            report.verticesUploadedBytes = stats.verticesUploadedBytes;
            report.queueSortNanos = stats.queueSortNanos;
        }
        std::string gpuIdleError;
        const bool gpuIdle = host->Graphics().WaitIdle(&gpuIdleError);
        report.gpuValidationErrors =
            host->Graphics().ValidationErrorCount();
        const bool smokeOk = report.assetsResolved && report.assetCatalogLoaded &&
                             report.scenePath == "Scenes/stage2.json" &&
                             report.spriteAssetsResolved > 0 &&
                             report.fontAssetsResolved &&
                             report.fontAssetsResolvedCount > 0 &&
                             koreanTitleProbe.ok() &&
                             report.uiComponentsLoaded > 0 &&
                             report.platformerPlayersLoaded > 0 &&
                             report.physicsBodiesLoaded >= 2 &&
                             report.physicsShapesLoaded >= 2 &&
                             packagedPhysicsProbe.ok() &&
                             report.scriptDrivenPersistence &&
                             report.postProcessed &&
                             !report.postProcessFallback &&
                             report.postProcessPasses > 0 &&
                             !report.postProcessProfileGuid.empty() &&
                             report.selectedCameraCount == 2 &&
                             report.renderedCameraCount == 2 &&
                             report.postProcessedCameraCount == 1 &&
                             report.postProcessFallbackCameraCount == 0 &&
                             report.lightingAppliedCameraCount == 1 &&
                             report.lightingFallbackCameraCount == 0 &&
                             report.shadowFallbackCameraCount == 0 &&
                             report.selectedLightCount == 1 &&
                             report.shadowedLightCount == 1 &&
                             report.shadowCasterDrawCount == 1 &&
                             report.lightingPasses > 0 &&
                             report.shadowPasses > 0 &&
                             report.graphicsApi == "sdlgpu" &&
                             report.graphicsDriver == "metal" &&
                             report.shaderArtifactFormat == "msl" &&
                             report.shaderManifestSha256 ==
                                 config.graphics.shaderManifestSha256 &&
                             gpuIdle && report.gpuValidationEnabled &&
                             report.gpuValidationErrors == 0 &&
                             report.finalPixelProbeValid &&
                             transitionsOk;
        report.status = smokeOk ? "ok" : "error";
        if (smokeOk) {
            report.message = "Runtime smoke completed. Scripts: " + scriptStatus + 
                             ", UserScriptComponents: " + std::to_string(scriptComponentCount);
        } else {
            report.message = "Runtime smoke contract failed. Scripts: " + scriptStatus +
                             ", UserScriptComponents: " +
                             std::to_string(scriptComponentCount);
            if (!gpuIdle) {
                report.message += ", GPU idle error: " + gpuIdleError;
            }
        }
        report.Save(smoke->reportPath);
        exitCode = smokeOk ? 0 : 4;
    }

    // Cleanup (unique_ptrs auto-release; explicit reset for deterministic order)
    sceneRuntime.Shutdown();
    UISystem::Get().ResetPointerCapture();
    PlayerPrefs::Shutdown();
    // Task 6.3/8.2 Step 7f: 텍스트/atlas GPU 자원은 renderer가 GPU idle을
    // 증명하고 반납 큐를 비운 다음에만 파괴되고, 텍스트 서비스는 그 다음,
    // guard의 종결 u_cleanup은 맨 마지막이다. 그 순서는 이 함수 한 곳에만
    // 적혀 있다.
    gameOutputRenderer.reset();
    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    // Task 11.2 Step 7f/7g: 종료 순서의 소유자는 host다. 진입점은 두 GPU
    // 소비자의 **이름만** 넘기고, 결과가 Complete가 아닌 동안에는 돌아가지
    // 않는다.
    host->RegisterGpuConsumers(renderer.get(), &TextRenderer::Get());
    ShutdownEngineOrExit(host, textDiagnostics);
    renderer.reset();

    return exitCode;
}

}  // namespace

int main(int argc, char* argv[]) {
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);
    RegisterBuiltinComponents();

    const std::optional<int> textSeam =
        ParseTextTestReturnAfterServices(argc, argv);
    const int code = RunRuntimeAfterPaths(argc, argv, textSeam.has_value(),
                                          textSeam.value_or(0));

    // The scoped function has returned, so the guard is destroyed. Gate on the
    // terminal state itself: !IsReady() is also true for a process whose guard
    // was never created, which would make this event prove nothing.
    if (textSeam) {
        if (molga::text::TextRuntimeDependencies::Get().WasTerminallyCleaned()) {
            EmitTextLifetimeEvent("u_cleanup");
        }
        EmitTextLifetimeEvent("process_return");
    }
    return code;
}
