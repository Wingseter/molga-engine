#include "PhysicsWorld.h"
#include "../Core/World.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/Rigidbody2D.h"
#include "../ECS/Components/Collider2D.h"
#include "../ECS/Components/BoxCollider2D.h"
#include "../ECS/Components/CircleCollider2D.h"
#include "../ECS/Components/TilemapRenderer.h"
#include "../Scripting/Script.h"
#include "../Core/EventBus.h"
#include "../Core/Events/PhysicsEvents.h"
#include "../Core/ProjectSettings.h"
#include <vector>
#include <algorithm>
#include <cmath>

static PhysicsWorld::ContactKey MakeContactKey(unsigned int idA, unsigned int idB, bool isTrigger) {
    if (idA > idB) {
        std::swap(idA, idB);
    }
    return { idA, idB, isTrigger };
}

static CollisionResult DetectCollision(Collider2D* colA, Collider2D* colB) {
    Collider2D::ShapeType typeA = colA->GetShapeType();
    Collider2D::ShapeType typeB = colB->GetShapeType();

    if (typeA == Collider2D::ShapeType::Box && typeB == Collider2D::ShapeType::Box) {
        auto* boxA = static_cast<BoxCollider2D*>(colA);
        auto* boxB = static_cast<BoxCollider2D*>(colB);
        return Collision::CheckAABBWithResult(boxA->GetWorldBounds(), boxB->GetWorldBounds());
    }
    else if (typeA == Collider2D::ShapeType::Circle && typeB == Collider2D::ShapeType::Circle) {
        auto* circleA = static_cast<CircleCollider2D*>(colA);
        auto* circleB = static_cast<CircleCollider2D*>(colB);
        return Collision::CheckCircleWithResult(circleA->GetWorldCircle(), circleB->GetWorldCircle());
    }
    else if (typeA == Collider2D::ShapeType::Box && typeB == Collider2D::ShapeType::Circle) {
        auto* boxA = static_cast<BoxCollider2D*>(colA);
        auto* circleB = static_cast<CircleCollider2D*>(colB);
        return Collision::CheckAABBCircleWithResult(boxA->GetWorldBounds(), circleB->GetWorldCircle());
    }
    else if (typeA == Collider2D::ShapeType::Circle && typeB == Collider2D::ShapeType::Box) {
        auto* circleA = static_cast<CircleCollider2D*>(colA);
        auto* boxB = static_cast<BoxCollider2D*>(colB);
        CollisionResult res = Collision::CheckAABBCircleWithResult(boxB->GetWorldBounds(), circleA->GetWorldCircle());
        if (res.collided) {
            res.normalX = -res.normalX;
            res.normalY = -res.normalY;
            res.overlapX = -res.overlapX;
            res.overlapY = -res.overlapY;
        }
        return res;
    }
    return CollisionResult();
}

PhysicsWorld::PhysicsWorld() {
}

PhysicsWorld::~PhysicsWorld() {
}

void PhysicsWorld::Step(World& world, float fixedDt) {
    // ── Step 1: Gravity & Integration ──
    for (auto& obj : world.Objects()) {
        if (obj && obj->IsActive()) {
            if (auto* rb = obj->GetComponent<Rigidbody2D>()) {
                if (rb->GetBodyType() == Rigidbody2D::BodyType::Dynamic) {
                    if (auto* trans = obj->GetComponent<Transform>()) {
                        float mass = rb->GetMass();
                        float gravityScale = rb->GetGravityScale();
                        float damping = rb->GetLinearDamping();

                        // gravity: 9.81m/s^2 * 100.0f pixels/s^2 = 981.0f pixels/s^2
                        Vector2 gravityForce(0.0f, 981.0f * gravityScale * mass);
                        Vector2 totalForce = rb->GetForceAccumulator() + gravityForce;

                        Vector2 velocity = rb->GetVelocity();
                        velocity += rb->GetImpulseAccumulator() / mass;
                        velocity += (totalForce / mass) * fixedDt;
                        velocity *= (1.0f - damping * fixedDt);

                        rb->SetVelocity(velocity);

                        Vector2 position = trans->GetPosition();
                        position += velocity * fixedDt;
                        trans->SetPosition(position);
                    }
                }
                rb->ClearForces();
            }
        }
    }

    // ── Step 2: Collision Detection ──
    struct ColliderInfo {
        GameObject* gameObject;
        Collider2D* collider;
    };

    std::vector<ColliderInfo> colliders;
    for (auto& obj : world.Objects()) {
        if (obj && obj->IsActive()) {
            for (auto* comp : obj->GetComponents()) {
                if (comp->IsEnabled()) {
                    if (auto* col = dynamic_cast<Collider2D*>(comp)) {
                        colliders.push_back({ obj.get(), col });
                    }
                }
            }
        }
    }

    std::set<ContactKey> currentContacts;
    struct ContactDetails {
        Vector2 normal;
        float penetration;
        Vector2 contactPoint;
    };
    std::map<ContactKey, ContactDetails> contactDetailsMap;

    // ── Tilemap Collisions ──
    std::vector<TilemapRenderer*> tilemaps;
    for (auto& obj : world.Objects()) {
        if (obj && obj->IsActive()) {
            for (auto* comp : obj->GetComponents()) {
                if (comp->IsEnabled()) {
                    if (auto* tm = dynamic_cast<TilemapRenderer*>(comp)) {
                        tilemaps.push_back(tm);
                    }
                }
            }
        }
    }

    for (auto& obj : world.Objects()) {
        if (!obj || !obj->IsActive()) continue;

        auto* rb = obj->GetComponent<Rigidbody2D>();
        if (!rb || rb->GetBodyType() != Rigidbody2D::BodyType::Dynamic) continue;

        for (auto* comp : obj->GetComponents()) {
            if (!comp->IsEnabled()) continue;

            auto* col = dynamic_cast<Collider2D*>(comp);
            if (!col) continue;

            if (col->GetShapeType() != Collider2D::ShapeType::Box &&
                col->GetShapeType() != Collider2D::ShapeType::Circle) {
                continue;
            }

            AABB colliderBounds = col->GetWorldBounds();

            for (auto* tm : tilemaps) {
                if (!ProjectSettings::Get().IsCollisionEnabled(obj->GetLayer(), tm->GetGameObject()->GetLayer())) {
                    continue;
                }

                std::vector<AABB> collidingTiles = tm->GetCollidingTiles(colliderBounds);
                for (const auto& tileBox : collidingTiles) {
                    CollisionResult res;
                    if (col->GetShapeType() == Collider2D::ShapeType::Box) {
                        res = Collision::CheckAABBWithResult(col->GetWorldBounds(), tileBox);
                    } else { // Circle
                        auto* circleCol = static_cast<CircleCollider2D*>(col);
                        res = Collision::CheckAABBCircleWithResult(tileBox, circleCol->GetWorldCircle());
                        if (res.collided) {
                            res.normalX = -res.normalX;
                            res.normalY = -res.normalY;
                            res.overlapX = -res.overlapX;
                            res.overlapY = -res.overlapY;
                        }
                    }

                    if (res.collided) {
                        bool isTrigger = col->IsTrigger();
                        ContactKey key = MakeContactKey(obj->GetID(), tm->GetGameObject()->GetID(), isTrigger);
                        currentContacts.insert(key);

                        ContactDetails details;
                        details.normal = Vector2(res.normalX, res.normalY);
                        details.penetration = std::sqrt(res.overlapX * res.overlapX + res.overlapY * res.overlapY);
                        details.contactPoint = tileBox.Center();
                        contactDetailsMap[key] = details;

                        if (!isTrigger) {
                            // Resolve penetration
                            if (auto* trans = obj->GetComponent<Transform>()) {
                                trans->SetPosition(trans->GetPosition() - Vector2(res.overlapX, res.overlapY));
                                // Update bounds for subsequent iterations
                                colliderBounds = col->GetWorldBounds();
                            }

                            // Resolve velocity
                            Vector2 vel = rb->GetVelocity();
                            Vector2 normalVec(res.normalX, res.normalY);
                            float velAlongNormal = -vel.Dot(normalVec);

                            if (velAlongNormal < 0.0f) {
                                float invMass = rb->GetMass() > 0.0f ? 1.0f / rb->GetMass() : 1.0f;
                                float j = -velAlongNormal / invMass;
                                Vector2 impulseVec = normalVec * j;
                                rb->SetVelocity(vel - impulseVec * invMass);
                            }
                        }
                    }
                }
            }
        }
    }


    for (size_t i = 0; i < colliders.size(); ++i) {
        for (size_t j = i + 1; j < colliders.size(); ++j) {
            GameObject* goA = colliders[i].gameObject;
            GameObject* goB = colliders[j].gameObject;
            if (goA == goB) continue;

            if (!ProjectSettings::Get().IsCollisionEnabled(goA->GetLayer(), goB->GetLayer())) {
                continue;
            }

            Rigidbody2D::BodyType typeA = Rigidbody2D::BodyType::Static;
            if (auto* rbA = goA->GetComponent<Rigidbody2D>()) {
                typeA = rbA->GetBodyType();
            }
            Rigidbody2D::BodyType typeB = Rigidbody2D::BodyType::Static;
            if (auto* rbB = goB->GetComponent<Rigidbody2D>()) {
                typeB = rbB->GetBodyType();
            }

            bool isStaticOrKinematicA = (typeA == Rigidbody2D::BodyType::Static || typeA == Rigidbody2D::BodyType::Kinematic);
            bool isStaticOrKinematicB = (typeB == Rigidbody2D::BodyType::Static || typeB == Rigidbody2D::BodyType::Kinematic);
            if (isStaticOrKinematicA && isStaticOrKinematicB) {
                continue;
            }

            Collider2D* colA = colliders[i].collider;
            Collider2D* colB = colliders[j].collider;

            // ── Step 3: Narrow Phase ──
            CollisionResult res = DetectCollision(colA, colB);
            if (res.collided) {
                bool isTrigger = colA->IsTrigger() || colB->IsTrigger();
                ContactKey key = MakeContactKey(goA->GetID(), goB->GetID(), isTrigger);
                currentContacts.insert(key);

                ContactDetails details;
                details.normal = Vector2(res.normalX, res.normalY);
                details.penetration = std::sqrt(res.overlapX * res.overlapX + res.overlapY * res.overlapY);

                Vector2 posA = goA->GetComponent<Transform>() ? goA->GetComponent<Transform>()->GetWorldPosition() : Vector2::Zero();
                Vector2 posB = goB->GetComponent<Transform>() ? goB->GetComponent<Transform>()->GetWorldPosition() : Vector2::Zero();
                details.contactPoint = (posA + posB) * 0.5f;

                contactDetailsMap[key] = details;

                if (!isTrigger) {
                    // ── Step 4: Penetration Resolution ──
                    float invMassA = (typeA == Rigidbody2D::BodyType::Static || typeA == Rigidbody2D::BodyType::Kinematic) ? 0.0f : 
                                     (goA->GetComponent<Rigidbody2D>() && goA->GetComponent<Rigidbody2D>()->GetMass() > 0.0f ? 1.0f / goA->GetComponent<Rigidbody2D>()->GetMass() : 1.0f);
                    float invMassB = (typeB == Rigidbody2D::BodyType::Static || typeB == Rigidbody2D::BodyType::Kinematic) ? 0.0f : 
                                     (goB->GetComponent<Rigidbody2D>() && goB->GetComponent<Rigidbody2D>()->GetMass() > 0.0f ? 1.0f / goB->GetComponent<Rigidbody2D>()->GetMass() : 1.0f);

                    float totalInvMass = invMassA + invMassB;
                    if (totalInvMass > 0.0f) {
                        float ratioA = invMassA / totalInvMass;
                        float ratioB = invMassB / totalInvMass;

                        Vector2 overlapVec(res.overlapX, res.overlapY);

                        if (invMassA > 0.0f) {
                            if (auto* transA = goA->GetComponent<Transform>()) {
                                transA->SetPosition(transA->GetPosition() - overlapVec * ratioA);
                            }
                        }
                        if (invMassB > 0.0f) {
                            if (auto* transB = goB->GetComponent<Transform>()) {
                                transB->SetPosition(transB->GetPosition() + overlapVec * ratioB);
                            }
                        }
                    }

                    // ── Step 5: Velocity Resolution ──
                    auto* rbA = goA->GetComponent<Rigidbody2D>();
                    auto* rbB = goB->GetComponent<Rigidbody2D>();
                    Vector2 velA = rbA ? rbA->GetVelocity() : Vector2::Zero();
                    Vector2 velB = rbB ? rbB->GetVelocity() : Vector2::Zero();
                    Vector2 relVel = velB - velA;
                    Vector2 normalVec(res.normalX, res.normalY);
                    float velAlongNormal = relVel.x * normalVec.x + relVel.y * normalVec.y;

                    if (velAlongNormal < 0.0f) {
                        float j = -velAlongNormal / totalInvMass;
                        Vector2 impulseVec = normalVec * j;

                        if (invMassA > 0.0f && rbA) {
                            rbA->SetVelocity(velA - impulseVec * invMassA);
                        }
                        if (invMassB > 0.0f && rbB) {
                            rbB->SetVelocity(velB + impulseVec * invMassB);
                        }
                    }
                }
            }
        }
    }

    // ── Step 6: Events & Callbacks ──
    for (const auto& key : currentContacts) {
        GameObject* goA = world.FindById(key.idA);
        GameObject* goB = world.FindById(key.idB);
        if (!goA || !goB) continue;

        bool wasContact = (previousContacts.find(key) != previousContacts.end());
        if (!wasContact) {
            // Enter
            if (key.isTrigger) {
                for (auto* comp : goA->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnTriggerEnter(goB);
                        }
                    }
                }
                for (auto* comp : goB->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnTriggerEnter(goA);
                        }
                    }
                }

                if (auto* colA = goA->GetComponent<Collider2D>()) {
                    if (colA->IsTrigger()) {
                        TriggerEvent evt;
                        evt.trigger_ID = goA->GetID();
                        evt.other_ID = goB->GetID();
                        evt.entered = true;
                        EventBus::QueueEvent(evt);
                    }
                }
                if (auto* colB = goB->GetComponent<Collider2D>()) {
                    if (colB->IsTrigger()) {
                        TriggerEvent evt;
                        evt.trigger_ID = goB->GetID();
                        evt.other_ID = goA->GetID();
                        evt.entered = true;
                        EventBus::QueueEvent(evt);
                    }
                }
            } else {
                for (auto* comp : goA->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnCollisionEnter(goB);
                        }
                    }
                }
                for (auto* comp : goB->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnCollisionEnter(goA);
                        }
                    }
                }

                CollisionEvent evt;
                evt.objectA_ID = goA->GetID();
                evt.objectB_ID = goB->GetID();
                auto detailsIt = contactDetailsMap.find(key);
                if (detailsIt != contactDetailsMap.end()) {
                    evt.contactPoint = detailsIt->second.contactPoint;
                    evt.normal = detailsIt->second.normal;
                    evt.penetration = detailsIt->second.penetration;
                }
                EventBus::QueueEvent(evt);
            }
        } else {
            // Stay
            if (key.isTrigger) {
                for (auto* comp : goA->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnTriggerStay(goB);
                        }
                    }
                }
                for (auto* comp : goB->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnTriggerStay(goA);
                        }
                    }
                }
            } else {
                for (auto* comp : goA->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnCollisionStay(goB);
                        }
                    }
                }
                for (auto* comp : goB->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->OnCollisionStay(goA);
                        }
                    }
                }
            }
        }
    }

    // Exit
    for (const auto& key : previousContacts) {
        if (currentContacts.find(key) == currentContacts.end()) {
            GameObject* goA = world.FindById(key.idA);
            GameObject* goB = world.FindById(key.idB);

            if (key.isTrigger) {
                if (goA) {
                    for (auto* comp : goA->GetComponents()) {
                        if (comp->IsEnabled()) {
                            if (auto* script = dynamic_cast<Script*>(comp)) {
                                script->OnTriggerExit(goB);
                            }
                        }
                    }
                }
                if (goB) {
                    for (auto* comp : goB->GetComponents()) {
                        if (comp->IsEnabled()) {
                            if (auto* script = dynamic_cast<Script*>(comp)) {
                                script->OnTriggerExit(goA);
                            }
                        }
                    }
                }

                if (goA && goB) {
                    if (auto* colA = goA->GetComponent<Collider2D>()) {
                        if (colA->IsTrigger()) {
                            TriggerEvent evt;
                            evt.trigger_ID = goA->GetID();
                            evt.other_ID = goB->GetID();
                            evt.entered = false;
                            EventBus::QueueEvent(evt);
                        }
                    }
                    if (auto* colB = goB->GetComponent<Collider2D>()) {
                        if (colB->IsTrigger()) {
                            TriggerEvent evt;
                            evt.trigger_ID = goB->GetID();
                            evt.other_ID = goA->GetID();
                            evt.entered = false;
                            EventBus::QueueEvent(evt);
                        }
                    }
                }
            } else {
                if (goA) {
                    for (auto* comp : goA->GetComponents()) {
                        if (comp->IsEnabled()) {
                            if (auto* script = dynamic_cast<Script*>(comp)) {
                                script->OnCollisionExit(goB);
                            }
                        }
                    }
                }
                if (goB) {
                    for (auto* comp : goB->GetComponents()) {
                        if (comp->IsEnabled()) {
                            if (auto* script = dynamic_cast<Script*>(comp)) {
                                script->OnCollisionExit(goA);
                            }
                        }
                    }
                }
            }
        }
    }

    previousContacts = currentContacts;
}
