#pragma once

#include "../Component.h"
#include "../../Common/Types.h"

class Rigidbody2D : public Component {
public:
    COMPONENT_TYPE(Rigidbody2D)

    enum class BodyType {
        Static,
        Kinematic,
        Dynamic
    };

    Rigidbody2D() = default;

    // Getters and Setters
    BodyType GetBodyType() const { return bodyType; }
    void SetBodyType(BodyType type) { bodyType = type; }

    float GetGravityScale() const { return gravityScale; }
    void SetGravityScale(float scale) { gravityScale = scale; }

    float GetMass() const { return mass; }
    void SetMass(float m) { mass = m > 0.0f ? m : 1.0f; }

    float GetLinearDamping() const { return linearDamping; }
    void SetLinearDamping(float damping) { linearDamping = damping; }

    bool IsRotationFrozen() const { return freezeRotation; }
    void SetFreezeRotation(bool freeze) { freezeRotation = freeze; }

    Vector2 GetVelocity() const { return velocity; }
    void SetVelocity(const Vector2& vel) { velocity = vel; }

    // Physics methods
    void AddForce(const Vector2& force) { forceAccumulator += force; }
    void AddImpulse(const Vector2& impulse) { impulseAccumulator += impulse; }

    // Integration helper methods
    Vector2 GetForceAccumulator() const { return forceAccumulator; }
    Vector2 GetImpulseAccumulator() const { return impulseAccumulator; }
    void ClearForces() {
        forceAccumulator = Vector2::Zero();
        impulseAccumulator = Vector2::Zero();
    }

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    BodyType bodyType = BodyType::Static;
    float gravityScale = 1.0f;
    float mass = 1.0f;
    float linearDamping = 0.0f;
    bool freezeRotation = false;
    Vector2 velocity = Vector2::Zero();

    // Accumulated physics states (cleared at the end of each Step)
    Vector2 forceAccumulator = Vector2::Zero();
    Vector2 impulseAccumulator = Vector2::Zero();
};
