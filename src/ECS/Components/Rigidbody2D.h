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
    void SetLinearDamping(float damping) { linearDamping = damping > 0.0f ? damping : 0.0f; }

    float GetAngularDamping() const { return angularDamping; }
    void SetAngularDamping(float damping) { angularDamping = damping > 0.0f ? damping : 0.0f; }

    bool IsRotationFrozen() const { return freezeRotation; }
    void SetFreezeRotation(bool freeze) { freezeRotation = freeze; }

    Vector2 GetVelocity() const { return velocity; }
    void SetVelocity(const Vector2& vel) { velocity = vel; }

    // Engine-facing angular velocity is expressed in degrees per second,
    // matching Transform's degree-based rotation API.
    float GetAngularVelocity() const { return angularVelocity; }
    void SetAngularVelocity(float degreesPerSecond) { angularVelocity = degreesPerSecond; }

    // Physics methods
    void AddForce(const Vector2& force) { forceAccumulator += force; }
    void AddImpulse(const Vector2& impulse) { impulseAccumulator += impulse; }
    void AddTorque(float torque) { torqueAccumulator += torque; }
    void AddAngularImpulse(float impulse) { angularImpulseAccumulator += impulse; }

    // Integration helper methods
    Vector2 GetForceAccumulator() const { return forceAccumulator; }
    Vector2 GetImpulseAccumulator() const { return impulseAccumulator; }
    float GetTorqueAccumulator() const { return torqueAccumulator; }
    float GetAngularImpulseAccumulator() const { return angularImpulseAccumulator; }
    void ClearForces() {
        forceAccumulator = Vector2::Zero();
        impulseAccumulator = Vector2::Zero();
        torqueAccumulator = 0.0f;
        angularImpulseAccumulator = 0.0f;
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
    float angularDamping = 0.0f;
    bool freezeRotation = false;
    Vector2 velocity = Vector2::Zero();
    float angularVelocity = 0.0f;

    // Accumulated physics states (cleared at the end of each Step)
    Vector2 forceAccumulator = Vector2::Zero();
    Vector2 impulseAccumulator = Vector2::Zero();
    float torqueAccumulator = 0.0f;
    float angularImpulseAccumulator = 0.0f;
};
