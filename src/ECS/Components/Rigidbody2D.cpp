#include "Rigidbody2D.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include <nlohmann/json.hpp>

REGISTER_COMPONENT(Rigidbody2D)
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

void Rigidbody2D::Serialize(nlohmann::json& j) const {
    j["bodyType"] = static_cast<int>(bodyType);
    j["gravityScale"] = gravityScale;
    j["mass"] = mass;
    j["linearDamping"] = linearDamping;
    j["freezeRotation"] = freezeRotation;
    j["velocity"] = { velocity.x, velocity.y };
}

void Rigidbody2D::Deserialize(const nlohmann::json& j) {
    if (j.contains("bodyType")) {
        bodyType = static_cast<BodyType>(j["bodyType"].get<int>());
    }
    if (j.contains("gravityScale")) {
        gravityScale = j["gravityScale"];
    }
    if (j.contains("mass")) {
        mass = j["mass"];
    }
    if (j.contains("linearDamping")) {
        linearDamping = j["linearDamping"];
    }
    if (j.contains("freezeRotation")) {
        freezeRotation = j["freezeRotation"];
    }
    if (j.contains("velocity") && j["velocity"].is_array()) {
        velocity = Vector2(j["velocity"][0], j["velocity"][1]);
    }
}

void Rigidbody2D::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    const char* bodyTypes[] = { "Static", "Kinematic", "Dynamic" };
    int currentType = static_cast<int>(bodyType);
    if (ImGui::Combo("Body Type", &currentType, bodyTypes, IM_ARRAYSIZE(bodyTypes))) {
        SetBodyType(static_cast<BodyType>(currentType));
    }

    if (bodyType == BodyType::Dynamic) {
        float gScale = gravityScale;
        if (ImGui::DragFloat("Gravity Scale", &gScale, 0.1f)) {
            SetGravityScale(gScale);
        }

        float m = mass;
        if (ImGui::DragFloat("Mass", &m, 0.1f, 0.001f, 10000.0f)) {
            SetMass(m);
        }

        float damping = linearDamping;
        if (ImGui::DragFloat("Linear Damping", &damping, 0.05f, 0.0f, 100.0f)) {
            SetLinearDamping(damping);
        }
    }

    bool freeze = freezeRotation;
    if (ImGui::Checkbox("Freeze Rotation", &freeze)) {
        SetFreezeRotation(freeze);
    }

    float vel[2] = { velocity.x, velocity.y };
    if (ImGui::DragFloat2("Velocity", vel, 0.5f)) {
        SetVelocity(Vector2(vel[0], vel[1]));
    }
#endif
}
