#include "ECS/Components/ParticleSystem.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "Rendering/SpriteBatcher.h"
#include "doctest.h"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace {

ParticleConfig StableConfig(int maxParticles = 16) {
    ParticleConfig config;
    config.spawnRate = 0.0f;
    config.maxParticles = maxParticles;
    config.spawnRadius = 7.0f;
    config.minSpeed = 2.0f;
    config.maxSpeed = 9.0f;
    config.minAngle = -1.0f;
    config.maxAngle = 2.0f;
    config.gravityX = 1.5f;
    config.gravityY = -3.0f;
    config.startSize = 6.0f;
    config.endSize = 2.0f;
    config.sizeVariance = 0.5f;
    config.minRotationSpeed = -2.0f;
    config.maxRotationSpeed = 3.0f;
    config.minLife = 2.0f;
    config.maxLife = 4.0f;
    config.seed = 123456U;
    return config;
}

const Particle* FirstActive(const ParticleEmitter& emitter) {
    for (const auto& particle : emitter.GetParticles()) {
        if (particle.active) return &particle;
    }
    return nullptr;
}

Vector2 GeometryCenter(const ParticleGeometryBatch& batch) {
    if (batch.geometry == nullptr || batch.geometry->size() < 4U) {
        return {};
    }
    Vector2 center;
    for (std::size_t i = 0; i < 4U; ++i) {
        center.x += (*batch.geometry)[i].x;
        center.y += (*batch.geometry)[i].y;
    }
    return center / 4.0f;
}

molga::ResolvedSprite MakeResolved(Texture* texture, const Frame& uv,
                                   Vector2 pivot = {0.5f, 0.5f}) {
    molga::ResolvedSprite result;
    result.texture = texture;
    result.uv = uv;
    result.pivot = pivot;
    result.nativeSize = {16.0f, 16.0f};
    result.pixelRect = {0, 0, 16, 16};
    result.valid = true;
    return result;
}

} // namespace

TEST_CASE("ParticleSystem preserves legacy public defaults and sorting API") {
    ParticleSystem system;
    CHECK(system.playOnAwake);
    CHECK(system.looping);
    CHECK(system.presetName == "Custom");
    CHECK_FALSE(system.useAdditiveBlending);
    CHECK(system.GetSortingOrder() == 0);
    CHECK(system.GetBlendMode() == BlendMode::Alpha);

    system.SetSortingOrder(10);
    system.SetBlendMode(BlendMode::Additive);
    CHECK(system.GetSortingOrder() == 10);
    CHECK(system.useAdditiveBlending);
}

TEST_CASE("Particle presets keep the existing authored values") {
    ParticleConfig fire = ParticlePresets::Fire();
    CHECK(fire.spawnRate == doctest::Approx(30.0f));
    CHECK(fire.maxParticles == 200);
    CHECK(fire.startSize == doctest::Approx(15.0f));
}

TEST_CASE("FloatCurve normalizes keys and evaluates piecewise linearly") {
    FloatCurve curve{{1.0f, 10.0f}, {0.0f, 0.0f}, {0.5f, 3.0f}, {0.5f, 4.0f}};
    REQUIRE(curve.keys.size() == 3U);
    CHECK(curve.Evaluate(-1.0f) == doctest::Approx(0.0f));
    CHECK(curve.Evaluate(0.25f) == doctest::Approx(2.0f));
    CHECK(curve.Evaluate(0.5f) == doctest::Approx(4.0f));
    CHECK(curve.Evaluate(0.75f) == doctest::Approx(7.0f));
    CHECK(curve.Evaluate(2.0f) == doctest::Approx(10.0f));

    FloatCurve constant{{0.3f, 8.0f}};
    CHECK(constant.Evaluate(0.0f) == doctest::Approx(8.0f));
    CHECK(constant.Evaluate(1.0f) == doctest::Approx(8.0f));
}

TEST_CASE("ColorGradient interpolates normalized color keys") {
    ColorGradient gradient{
        {0.0f, Color(1.0f, 0.0f, 0.0f, 1.0f)},
        {1.0f, Color(0.0f, 0.0f, 1.0f, 0.0f)}
    };
    const Color middle = gradient.Evaluate(0.5f);
    CHECK(middle.r == doctest::Approx(0.5f));
    CHECK(middle.g == doctest::Approx(0.0f));
    CHECK(middle.b == doctest::Approx(0.5f));
    CHECK(middle.a == doctest::Approx(0.5f));
}

TEST_CASE("ParticleEmitter applies size curves and color gradients over lifetime") {
    ParticleConfig config;
    config.spawnRate = 0.0f;
    config.maxParticles = 1;
    config.spawnRadius = 0.0f;
    config.minSpeed = config.maxSpeed = 0.0f;
    config.minLife = config.maxLife = 4.0f;
    config.sizeVariance = 0.0f;
    config.sizeOverLife = FloatCurve{{0.0f, 2.0f}, {0.5f, 10.0f}, {1.0f, 4.0f}};
    config.colorOverLife = ColorGradient{
        {0.0f, Color(1.0f, 0.0f, 0.0f, 1.0f)},
        {0.5f, Color(0.0f, 1.0f, 0.0f, 0.5f)},
        {1.0f, Color(0.0f, 0.0f, 1.0f, 0.0f)}
    };

    ParticleEmitter emitter;
    emitter.SetConfig(config);
    emitter.Burst(1);
    emitter.Update(2.0f);
    const Particle* particle = FirstActive(emitter);
    REQUIRE(particle != nullptr);
    CHECK(particle->size == doctest::Approx(10.0f));
    CHECK(particle->r == doctest::Approx(0.0f));
    CHECK(particle->g == doctest::Approx(1.0f));
    CHECK(particle->b == doctest::Approx(0.0f));
    CHECK(particle->a == doctest::Approx(0.5f));
}

TEST_CASE("ParticleEmitter uses a deterministic per-emitter seed and pool") {
    const ParticleConfig config = StableConfig(8);
    ParticleEmitter first;
    ParticleEmitter second;
    first.SetConfig(config);
    second.SetConfig(config);
    first.SetPosition(10.0f, 20.0f);
    second.SetPosition(10.0f, 20.0f);
    first.Burst(6);
    second.Burst(6);
    first.Update(0.25f);
    second.Update(0.25f);

    CHECK(first.GetActiveCount() == 6);
    CHECK(first.GetFreeCount() == 2);
    REQUIRE(first.GetParticles().size() == second.GetParticles().size());
    for (std::size_t i = 0; i < first.GetParticles().size(); ++i) {
        const Particle& lhs = first.GetParticles()[i];
        const Particle& rhs = second.GetParticles()[i];
        CHECK(lhs.active == rhs.active);
        CHECK(lhs.x == rhs.x);
        CHECK(lhs.y == rhs.y);
        CHECK(lhs.vx == rhs.vx);
        CHECK(lhs.vy == rhs.vy);
        CHECK(lhs.life == rhs.life);
        CHECK(lhs.rotation == rhs.rotation);
        CHECK(lhs.frameIndex == rhs.frameIndex);
    }

    const std::vector<Particle> firstRun = first.GetParticles();
    first.Clear();
    first.Burst(6);
    first.Update(0.25f);
    for (std::size_t i = 0; i < firstRun.size(); ++i) {
        CHECK(first.GetParticles()[i].active == firstRun[i].active);
        CHECK(first.GetParticles()[i].x == firstRun[i].x);
        CHECK(first.GetParticles()[i].y == firstRun[i].y);
    }
}

TEST_CASE("ParticleEmitter Pause Resume Stop and Clear have distinct semantics") {
    ParticleEmitter emitter;
    ParticleConfig config = StableConfig(8);
    config.spawnRate = 20.0f;
    config.minLife = config.maxLife = 5.0f;
    emitter.SetConfig(config);
    emitter.Start();
    emitter.Update(0.1f);
    REQUIRE(emitter.GetActiveCount() == 2);
    const Particle beforePause = *FirstActive(emitter);

    emitter.Pause();
    CHECK(emitter.IsPaused());
    CHECK(emitter.IsEmitting());
    emitter.Update(1.0f);
    const Particle afterPause = *FirstActive(emitter);
    CHECK(afterPause.life == beforePause.life);
    CHECK(afterPause.x == beforePause.x);

    emitter.Resume();
    emitter.Update(0.1f);
    CHECK(FirstActive(emitter)->life < beforePause.life);

    emitter.Stop();
    CHECK_FALSE(emitter.IsEmitting());
    CHECK(emitter.GetActiveCount() > 0); // Stop only stops emission.
    emitter.Clear();
    CHECK(emitter.GetActiveCount() == 0);
    CHECK(emitter.GetFreeCount() == config.maxParticles);
}

TEST_CASE("ParticleSystem separates emitter duration from particle lifetime") {
    auto object = std::make_shared<GameObject>("DurationEmitter");
    object->AddComponent<Transform>();
    auto* system = object->AddComponent<ParticleSystem>();
    system->playOnAwake = false;
    system->looping = false;
    system->durationSeconds = 0.25f;
    system->config.spawnRate = 20.0f;
    system->config.maxParticles = 32;
    system->config.minLife = system->config.maxLife = 2.0f;
    system->GetEmitter().SetConfig(system->config);

    system->Play();
    system->Update(0.3f);
    CHECK_FALSE(system->IsPlaying());
    CHECK(system->GetEmitter().GetActiveCount() > 0);
    const Particle* particle = FirstActive(system->GetEmitter());
    REQUIRE(particle != nullptr);
    CHECK(particle->life > 1.0f); // It survives after emission duration ends.

    system->Clear();
    CHECK(system->GetEmitter().GetActiveCount() == 0);
}

TEST_CASE("ParticleSystem legacy duration fallback remains source compatible") {
    auto object = std::make_shared<GameObject>("LegacyEmitter");
    object->AddComponent<Transform>();
    auto* system = object->AddComponent<ParticleSystem>();
    system->playOnAwake = false;
    system->looping = false;
    system->config.maxLife = 0.5f;
    system->config.spawnRate = 10.0f;
    system->GetEmitter().SetConfig(system->config);
    system->Play();
    system->Update(0.3f);
    CHECK(system->IsPlaying());
    system->Update(0.3f);
    CHECK_FALSE(system->IsPlaying());
}

TEST_CASE("ParticleSystem schema v2 roundtrips curves sprites and playback settings") {
    ParticleSystem source;
    source.playOnAwake = false;
    source.looping = false;
    source.durationSeconds = 3.5f;
    source.presetName = "Snow";
    source.SetBlendMode(BlendMode::Additive);
    source.SetSortingOrder(5);
    source.config.spawnRate = 123.45f;
    source.config.maxParticles = 500;
    source.config.seed = 99U;
    source.config.simulationSpace = ParticleSimulationSpace::Local;
    source.config.frameMode = ParticleFrameMode::OverLife;
    source.config.sprites = {
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "11111111111111111111111111111111"},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "22222222222222222222222222222222"}
    };
    source.config.sizeOverLife = FloatCurve{{0.0f, 12.0f}, {0.4f, 8.0f}, {1.0f, 1.0f}};
    source.config.colorOverLife = ColorGradient{
        {0.0f, Color(1.0f, 1.0f, 0.0f, 1.0f)},
        {1.0f, Color(1.0f, 0.0f, 0.0f, 0.0f)}
    };

    nlohmann::json json;
    source.Serialize(json);
    CHECK(json["schemaVersion"] == 2);
    CHECK(json["blendMode"] == "Additive");
    CHECK(json["sprites"].size() == 2U);
    CHECK(json["config"]["sizeOverLife"]["keys"].size() == 3U);

    ParticleSystem restored;
    restored.Deserialize(json);
    CHECK_FALSE(restored.playOnAwake);
    CHECK_FALSE(restored.looping);
    CHECK(restored.durationSeconds == doctest::Approx(3.5f));
    CHECK(restored.GetBlendMode() == BlendMode::Additive);
    CHECK(restored.GetSortingOrder() == 5);
    CHECK(restored.config.spawnRate == doctest::Approx(123.45f));
    CHECK(restored.config.maxParticles == 500);
    CHECK(restored.config.seed == 99U);
    CHECK(restored.config.simulationSpace == ParticleSimulationSpace::Local);
    CHECK(restored.config.frameMode == ParticleFrameMode::OverLife);
    REQUIRE(restored.config.sprites.size() == 2U);
    CHECK(restored.config.sprites[1].sliceId == "22222222222222222222222222222222");
    CHECK(restored.config.sizeOverLife.Evaluate(0.4f) == doctest::Approx(8.0f));
    CHECK(restored.config.colorOverLife.Evaluate(1.0f).a == doctest::Approx(0.0f));
}

TEST_CASE("ParticleSystem migrates legacy start and end values to v2 keys") {
    nlohmann::json legacy = {
        {"playOnAwake", false},
        {"looping", false},
        {"useAdditiveBlending", true},
        {"config", {
            {"startSize", 14.0f}, {"endSize", 3.0f},
            {"minLife", 0.25f}, {"maxLife", 0.75f},
            {"startColor", {1.0f, 0.5f, 0.25f, 1.0f}},
            {"endColor", {0.0f, 0.0f, 1.0f, 0.0f}}
        }}
    };
    ParticleSystem system;
    system.Deserialize(legacy);
    CHECK(system.durationSeconds == doctest::Approx(0.75f));
    REQUIRE(system.config.sizeOverLife.keys.size() == 2U);
    REQUIRE(system.config.colorOverLife.keys.size() == 2U);
    CHECK(system.config.sizeOverLife.Evaluate(0.0f) == doctest::Approx(14.0f));
    CHECK(system.config.sizeOverLife.Evaluate(1.0f) == doctest::Approx(3.0f));
    CHECK(system.config.colorOverLife.Evaluate(0.0f).g == doctest::Approx(0.5f));
    CHECK(system.config.colorOverLife.Evaluate(1.0f).b == doctest::Approx(1.0f));

    nlohmann::json migrated;
    system.Serialize(migrated);
    CHECK(migrated["schemaVersion"] == 2);
    CHECK(migrated["config"]["sizeOverLife"]["keys"].size() == 2U);
    CHECK(migrated["config"]["colorOverLife"]["keys"].size() == 2U);
}

TEST_CASE("Particle geometry uses slice UVs and groups quads by texture") {
    ParticleConfig config;
    config.spawnRate = 0.0f;
    config.maxParticles = 2;
    config.minSpeed = config.maxSpeed = 0.0f;
    config.spawnRadius = 0.0f;
    config.minLife = config.maxLife = 2.0f;
    config.startSize = config.endSize = 10.0f;
    config.frameMode = ParticleFrameMode::OverLife;
    config.seed = 7U;
    config.sprites = {
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "11111111111111111111111111111111"},
        {"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "22222222222222222222222222222222"}
    };
    ParticleEmitter emitter;
    emitter.SetConfig(config);
    emitter.Burst(1);
    emitter.Update(0.75f); // first particle remains in frame 0
    emitter.Burst(1);
    emitter.Update(0.30f); // old particle frame 1, new particle frame 0

    Texture* firstTexture = reinterpret_cast<Texture*>(static_cast<std::uintptr_t>(0x10));
    Texture* secondTexture = reinterpret_cast<Texture*>(static_cast<std::uintptr_t>(0x20));
    std::vector<molga::ResolvedSprite> sprites{
        MakeResolved(firstTexture, Frame(0.10f, 0.20f, 0.30f, 0.40f)),
        MakeResolved(secondTexture, Frame(0.50f, 0.60f, 0.70f, 0.80f))
    };
    const auto batches = emitter.BuildGeometry(sprites);
    REQUIRE(batches.size() == 2U);
    CHECK(batches[0].QuadCount() == 1U);
    CHECK(batches[1].QuadCount() == 1U);

    const ParticleGeometryBatch* firstBatch = batches[0].texture == firstTexture
        ? &batches[0] : &batches[1];
    REQUIRE(firstBatch->geometry != nullptr);
    const auto& vertices = *firstBatch->geometry;
    REQUIRE(vertices.size() == 4U);
    CHECK(vertices[0].u == doctest::Approx(0.10f));
    CHECK(vertices[0].v == doctest::Approx(0.20f));
    CHECK(vertices[1].u == doctest::Approx(0.30f));
    CHECK(vertices[2].v == doctest::Approx(0.40f));
}

TEST_CASE("Particle frame modes select start random and over-life frames deterministically") {
    ParticleConfig config = StableConfig(4);
    config.sprites = {
        {"a", "0"}, {"a", "1"}, {"a", "2"}, {"a", "3"}
    };
    config.frameMode = ParticleFrameMode::Random;
    ParticleEmitter first;
    ParticleEmitter second;
    first.SetConfig(config);
    second.SetConfig(config);
    first.Burst(4);
    second.Burst(4);
    for (std::size_t i = 0; i < first.GetParticles().size(); ++i) {
        CHECK(first.GetParticles()[i].frameIndex == second.GetParticles()[i].frameIndex);
    }

    config.frameMode = ParticleFrameMode::Start;
    first.SetConfig(config);
    first.Burst(1);
    REQUIRE(FirstActive(first) != nullptr);
    CHECK(first.FrameIndexForParticle(*FirstActive(first), 4U) == 0U);

    config.frameMode = ParticleFrameMode::OverLife;
    config.minLife = config.maxLife = 4.0f;
    first.SetConfig(config);
    first.Burst(1);
    first.Update(2.1f);
    REQUIRE(FirstActive(first) != nullptr);
    CHECK(first.FrameIndexForParticle(*FirstActive(first), 4U) == 2U);
}

TEST_CASE("Local particles follow emitter movement while world particles do not") {
    ParticleConfig config;
    config.spawnRate = 0.0f;
    config.maxParticles = 1;
    config.minSpeed = config.maxSpeed = 0.0f;
    config.spawnRadius = 0.0f;
    config.minLife = config.maxLife = 10.0f;
    config.startSize = config.endSize = 2.0f;

    config.simulationSpace = ParticleSimulationSpace::World;
    ParticleEmitter world;
    world.SetConfig(config);
    world.SetPosition(10.0f, 20.0f);
    world.Burst(1);
    world.SetPosition(30.0f, 40.0f);
    const auto worldBatches = world.BuildGeometry({});
    REQUIRE(worldBatches.size() == 1U);
    const Vector2 worldCenter = GeometryCenter(worldBatches[0]);
    CHECK(worldCenter.x == doctest::Approx(10.0f));
    CHECK(worldCenter.y == doctest::Approx(20.0f));

    config.simulationSpace = ParticleSimulationSpace::Local;
    ParticleEmitter local;
    local.SetConfig(config);
    local.SetPosition(10.0f, 20.0f);
    local.Burst(1);
    local.SetPosition(30.0f, 40.0f);
    const auto localBatches = local.BuildGeometry({});
    REQUIRE(localBatches.size() == 1U);
    const Vector2 localCenter = GeometryCenter(localBatches[0]);
    CHECK(localCenter.x == doctest::Approx(30.0f));
    CHECK(localCenter.y == doctest::Approx(40.0f));
}

TEST_CASE("Particle multi-quad geometry respects the 2048 batch boundary") {
    ParticleConfig config;
    config.spawnRate = 0.0f;
    config.maxParticles = 2049;
    config.minSpeed = config.maxSpeed = 0.0f;
    config.spawnRadius = 0.0f;
    config.minLife = config.maxLife = 10.0f;
    config.startSize = config.endSize = 1.0f;
    ParticleEmitter emitter;
    emitter.SetConfig(config);
    emitter.Burst(2049);
    const auto batches = emitter.BuildGeometry({});
    REQUIRE(batches.size() == 1U);
    CHECK(batches[0].QuadCount() == 2049U);
    CHECK(molga::SpriteBatcher::RequiredBatchCount(batches[0].QuadCount()) == 2U);
}

TEST_CASE("ParticleSystem submits immutable Alpha or Additive geometry commands") {
    auto object = std::make_shared<GameObject>("RenderEmitter");
    object->AddComponent<Transform>();
    auto* system = object->AddComponent<ParticleSystem>();
    system->config = StableConfig(4);
    system->config.spawnRate = 0.0f;
    system->config.sprites.clear();
    system->GetEmitter().SetConfig(system->config);
    system->Emit(3);
    system->SetBlendMode(BlendMode::Additive);

    molga::RenderQueue queue;
    system->CollectRender(queue);
    REQUIRE(queue.GetCommands().size() == 1U);
    const auto& command = queue.GetCommands().front();
    CHECK(command.batchKey.blendMode == BlendMode::Additive);
    CHECK(command.batchKey.isBatchable);
    REQUIRE(command.geometry != nullptr);
    CHECK(command.geometry->size() == 12U);
    CHECK(command.worldBounds.has_value());
}

TEST_CASE("Editor preview emitter state is isolated from serialization and live pool") {
    auto object = std::make_shared<GameObject>("PreviewEmitter");
    object->AddComponent<Transform>()->SetPosition(50.0f, -100.0f);
    auto* system = object->AddComponent<ParticleSystem>();
    system->config = StableConfig(12);
    system->config.spawnRate = 0.0f;
    system->GetEmitter().SetConfig(system->config);

    nlohmann::json before;
    system->Serialize(before);
    ParticleEmitter& preview = system->GetEditorPreviewEmitter();
    preview.Start();
    preview.Burst(7);
    system->UpdateEditorPreview(0.25f);
    CHECK(preview.GetActiveCount() == 7);
    CHECK(system->GetEmitter().GetActiveCount() == 0);
    CHECK(system->TryGetEditorPreviewEmitter() != nullptr);

    nlohmann::json after;
    system->Serialize(after);
    CHECK(after == before);

    system->ResetEditorPreview();
    REQUIRE(system->TryGetEditorPreviewEmitter() != nullptr);
    CHECK(system->TryGetEditorPreviewEmitter()->GetActiveCount() == 0);
}

TEST_CASE("ParticleSystem syncs the emitter to its Transform") {
    auto object = std::make_shared<GameObject>("EmitterObj");
    auto* transform = object->AddComponent<Transform>();
    auto* system = object->AddComponent<ParticleSystem>();
    transform->SetPosition(50.0f, -100.0f);
    system->Update(0.1f);
    CHECK(system->GetEmitter().x == doctest::Approx(50.0f));
    CHECK(system->GetEmitter().y == doctest::Approx(-100.0f));
}
