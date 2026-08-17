#pragma once

#include "Common/Constants.h"
#include "Common/Types.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/SpriteRef.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

class Camera2D;
class Renderer;
class Shader;
class Texture;

struct FloatCurveKey {
    float time = 0.0f;
    float value = 0.0f;
};

struct FloatCurve {
    std::vector<FloatCurveKey> keys;

    FloatCurve() = default;
    FloatCurve(std::initializer_list<FloatCurveKey> initialKeys);

    void Normalize();
    float Evaluate(float normalizedTime) const;
    static FloatCurve Linear(float start, float end);
};

struct ColorGradientKey {
    float time = 0.0f;
    Color color = Color::White();
};

struct ColorGradient {
    std::vector<ColorGradientKey> keys;

    ColorGradient() = default;
    ColorGradient(std::initializer_list<ColorGradientKey> initialKeys);

    void Normalize();
    Color Evaluate(float normalizedTime) const;
    static ColorGradient Linear(const Color& start, const Color& end);
};

enum class ParticleFrameMode {
    Start,
    Random,
    OverLife,
};

enum class ParticleSimulationSpace {
    Local,
    World,
};

const char* ToString(ParticleFrameMode mode);
const char* ToString(ParticleSimulationSpace space);
ParticleFrameMode ParticleFrameModeFromString(const std::string& value);
ParticleSimulationSpace ParticleSimulationSpaceFromString(const std::string& value);

struct Particle {
    float x = 0.0f, y = 0.0f; // World position or emitter-local position.
    float vx = 0.0f, vy = 0.0f;
    float size = 0.0f;
    float sizeOffset = 0.0f;
    float rotation = 0.0f;       // Radians.
    float rotationSpeed = 0.0f;  // Radians per second.
    float life = 0.0f;
    float maxLife = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    std::uint32_t frameIndex = 0;
    bool active = false;
};

struct ParticleConfig {
    // Spawn settings
    float spawnRate = 10.0f;
    int maxParticles = 100;
    float spawnRadius = 0.0f;

    // Velocity
    float minSpeed = 50.0f;
    float maxSpeed = 100.0f;
    float minAngle = 0.0f;
    float maxAngle = Constants::TWO_PI;

    // Gravity
    float gravityX = 0.0f;
    float gravityY = 0.0f;

    // Legacy size fields remain public and are migrated to sizeOverLife.
    float startSize = 10.0f;
    float endSize = 2.0f;
    float sizeVariance = 0.0f;
    FloatCurve sizeOverLife;

    // Rotation
    float minRotationSpeed = 0.0f;
    float maxRotationSpeed = 0.0f;

    // Particle lifetime. Emitter duration is owned separately by ParticleSystem.
    float minLife = 0.5f;
    float maxLife = 1.5f;

    // Legacy color fields remain public and are migrated to colorOverLife.
    float startR = 1.0f, startG = 1.0f, startB = 1.0f, startA = 1.0f;
    float endR = 1.0f, endG = 1.0f, endB = 1.0f, endA = 0.0f;
    ColorGradient colorOverLife;

    // P1 textured particle settings.
    std::vector<molga::SpriteRef> sprites;
    ParticleFrameMode frameMode = ParticleFrameMode::Start;
    ParticleSimulationSpace simulationSpace = ParticleSimulationSpace::World;
    std::uint32_t seed = 1;

    // Clamps invalid ranges and materializes legacy start/end values as curves.
    void Normalize();
};

struct ParticleGeometryBatch {
    Texture* texture = nullptr;
    std::shared_ptr<const std::vector<molga::Vertex2D>> geometry;
    AABB worldBounds;

    std::size_t QuadCount() const {
        return geometry ? geometry->size() / 4U : 0U;
    }
};

class ParticleEmitter {
public:
    ParticleEmitter();
    ~ParticleEmitter() = default;

    void SetPosition(float x, float y);
    void SetConfig(const ParticleConfig& config);
    void SetSeed(std::uint32_t seed);
    void ResetRandom();
    void SetSimulationSpace(ParticleSimulationSpace space);

    void Start();
    void Pause();
    void Resume();
    void Stop();
    void Clear();
    void Burst(int count);

    void Update(float dt);
    void Render(Renderer* renderer, Shader* shader, Camera2D* camera = nullptr);

    std::vector<ParticleGeometryBatch> BuildGeometry(
        const std::vector<molga::ResolvedSprite>& resolvedSprites) const;
    std::size_t FrameIndexForParticle(const Particle& particle,
                                      std::size_t frameCount) const;

    bool IsActive() const { return emitting || !activeIndices_.empty(); }
    int GetActiveCount() const { return static_cast<int>(activeIndices_.size()); }
    int GetFreeCount() const { return static_cast<int>(freeIndices_.size()); }
    const std::vector<Particle>& GetParticles() const { return particles; }
    bool IsEmitting() const { return emitting; }
    bool IsPaused() const { return paused_; }

    float x = 0.0f, y = 0.0f;
    ParticleConfig config;
    bool emitting = false;

private:
    bool EmitParticle();
    std::uint32_t NextRandom();
    float RandomFloat(float min, float max);
    void RebuildFreeList();

    std::vector<Particle> particles;
    std::vector<std::size_t> activeIndices_;
    std::vector<std::size_t> freeIndices_;
    float spawnAccumulator = 0.0f;
    std::uint32_t randomState_ = 1;
    bool paused_ = false;
};

namespace ParticlePresets {
    ParticleConfig Fire();
    ParticleConfig Smoke();
    ParticleConfig Spark();
    ParticleConfig Snow();
    ParticleConfig Explosion();
}
