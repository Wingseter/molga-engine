#include "Particle.h"
#include "Renderer.h"
#include "Shader.h"
#include "Camera2D.h"
#include "Sprite.h"

ParticleEmitter::ParticleEmitter()
    : x(0), y(0), emitting(false), spawnAccumulator(0) {
}

ParticleEmitter::~ParticleEmitter() {
}

void ParticleEmitter::SetPosition(float x, float y) {
    this->x = x;
    this->y = y;
}

void ParticleEmitter::SetConfig(const ParticleConfig& config) {
    this->config = config;
    particles.resize(config.maxParticles);
    for (auto& p : particles) {
        p.active = false;
    }
}

void ParticleEmitter::Start() {
    emitting = true;
}

void ParticleEmitter::Stop() {
    emitting = false;
}

void ParticleEmitter::Burst(int count) {
    for (int i = 0; i < count; i++) {
        EmitParticle();
    }
}

float ParticleEmitter::RandomFloat(float min, float max) {
    return min + static_cast<float>(rand()) / RAND_MAX * (max - min);
}

void ParticleEmitter::EmitParticle() {
    for (auto& p : particles) {
        if (!p.active) {
            // Position with variance
            float angle = RandomFloat(0, 6.28318f);
            float radius = RandomFloat(0, config.spawnRadius);
            p.x = x + cos(angle) * radius;
            p.y = y + sin(angle) * radius;

            // Velocity
            float speed = RandomFloat(config.minSpeed, config.maxSpeed);
            float velAngle = RandomFloat(config.minAngle, config.maxAngle);
            p.vx = cos(velAngle) * speed;
            p.vy = sin(velAngle) * speed;

            // Size
            p.size = config.startSize + RandomFloat(-config.sizeVariance, config.sizeVariance);

            // Rotation
            p.rotation = RandomFloat(0, 6.28318f);
            p.rotationSpeed = RandomFloat(config.minRotationSpeed, config.maxRotationSpeed);

            // Life
            p.maxLife = RandomFloat(config.minLife, config.maxLife);
            p.life = p.maxLife;

            // Color (start values)
            p.r = config.startR;
            p.g = config.startG;
            p.b = config.startB;
            p.a = config.startA;

            p.active = true;
            break;
        }
    }
}

void ParticleEmitter::Update(float dt) {
    // Spawn new particles
    if (emitting) {
        spawnAccumulator += config.spawnRate * dt;
        while (spawnAccumulator >= 1.0f) {
            EmitParticle();
            spawnAccumulator -= 1.0f;
        }
    }

    // Update existing particles
    for (auto& p : particles) {
        if (!p.active) continue;

        // Update life
        p.life -= dt;
        if (p.life <= 0) {
            p.active = false;
            continue;
        }

        // Life ratio (1 = just born, 0 = about to die)
        float lifeRatio = p.life / p.maxLife;
        float deathRatio = 1.0f - lifeRatio;

        // Apply gravity
        p.vx += config.gravityX * dt;
        p.vy += config.gravityY * dt;

        // Update position
        p.x += p.vx * dt;
        p.y += p.vy * dt;

        // Update rotation
        p.rotation += p.rotationSpeed * dt;

        // Interpolate size
        p.size = config.startSize * lifeRatio + config.endSize * deathRatio;

        // Interpolate color
        p.r = config.startR * lifeRatio + config.endR * deathRatio;
        p.g = config.startG * lifeRatio + config.endG * deathRatio;
        p.b = config.startB * lifeRatio + config.endB * deathRatio;
        p.a = config.startA * lifeRatio + config.endA * deathRatio;
    }
}

void ParticleEmitter::Render(Renderer* renderer, Shader* shader, Camera2D* camera) {
    renderer->Begin(shader, camera);

    for (const auto& p : particles) {
        if (!p.active) continue;

        Sprite sprite;
        sprite.SetPosition(p.x - p.size / 2, p.y - p.size / 2);
        sprite.SetSize(p.size, p.size);
        sprite.SetColor(p.r, p.g, p.b, p.a);
        sprite.SetRotation(p.rotation);
        renderer->DrawSprite(&sprite);
    }

    renderer->End();
}

int ParticleEmitter::GetActiveCount() const {
    int count = 0;
    for (const auto& p : particles) {
        if (p.active) count++;
    }
    return count;
}

// ============ Presets ============

namespace ParticlePresets {

ParticleConfig Fire() {
    ParticleConfig c;
    c.spawnRate = 30.0f;
    c.maxParticles = 200;
    c.spawnRadius = 10.0f;
    c.minSpeed = 30.0f;
    c.maxSpeed = 80.0f;
    c.minAngle = -2.0f;  // Upward
    c.maxAngle = -1.14f;
    c.gravityY = -50.0f;  // Rise up
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
    c.maxAngle = 6.28f;
    c.gravityY = 200.0f;  // Fall down
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
    c.spawnRadius = 400.0f;  // Wide spawn area
    c.minSpeed = 10.0f;
    c.maxSpeed = 30.0f;
    c.minAngle = 1.3f;  // Downward
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
    c.spawnRate = 0.0f;  // Use Burst() instead
    c.maxParticles = 100;
    c.spawnRadius = 5.0f;
    c.minSpeed = 150.0f;
    c.maxSpeed = 300.0f;
    c.minAngle = 0.0f;
    c.maxAngle = 6.28f;
    c.gravityY = 100.0f;
    c.startSize = 12.0f;
    c.endSize = 2.0f;
    c.minLife = 0.3f;
    c.maxLife = 0.8f;
    c.startR = 1.0f; c.startG = 0.8f; c.startB = 0.2f; c.startA = 1.0f;
    c.endR = 0.8f; c.endG = 0.2f; c.endB = 0.0f; c.endA = 0.0f;
    return c;
}

}
