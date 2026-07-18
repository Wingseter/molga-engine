#include "Particle.h"

#include "Common/Constants.h"
#include "Rendering/Camera2D.h"
#include "Rendering/Renderer.h"
#include "Rendering/Shader.h"
#include "Rendering/Sprite.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float Clamp01(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

Color SanitizeColor(const Color& color) {
    return {
        FiniteOr(color.r, 1.0f), FiniteOr(color.g, 1.0f),
        FiniteOr(color.b, 1.0f), FiniteOr(color.a, 1.0f)
    };
}

void SetVertex(molga::Vertex2D& vertex, float x, float y, float u, float v,
               const Particle& particle) {
    vertex = {x, y, u, v, particle.r, particle.g, particle.b, particle.a};
}

} // namespace

FloatCurve::FloatCurve(std::initializer_list<FloatCurveKey> initialKeys)
    : keys(initialKeys) {
    Normalize();
}

void FloatCurve::Normalize() {
    for (auto& key : keys) {
        key.time = Clamp01(key.time);
        key.value = FiniteOr(key.value, 0.0f);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.time < rhs.time;
    });
    std::vector<FloatCurveKey> normalized;
    normalized.reserve(keys.size());
    for (const auto& key : keys) {
        if (!normalized.empty() && normalized.back().time == key.time) {
            normalized.back() = key; // The last authored key wins.
        } else {
            normalized.push_back(key);
        }
    }
    keys = std::move(normalized);
}

float FloatCurve::Evaluate(float normalizedTime) const {
    if (keys.empty()) return 0.0f;
    const float time = Clamp01(normalizedTime);
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
        [](float value, const FloatCurveKey& key) { return value < key.time; });
    if (upper == keys.begin()) return upper->value;
    const auto lower = std::prev(upper);
    const float span = upper->time - lower->time;
    if (span <= 0.0f) return upper->value;
    const float t = (time - lower->time) / span;
    return lower->value + (upper->value - lower->value) * t;
}

FloatCurve FloatCurve::Linear(float start, float end) {
    return FloatCurve{{0.0f, start}, {1.0f, end}};
}

ColorGradient::ColorGradient(std::initializer_list<ColorGradientKey> initialKeys)
    : keys(initialKeys) {
    Normalize();
}

void ColorGradient::Normalize() {
    for (auto& key : keys) {
        key.time = Clamp01(key.time);
        key.color = SanitizeColor(key.color);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.time < rhs.time;
    });
    std::vector<ColorGradientKey> normalized;
    normalized.reserve(keys.size());
    for (const auto& key : keys) {
        if (!normalized.empty() && normalized.back().time == key.time) {
            normalized.back() = key;
        } else {
            normalized.push_back(key);
        }
    }
    keys = std::move(normalized);
}

Color ColorGradient::Evaluate(float normalizedTime) const {
    if (keys.empty()) return Color::White();
    const float time = Clamp01(normalizedTime);
    if (time <= keys.front().time) return keys.front().color;
    if (time >= keys.back().time) return keys.back().color;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
        [](float value, const ColorGradientKey& key) { return value < key.time; });
    if (upper == keys.begin()) return upper->color;
    const auto lower = std::prev(upper);
    const float span = upper->time - lower->time;
    if (span <= 0.0f) return upper->color;
    return Color::Lerp(lower->color, upper->color, (time - lower->time) / span);
}

ColorGradient ColorGradient::Linear(const Color& start, const Color& end) {
    return ColorGradient{{0.0f, start}, {1.0f, end}};
}

const char* ToString(ParticleFrameMode mode) {
    switch (mode) {
        case ParticleFrameMode::Random: return "Random";
        case ParticleFrameMode::OverLife: return "OverLife";
        case ParticleFrameMode::Start: return "Start";
    }
    return "Start";
}

const char* ToString(ParticleSimulationSpace space) {
    return space == ParticleSimulationSpace::Local ? "Local" : "World";
}

ParticleFrameMode ParticleFrameModeFromString(const std::string& value) {
    if (value == "Random") return ParticleFrameMode::Random;
    if (value == "OverLife") return ParticleFrameMode::OverLife;
    return ParticleFrameMode::Start;
}

ParticleSimulationSpace ParticleSimulationSpaceFromString(const std::string& value) {
    return value == "Local" ? ParticleSimulationSpace::Local
                            : ParticleSimulationSpace::World;
}

void ParticleConfig::Normalize() {
    spawnRate = std::max(0.0f, FiniteOr(spawnRate, 0.0f));
    maxParticles = std::max(0, maxParticles);
    spawnRadius = std::max(0.0f, FiniteOr(spawnRadius, 0.0f));
    minSpeed = FiniteOr(minSpeed, 0.0f);
    maxSpeed = FiniteOr(maxSpeed, minSpeed);
    if (minSpeed > maxSpeed) std::swap(minSpeed, maxSpeed);
    minAngle = FiniteOr(minAngle, 0.0f);
    maxAngle = FiniteOr(maxAngle, minAngle);
    if (minAngle > maxAngle) std::swap(minAngle, maxAngle);
    gravityX = FiniteOr(gravityX, 0.0f);
    gravityY = FiniteOr(gravityY, 0.0f);
    startSize = FiniteOr(startSize, 0.0f);
    endSize = FiniteOr(endSize, startSize);
    sizeVariance = std::max(0.0f, FiniteOr(sizeVariance, 0.0f));
    minRotationSpeed = FiniteOr(minRotationSpeed, 0.0f);
    maxRotationSpeed = FiniteOr(maxRotationSpeed, minRotationSpeed);
    if (minRotationSpeed > maxRotationSpeed) {
        std::swap(minRotationSpeed, maxRotationSpeed);
    }
    minLife = std::max(0.0001f, FiniteOr(minLife, 0.0001f));
    maxLife = std::max(0.0001f, FiniteOr(maxLife, minLife));
    if (minLife > maxLife) std::swap(minLife, maxLife);

    if (sizeOverLife.keys.empty()) {
        sizeOverLife = FloatCurve::Linear(startSize, endSize);
    } else {
        sizeOverLife.Normalize();
        startSize = sizeOverLife.Evaluate(0.0f);
        endSize = sizeOverLife.Evaluate(1.0f);
    }

    const Color legacyStart{startR, startG, startB, startA};
    const Color legacyEnd{endR, endG, endB, endA};
    if (colorOverLife.keys.empty()) {
        colorOverLife = ColorGradient::Linear(legacyStart, legacyEnd);
    } else {
        colorOverLife.Normalize();
    }
    const Color start = colorOverLife.Evaluate(0.0f);
    const Color end = colorOverLife.Evaluate(1.0f);
    startR = start.r; startG = start.g; startB = start.b; startA = start.a;
    endR = end.r; endG = end.g; endB = end.b; endA = end.a;
}

ParticleEmitter::ParticleEmitter() {
    SetConfig(config);
}

void ParticleEmitter::SetPosition(float positionX, float positionY) {
    x = FiniteOr(positionX, 0.0f);
    y = FiniteOr(positionY, 0.0f);
}

void ParticleEmitter::SetConfig(const ParticleConfig& newConfig) {
    config = newConfig;
    config.Normalize();
    particles.assign(static_cast<std::size_t>(config.maxParticles), Particle{});
    activeIndices_.clear();
    RebuildFreeList();
    spawnAccumulator = 0.0f;
    ResetRandom();
}

void ParticleEmitter::SetSeed(std::uint32_t seed) {
    config.seed = seed;
    ResetRandom();
}

void ParticleEmitter::ResetRandom() {
    randomState_ = config.seed == 0 ? 0x6d2b79f5U : config.seed;
}

void ParticleEmitter::SetSimulationSpace(ParticleSimulationSpace space) {
    if (config.simulationSpace == space) return;
    for (const std::size_t index : activeIndices_) {
        Particle& particle = particles[index];
        if (space == ParticleSimulationSpace::Local) {
            particle.x -= x;
            particle.y -= y;
        } else {
            particle.x += x;
            particle.y += y;
        }
    }
    config.simulationSpace = space;
}

void ParticleEmitter::Start() {
    emitting = true;
    paused_ = false;
}

void ParticleEmitter::Pause() {
    paused_ = true;
}

void ParticleEmitter::Resume() {
    paused_ = false;
}

void ParticleEmitter::Stop() {
    emitting = false;
    paused_ = false;
    spawnAccumulator = 0.0f;
}

void ParticleEmitter::RebuildFreeList() {
    freeIndices_.clear();
    freeIndices_.reserve(particles.size());
    for (std::size_t i = particles.size(); i > 0; --i) {
        freeIndices_.push_back(i - 1U);
    }
}

void ParticleEmitter::Clear() {
    for (const std::size_t index : activeIndices_) particles[index].active = false;
    activeIndices_.clear();
    RebuildFreeList();
    spawnAccumulator = 0.0f;
    ResetRandom();
}

void ParticleEmitter::Burst(int count) {
    const int emitCount = std::min(std::max(0, count), GetFreeCount());
    for (int i = 0; i < emitCount; ++i) EmitParticle();
}

std::uint32_t ParticleEmitter::NextRandom() {
    std::uint32_t value = randomState_;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    randomState_ = value;
    return value;
}

float ParticleEmitter::RandomFloat(float min, float max) {
    if (min > max) std::swap(min, max);
    if (min == max) return min;
    constexpr float inverse24Bit = 1.0f / 16777216.0f;
    const float unit = static_cast<float>(NextRandom() >> 8U) * inverse24Bit;
    return min + unit * (max - min);
}

bool ParticleEmitter::EmitParticle() {
    if (freeIndices_.empty()) return false;
    const std::size_t index = freeIndices_.back();
    freeIndices_.pop_back();
    Particle& particle = particles[index];

    const float spawnAngle = RandomFloat(0.0f, Constants::TWO_PI);
    const float radius = RandomFloat(0.0f, config.spawnRadius);
    const float offsetX = std::cos(spawnAngle) * radius;
    const float offsetY = std::sin(spawnAngle) * radius;
    if (config.simulationSpace == ParticleSimulationSpace::Local) {
        particle.x = offsetX;
        particle.y = offsetY;
    } else {
        particle.x = x + offsetX;
        particle.y = y + offsetY;
    }

    const float speed = RandomFloat(config.minSpeed, config.maxSpeed);
    const float velocityAngle = RandomFloat(config.minAngle, config.maxAngle);
    particle.vx = std::cos(velocityAngle) * speed;
    particle.vy = std::sin(velocityAngle) * speed;
    particle.sizeOffset = RandomFloat(-config.sizeVariance, config.sizeVariance);
    particle.size = std::max(0.0f, config.sizeOverLife.Evaluate(0.0f) + particle.sizeOffset);
    particle.rotation = RandomFloat(0.0f, Constants::TWO_PI);
    particle.rotationSpeed = RandomFloat(config.minRotationSpeed,
                                         config.maxRotationSpeed);
    particle.maxLife = RandomFloat(config.minLife, config.maxLife);
    particle.life = particle.maxLife;
    const Color color = config.colorOverLife.Evaluate(0.0f);
    particle.r = color.r; particle.g = color.g;
    particle.b = color.b; particle.a = color.a;
    particle.frameIndex = !config.sprites.empty() &&
                                  config.frameMode == ParticleFrameMode::Random
        ? NextRandom() % static_cast<std::uint32_t>(config.sprites.size())
        : 0U;
    particle.active = true;
    activeIndices_.push_back(index);
    return true;
}

void ParticleEmitter::Update(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f || paused_) return;

    if (emitting && config.spawnRate > 0.0f) {
        spawnAccumulator += config.spawnRate * dt;
        const float whole = std::floor(spawnAccumulator);
        if (whole >= 1.0f) {
            spawnAccumulator -= whole;
            const int requested = whole > static_cast<float>(std::numeric_limits<int>::max())
                ? std::numeric_limits<int>::max() : static_cast<int>(whole);
            Burst(requested);
        }
    }

    std::size_t active = 0;
    while (active < activeIndices_.size()) {
        const std::size_t index = activeIndices_[active];
        Particle& particle = particles[index];
        particle.life -= dt;
        if (particle.life <= 0.0f || !std::isfinite(particle.life)) {
            particle.active = false;
            freeIndices_.push_back(index);
            activeIndices_[active] = activeIndices_.back();
            activeIndices_.pop_back();
            continue;
        }

        const float normalizedAge = Clamp01(1.0f - particle.life / particle.maxLife);
        particle.vx += config.gravityX * dt;
        particle.vy += config.gravityY * dt;
        particle.x += particle.vx * dt;
        particle.y += particle.vy * dt;
        particle.rotation += particle.rotationSpeed * dt;
        particle.size = std::max(0.0f,
            config.sizeOverLife.Evaluate(normalizedAge) + particle.sizeOffset);
        const Color color = config.colorOverLife.Evaluate(normalizedAge);
        particle.r = color.r; particle.g = color.g;
        particle.b = color.b; particle.a = color.a;
        ++active;
    }
}

std::size_t ParticleEmitter::FrameIndexForParticle(const Particle& particle,
                                                   std::size_t frameCount) const {
    if (frameCount == 0) return 0;
    switch (config.frameMode) {
        case ParticleFrameMode::Random:
            return static_cast<std::size_t>(particle.frameIndex) % frameCount;
        case ParticleFrameMode::OverLife: {
            const float age = Clamp01(1.0f - particle.life / particle.maxLife);
            return std::min(static_cast<std::size_t>(age * static_cast<float>(frameCount)),
                            frameCount - 1U);
        }
        case ParticleFrameMode::Start:
            return 0;
    }
    return 0;
}

std::vector<ParticleGeometryBatch> ParticleEmitter::BuildGeometry(
    const std::vector<molga::ResolvedSprite>& resolvedSprites) const {
    struct WorkingBatch {
        Texture* texture = nullptr;
        std::shared_ptr<std::vector<molga::Vertex2D>> vertices =
            std::make_shared<std::vector<molga::Vertex2D>>();
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
    };

    std::vector<WorkingBatch> working;
    const bool textured = !config.sprites.empty();
    const std::size_t frameCount = textured ? config.sprites.size() : 0U;

    for (const std::size_t particleIndex : activeIndices_) {
        const Particle& particle = particles[particleIndex];
        if (!particle.active || !std::isfinite(particle.size) || particle.size <= 0.0f) {
            continue;
        }

        Texture* texture = nullptr;
        Frame uv{};
        Vector2 pivot{0.5f, 0.5f};
        if (textured) {
            const std::size_t frame = FrameIndexForParticle(particle, frameCount);
            if (frame >= resolvedSprites.size() || !resolvedSprites[frame].valid) continue;
            const auto& sprite = resolvedSprites[frame];
            texture = sprite.texture;
            uv = sprite.uv;
            pivot = sprite.pivot;
        }

        auto found = std::find_if(working.begin(), working.end(),
            [&](const WorkingBatch& batch) { return batch.texture == texture; });
        if (found == working.end()) {
            working.push_back({});
            found = std::prev(working.end());
            found->texture = texture;
            found->vertices->reserve(activeIndices_.size() * 4U);
        }

        const float centerX = particle.x +
            (config.simulationSpace == ParticleSimulationSpace::Local ? x : 0.0f);
        const float centerY = particle.y +
            (config.simulationSpace == ParticleSimulationSpace::Local ? y : 0.0f);
        const float left = -pivot.x * particle.size;
        const float right = (1.0f - pivot.x) * particle.size;
        const float top = -pivot.y * particle.size;
        const float bottom = (1.0f - pivot.y) * particle.size;
        const float cosine = std::cos(particle.rotation);
        const float sine = std::sin(particle.rotation);
        auto transform = [&](float localX, float localY) {
            return Vector2{centerX + localX * cosine - localY * sine,
                           centerY + localX * sine + localY * cosine};
        };
        const Vector2 topLeft = transform(left, top);
        const Vector2 topRight = transform(right, top);
        const Vector2 bottomRight = transform(right, bottom);
        const Vector2 bottomLeft = transform(left, bottom);

        auto& vertices = *found->vertices;
        const std::size_t offset = vertices.size();
        vertices.resize(offset + 4U);
        SetVertex(vertices[offset], topLeft.x, topLeft.y, uv.u0, uv.v1, particle);
        SetVertex(vertices[offset + 1U], topRight.x, topRight.y, uv.u1, uv.v1, particle);
        SetVertex(vertices[offset + 2U], bottomRight.x, bottomRight.y,
                  uv.u1, uv.v0, particle);
        SetVertex(vertices[offset + 3U], bottomLeft.x, bottomLeft.y,
                  uv.u0, uv.v0, particle);

        for (const Vector2 point : {topLeft, topRight, bottomRight, bottomLeft}) {
            found->minX = std::min(found->minX, point.x);
            found->minY = std::min(found->minY, point.y);
            found->maxX = std::max(found->maxX, point.x);
            found->maxY = std::max(found->maxY, point.y);
        }
    }

    std::vector<ParticleGeometryBatch> result;
    result.reserve(working.size());
    for (auto& batch : working) {
        if (batch.vertices->empty()) continue;
        ParticleGeometryBatch output;
        output.texture = batch.texture;
        output.geometry = std::move(batch.vertices);
        output.worldBounds = {batch.minX, batch.minY,
                              batch.maxX - batch.minX, batch.maxY - batch.minY};
        result.push_back(std::move(output));
    }
    return result;
}

void ParticleEmitter::Render(Renderer* renderer, Shader* shader, Camera2D* camera) {
    if (!renderer) return;
    renderer->Begin(shader, camera);
    for (const std::size_t index : activeIndices_) {
        const Particle& particle = particles[index];
        Sprite sprite;
        const float offsetX = config.simulationSpace == ParticleSimulationSpace::Local ? x : 0.0f;
        const float offsetY = config.simulationSpace == ParticleSimulationSpace::Local ? y : 0.0f;
        sprite.SetPosition(particle.x + offsetX - particle.size * 0.5f,
                           particle.y + offsetY - particle.size * 0.5f);
        sprite.SetSize(particle.size, particle.size);
        sprite.SetColor(particle.r, particle.g, particle.b, particle.a);
        sprite.SetRotation(particle.rotation * 180.0f / Constants::PI);
        renderer->DrawSprite(&sprite);
    }
    renderer->End();
}

namespace ParticlePresets {

ParticleConfig Fire() {
    ParticleConfig c;
    c.spawnRate = 30.0f;
    c.maxParticles = 200;
    c.spawnRadius = 10.0f;
    c.minSpeed = 30.0f;
    c.maxSpeed = 80.0f;
    c.minAngle = -2.0f;
    c.maxAngle = -1.14f;
    c.gravityY = -50.0f;
    c.startSize = 15.0f;
    c.endSize = 3.0f;
    c.minLife = 0.5f;
    c.maxLife = 1.2f;
    c.startR = 1.0f; c.startG = 0.6f; c.startB = 0.1f; c.startA = 1.0f;
    c.endR = 1.0f; c.endG = 0.2f; c.endB = 0.0f; c.endA = 0.0f;
    return c;
}

ParticleConfig Smoke() {
    ParticleConfig c;
    c.spawnRate = 15.0f;
    c.maxParticles = 100;
    c.spawnRadius = 5.0f;
    c.minSpeed = 20.0f;
    c.maxSpeed = 40.0f;
    c.minAngle = -1.8f;
    c.maxAngle = -1.3f;
    c.gravityY = -20.0f;
    c.startSize = 10.0f;
    c.endSize = 30.0f;
    c.minLife = 1.0f;
    c.maxLife = 2.5f;
    c.startR = 0.5f; c.startG = 0.5f; c.startB = 0.5f; c.startA = 0.6f;
    c.endR = 0.3f; c.endG = 0.3f; c.endB = 0.3f; c.endA = 0.0f;
    return c;
}

ParticleConfig Spark() {
    ParticleConfig c;
    c.spawnRate = 50.0f;
    c.maxParticles = 150;
    c.spawnRadius = 3.0f;
    c.minSpeed = 100.0f;
    c.maxSpeed = 200.0f;
    c.minAngle = 0.0f;
    c.maxAngle = Constants::TWO_PI;
    c.gravityY = 200.0f;
    c.startSize = 4.0f;
    c.endSize = 1.0f;
    c.minLife = 0.2f;
    c.maxLife = 0.6f;
    c.startR = 1.0f; c.startG = 0.9f; c.startB = 0.3f; c.startA = 1.0f;
    c.endR = 1.0f; c.endG = 0.4f; c.endB = 0.0f; c.endA = 0.0f;
    return c;
}

ParticleConfig Snow() {
    ParticleConfig c;
    c.spawnRate = 20.0f;
    c.maxParticles = 300;
    c.spawnRadius = 400.0f;
    c.minSpeed = 10.0f;
    c.maxSpeed = 30.0f;
    c.minAngle = 1.3f;
    c.maxAngle = 1.8f;
    c.gravityY = 20.0f;
    c.startSize = 5.0f;
    c.endSize = 5.0f;
    c.minRotationSpeed = -1.0f;
    c.maxRotationSpeed = 1.0f;
    c.minLife = 3.0f;
    c.maxLife = 6.0f;
    c.startR = 1.0f; c.startG = 1.0f; c.startB = 1.0f; c.startA = 0.8f;
    c.endR = 1.0f; c.endG = 1.0f; c.endB = 1.0f; c.endA = 0.0f;
    return c;
}

ParticleConfig Explosion() {
    ParticleConfig c;
    c.spawnRate = 0.0f;
    c.maxParticles = 100;
    c.spawnRadius = 5.0f;
    c.minSpeed = 150.0f;
    c.maxSpeed = 300.0f;
    c.minAngle = 0.0f;
    c.maxAngle = Constants::TWO_PI;
    c.gravityY = 100.0f;
    c.startSize = 12.0f;
    c.endSize = 2.0f;
    c.minLife = 0.3f;
    c.maxLife = 0.8f;
    c.startR = 1.0f; c.startG = 0.8f; c.startB = 0.2f; c.startA = 1.0f;
    c.endR = 0.8f; c.endG = 0.2f; c.endB = 0.0f; c.endA = 0.0f;
    return c;
}

} // namespace ParticlePresets
