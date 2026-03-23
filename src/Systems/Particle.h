#ifndef MOLGA_PARTICLE_H
#define MOLGA_PARTICLE_H

#include <vector>
#include <functional>
#include <cstdlib>
#include <cmath>

class Renderer;
class Shader;
class Camera2D;

struct Particle {
    float x, y;           // Position
    float vx, vy;         // Velocity
    float size;           // Current size
    float rotation;       // Rotation angle
    float rotationSpeed;  // Rotation speed
    float life;           // Remaining life (0-1)
    float maxLife;        // Total lifetime
    float r, g, b, a;     // Color
    bool active;
};

struct ParticleConfig {
    // Spawn settings
    float spawnRate = 10.0f;        // Particles per second
    int maxParticles = 100;

    // Position variance
    float spawnRadius = 0.0f;       // Random spawn radius

    // Velocity
    float minSpeed = 50.0f;
    float maxSpeed = 100.0f;
    float minAngle = 0.0f;          // Radians
    float maxAngle = 6.28f;         // Full circle

    // Gravity
    float gravityX = 0.0f;
    float gravityY = 0.0f;

    // Size
    float startSize = 10.0f;
    float endSize = 2.0f;
    float sizeVariance = 0.0f;

    // Rotation
    float minRotationSpeed = 0.0f;
    float maxRotationSpeed = 0.0f;

    // Life
    float minLife = 0.5f;
    float maxLife = 1.5f;

    // Color (start)
    float startR = 1.0f, startG = 1.0f, startB = 1.0f, startA = 1.0f;
    // Color (end)
    float endR = 1.0f, endG = 1.0f, endB = 1.0f, endA = 0.0f;
};

class ParticleEmitter {
public:
    ParticleEmitter();
    ~ParticleEmitter();

    void SetPosition(float x, float y);
    void SetConfig(const ParticleConfig& config);

    void Start();
    void Stop();
    void Burst(int count);  // Emit multiple particles at once

    void Update(float dt);
    void Render(Renderer* renderer, Shader* shader, Camera2D* camera = nullptr);

    bool IsActive() const { return emitting || GetActiveCount() > 0; }
    int GetActiveCount() const;

    float x, y;
    ParticleConfig config;
    bool emitting;

private:
    void EmitParticle();
    float RandomFloat(float min, float max);

    std::vector<Particle> particles;
    float spawnAccumulator;
};

// Preset particle effects
namespace ParticlePresets {
    ParticleConfig Fire();
    ParticleConfig Smoke();
    ParticleConfig Spark();
    ParticleConfig Snow();
    ParticleConfig Explosion();
}

#endif
