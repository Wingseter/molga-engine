// Molga Engine Runtime - Standalone game player without editor
#include <glad/glad.h>
#include <GLFW/glfw3.h>

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
#include "Core/AssetDatabase.h"
#include "Core/PersistentStorage.h"
#include "Core/PlayerPrefs.h"
#include "Core/SaveSystem.h"
#include "UI/UISystem.h"
#include "Scripting/ScriptPackageLoader.h"
#include "Rendering/FontFace.h"
#include "Rendering/Utf8.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <optional>

GLFWwindow* g_window = nullptr;

namespace {

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

KoreanTitleProbe ProbeKoreanTitle(World& world) {
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
    result.textPreserved = true;

    const std::string& fontGuid = title->GetFontGuid();
    const auto fontPath = molga::AssetDatabase::Get().AbsoluteSourcePath(fontGuid);
    molga::FontFace face;
    if (fontGuid.empty() || fontPath.empty() || !face.LoadFromFile(fontPath)) {
        return result;
    }

    int drawableCodepoints = 0;
    bool sawHangul = false;
    bool allGlyphsPresent = true;
    for (const std::uint32_t codepoint : molga::DecodeUtf8(title->GetText())) {
        if (codepoint == ' ' || codepoint == '\n' || codepoint == '\r' ||
            codepoint == '\t') {
            continue;
        }
        sawHangul = sawHangul || (codepoint >= 0xAC00U && codepoint <= 0xD7A3U);
        allGlyphsPresent = allGlyphsPresent && face.HasGlyph(codepoint);
        ++drawableCodepoints;
    }
    result.fontGlyphsPresent = sawHangul && allGlyphsPresent && drawableCodepoints > 0;

    molga::RenderQueue proofQueue;
    TextDrawParams params;
    params.text = title->GetText();
    params.fontGuid = fontGuid;
    params.fontSizePx = title->GetFontSizePx();
    params.lineSpacing = title->GetLineSpacing();
    TextRenderer::Get().CollectText(proofQueue, params);
    result.glyphQuads = static_cast<int>(proofQueue.GetCommands().size());

    bool allQuadsUseAtlasTextures = !proofQueue.GetCommands().empty();
    for (const auto& command : proofQueue.GetCommands()) {
        allQuadsUseAtlasTextures = allQuadsUseAtlasTextures &&
                                   command.batchKey.texture != nullptr &&
                                   command.batchKey.isBatchable;
    }
    const int atlasPixelSize = std::max(
        1, std::min(static_cast<int>(std::lround(title->GetFontSizePx())), 512));
    result.atlasQuadsCollected = result.glyphQuads == drawableCodepoints &&
        allQuadsUseAtlasTextures &&
        TextRenderer::Get().GetAtlasPageCount(fontGuid, atlasPixelSize) > 0U;
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

    // First teleport well clear of the slope and tick once so an earlier trial's
    // contacts cannot influence this trial. The next teleport uses the exact same
    // position and velocity for the authored-material and zero-friction controls.
    playerTransform.SetWorldPosition(startPosition + outwardNormal * 160.0f);
    playerTransform.SetWorldRotation(slopeRotation);
    playerBody.SetVelocity(Vector2::Zero());
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

}  // namespace

int main(int argc, char* argv[]) {
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);
    RegisterBuiltinComponents();

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
    for (const auto& required : { "Assets", "Scenes", "Shaders" }) {
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
    wc.visible = !smoke->enabled;
    GLFWwindow* window = EngineInit(wc);
    if (!window) return -1;
    g_window = window;

    // Initialize renderer
    auto renderer = std::make_unique<Renderer>();
    renderer->Init();
    molga::RenderSystem2D::Get().Init();
    auto vertPath = PathService::Get().EngineResource("Shaders/default.vert").string();
    auto fragPath = PathService::Get().EngineResource("Shaders/default.frag").string();
    Shader* shader = ShaderManager::Get().Load("default", vertPath, fragPath);

    auto batchVertPath = PathService::Get().EngineResource("Shaders/batch.vert").string();
    auto batchFragPath = PathService::Get().EngineResource("Shaders/batch.frag").string();
    ShaderManager::Get().Load("batch", batchVertPath, batchFragPath);
    auto camera = std::make_unique<Camera2D>(static_cast<float>(config.windowWidth),
                                             static_cast<float>(config.windowHeight));
    auto uiCamera = std::make_unique<Camera2D>(static_cast<float>(config.windowWidth),
                                               static_cast<float>(config.windowHeight));
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
        camera.reset();
        molga::RenderSystem2D::Get().Shutdown();
        ShaderManager::Get().Shutdown();
        renderer.reset();
        EngineShutdown();
        return 4;
    }

    // Initialize text renderer
    TextRenderer::Get().Init();

    // Load asset catalog if present (runtime mode: read-only, no .meta creation)
    PathService::Get().SetAssetRoot(PathService::Get().ExecutableDir());
    bool assetCatalogLoaded = false;
    int assetCatalogRecords = 0;
    {
        auto catalogPath = PathService::Get().ExecutableDir() / "asset_catalog.json";
        if (std::filesystem::exists(catalogPath)) {
            assetCatalogLoaded = molga::AssetDatabase::Get().LoadCatalog(
                catalogPath, PathService::Get().ExecutableDir());
            assetCatalogRecords = static_cast<int>(molga::AssetDatabase::Get().RecordCount());
            if (assetCatalogLoaded) {
                std::cout << "Asset catalog loaded: " << assetCatalogRecords
                          << " records" << std::endl;
            } else {
                std::cerr << "Failed to load asset catalog: " << catalogPath << std::endl;
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
        TextRenderer::Get().Shutdown();
        camera.reset();
        molga::RenderSystem2D::Get().Shutdown();
        ShaderManager::Get().Shutdown();
        renderer.reset();
        EngineShutdown();
        return 4;
    }

    std::cout << "Loaded " << sceneRuntime.ActiveWorld().Objects().size()
              << " game objects" << std::endl;
    // Prove that the packaged startup scene retained its exact Hangul title and
    // that its GUID-backed font can rasterize those codepoints into real atlas
    // quads. Merely finding a font file in the catalog is not sufficient.
    const KoreanTitleProbe koreanTitleProbe =
        ProbeKoreanTitle(sceneRuntime.ActiveWorld());

    int renderedFrames = 0;
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
    // Main game loop
    while (!glfwWindowShouldClose(window)) {
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();
        World& world = sceneRuntime.ActiveWorld();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        framebufferWidth = std::max(framebufferWidth, 1);
        framebufferHeight = std::max(framebufferHeight, 1);
        const float pointerScaleX = windowWidth > 0
            ? static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth)
            : 1.0f;
        const float pointerScaleY = windowHeight > 0
            ? static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight)
            : 1.0f;
        const Vector2 uiViewport{static_cast<float>(framebufferWidth),
                                 static_cast<float>(framebufferHeight)};
        UIPointerState uiPointer{
            {Input::GetMouseX() * pointerScaleX, Input::GetMouseY() * pointerScaleY},
            Input::GetMouseButton(GLFW_MOUSE_BUTTON_LEFT),
            Input::GetMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT),
            Input::GetMouseButtonUp(GLFW_MOUSE_BUTTON_LEFT),
            true};
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

        UISystem::Get().ProcessInput(world, uiViewport, uiPointer);

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
        world.LateUpdate(dt);
        world.FlushDeferred(dt);

        // Collect render commands
        molga::RenderQueue queue;
        {
            MOLGA_PROFILE_SCOPE("RenderQueue.Collect", molga::ProfileCategory::Rendering);
            for (auto& obj : world.Objects()) {
                if (obj && obj->IsActive()) {
                    for (auto* comp : obj->GetComponents()) {
                        if (comp && comp->IsEnabled()) {
                            comp->CollectRender(queue);
                        }
                    }
                }
            }
        }

        Camera* mainCam = nullptr;
        for (const auto& obj : world.Objects()) {
            if (obj && obj->IsActive()) {
                if (auto cam = obj->GetComponent<Camera>()) {
                    if (cam->IsEnabled() && cam->IsMain()) {
                        if (!mainCam || cam->GetDepth() > mainCam->GetDepth()) {
                            mainCam = cam;
                        }
                    }
                }
            }
        }

        if (mainCam) {
            Color clearColor = mainCam->GetBackgroundColor();
            renderer->Clear(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
            {
                molga::RenderPass pass(*renderer, shader, mainCam->GetCamera2D());
                molga::RenderSystem2D::Get().Render(queue, renderer.get(), mainCam->GetCamera2D());
            }
        } else {
            renderer->Clear(0.1f, 0.1f, 0.15f, 1.0f);
            {
                molga::RenderPass pass(*renderer, shader, camera.get());
                molga::RenderSystem2D::Get().Render(queue, renderer.get(), camera.get());
            }
        }

        molga::RenderQueue uiQueue;
        UISystem::Get().CollectRender(
            world,
            {static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight)},
            uiQueue);
        if (!uiQueue.GetCommands().empty()) {
            uiCamera->SetScreenSize(static_cast<float>(framebufferWidth),
                                    static_cast<float>(framebufferHeight));
            molga::RenderPass pass(*renderer, shader, uiCamera.get());
            molga::RenderSystem2D::Get().Render(uiQueue, renderer.get(), uiCamera.get());
        }

        EventBus::ProcessQueue();

        if (sceneRuntime.IsSceneLoadPending()) {
            if (sceneRuntime.CommitPendingLoad()) {
                Time::ResetFixedAccumulator();
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

        glfwSwapBuffers(window);
        glfwPollEvents();

        // ESC to quit
        if (Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
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
                             report.scriptDrivenPersistence && transitionsOk;
        report.status = smokeOk ? "ok" : "error";
        if (smokeOk) {
            report.message = "Runtime smoke completed. Scripts: " + scriptStatus + 
                             ", UserScriptComponents: " + std::to_string(scriptComponentCount);
        } else {
            report.message = "Runtime smoke contract failed. Scripts: " + scriptStatus +
                             ", UserScriptComponents: " +
                             std::to_string(scriptComponentCount);
        }
        report.Save(smoke->reportPath);
        exitCode = smokeOk ? 0 : 4;
    }

    // Cleanup (unique_ptrs auto-release; explicit reset for deterministic order)
    sceneRuntime.Shutdown();
    UISystem::Get().ResetPointerCapture();
    PlayerPrefs::Shutdown();
    TextRenderer::Get().Shutdown();
    camera.reset();
    uiCamera.reset();
    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    renderer.reset();
    EngineShutdown();

    return exitCode;
}
