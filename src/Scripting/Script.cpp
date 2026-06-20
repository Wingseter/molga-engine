#include "Script.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#include "Core/World.h"
#include "Core/Scheduler.h"
#include "../Physics/Physics2D.h"
#include <nlohmann/json.hpp>

const ScriptFieldRegistry& Script::Fields() {
    if (!fieldsBuilt_) {
        RegisterFields(fieldRegistry_);
        fieldsBuilt_ = true;
    }
    return fieldRegistry_;
}

void Script::Serialize(nlohmann::json& j) const {
    // const 메서드지만 레지스트리는 지연 구성이 필요하므로 1회 빌드.
    const ScriptFieldRegistry& reg = const_cast<Script*>(this)->Fields();
    if (reg.Empty()) return;

    // 사용자 필드명이 임의의 문자열이므로 컴포넌트 envelope("type","enabled")와
    // 충돌하지 않도록 "fields" 하위 객체에 격리한다.
    nlohmann::json fields = nlohmann::json::object();
    for (const auto& f : reg.Fields()) {
        switch (f.type) {
            case ScriptFieldType::Float:  fields[f.name] = *static_cast<const float*>(f.ptr); break;
            case ScriptFieldType::Int:    fields[f.name] = *static_cast<const int*>(f.ptr); break;
            case ScriptFieldType::Bool:   fields[f.name] = *static_cast<const bool*>(f.ptr); break;
            case ScriptFieldType::String: fields[f.name] = *static_cast<const std::string*>(f.ptr); break;
            case ScriptFieldType::Vector2: {
                const auto* v = static_cast<const Vector2*>(f.ptr);
                fields[f.name] = { v->x, v->y };
                break;
            }
            case ScriptFieldType::Color: {
                const auto* c = static_cast<const Color*>(f.ptr);
                fields[f.name] = { c->r, c->g, c->b, c->a };
                break;
            }
            case ScriptFieldType::ObjectRef:
                fields[f.name] = static_cast<const ObjectRef*>(f.ptr)->targetId;
                break;
            case ScriptFieldType::PrefabRef:
                fields[f.name] = static_cast<const PrefabRef*>(f.ptr)->guid;
                break;
        }
    }
    j["fields"] = std::move(fields);
}

void Script::Deserialize(const nlohmann::json& j) {
    if (!j.contains("fields")) return;
    const auto& fields = j.at("fields");
    const ScriptFieldRegistry& reg = Fields();
    for (const auto& f : reg.Fields()) {
        if (!fields.contains(f.name)) continue;
        const auto& val = fields.at(f.name);
        try {
            switch (f.type) {
                case ScriptFieldType::Float:  *static_cast<float*>(f.ptr) = val.get<float>(); break;
                case ScriptFieldType::Int:    *static_cast<int*>(f.ptr) = val.get<int>(); break;
                case ScriptFieldType::Bool:   *static_cast<bool*>(f.ptr) = val.get<bool>(); break;
                case ScriptFieldType::String: *static_cast<std::string*>(f.ptr) = val.get<std::string>(); break;
                case ScriptFieldType::Vector2: {
                    if (val.is_array() && val.size() >= 2) {
                        auto* v = static_cast<Vector2*>(f.ptr);
                        v->x = val[0].get<float>();
                        v->y = val[1].get<float>();
                    }
                    break;
                }
                case ScriptFieldType::Color: {
                    if (val.is_array() && val.size() >= 4) {
                        auto* c = static_cast<Color*>(f.ptr);
                        c->r = val[0].get<float>();
                        c->g = val[1].get<float>();
                        c->b = val[2].get<float>();
                        c->a = val[3].get<float>();
                    }
                    break;
                }
                case ScriptFieldType::ObjectRef:
                    static_cast<ObjectRef*>(f.ptr)->targetId = val.get<unsigned int>();
                    break;
                case ScriptFieldType::PrefabRef:
                    static_cast<PrefabRef*>(f.ptr)->guid = val.get<std::string>();
                    break;
            }
        } catch (...) {
            // 타입 불일치 등은 무시하고 기본값 유지.
        }
    }
}

void Script::RemapReferences(const std::unordered_map<unsigned int, unsigned int>& idRemap) {
    for (const auto& f : Fields().Fields()) {
        if (f.type != ScriptFieldType::ObjectRef) continue;
        auto* ref = static_cast<ObjectRef*>(f.ptr);
        if (ref->targetId == 0) continue;
        auto it = idRemap.find(ref->targetId);
        if (it != idRemap.end()) {
            ref->targetId = it->second;  // 같은 서브트리 내부 참조 -> 새 id
        }
        // 맵에 없으면 외부(씬) 참조이므로 원본 id 유지.
    }
}

GameObject* Script::Resolve(const ObjectRef& ref) const {
    if (ref.targetId == 0) return nullptr;
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->FindById(ref.targetId);
    }
    return nullptr;
}

GameObject* Script::Instantiate(const PrefabRef& ref) {
    if (ref.guid.empty()) return nullptr;
    return InstantiatePrefab(ref.guid);
}

World* Script::GetWorld() const {
    return gameObject ? gameObject->GetWorld() : nullptr;
}

GameObject* Script::Find(const std::string& name) const {
    World* w = GetWorld();
    return w ? w->Find(name) : nullptr;
}

GameObject* Script::FindWithTag(const std::string& tag) const {
    World* w = GetWorld();
    return w ? w->FindWithTag(tag) : nullptr;
}

std::vector<GameObject*> Script::FindAllWithTag(const std::string& tag) const {
    World* w = GetWorld();
    return w ? w->FindAllWithTag(tag) : std::vector<GameObject*>{};
}

RaycastHit2D Script::Raycast(const Vector2& origin, const Vector2& direction,
                             float maxDistance, int layerMask) const {
    World* w = GetWorld();
    return w ? Physics2D::Raycast(*w, origin, direction, maxDistance, layerMask) : RaycastHit2D{};
}

std::vector<GameObject*> Script::OverlapCircleAll(const Vector2& center, float radius,
                                                  int layerMask) const {
    World* w = GetWorld();
    return w ? Physics2D::OverlapCircleAll(*w, center, radius, layerMask) : std::vector<GameObject*>{};
}

std::vector<GameObject*> Script::OverlapBoxAll(const Vector2& center, const Vector2& halfExtents,
                                               int layerMask) const {
    World* w = GetWorld();
    return w ? Physics2D::OverlapBoxAll(*w, center, halfExtents, layerMask) : std::vector<GameObject*>{};
}

GameObject* Script::OverlapPoint(const Vector2& point, int layerMask) const {
    World* w = GetWorld();
    return w ? Physics2D::OverlapPoint(*w, point, layerMask) : nullptr;
}

// ── 타이머 / 코루틴 ──
// owner는 이 스크립트 인스턴스(this), goId는 소유 GameObject id로 태깅한다.
void Script::Invoke(std::function<void()> fn, float delay) {
    World* w = GetWorld();
    if (w && w->GetScheduler() && gameObject) {
        w->GetScheduler()->Invoke(this, gameObject->GetID(), std::move(fn), delay);
    }
}

void Script::InvokeRepeating(std::function<void()> fn, float delay, float interval) {
    World* w = GetWorld();
    if (w && w->GetScheduler() && gameObject) {
        w->GetScheduler()->InvokeRepeating(this, gameObject->GetID(), std::move(fn), delay, interval);
    }
}

void Script::CancelInvoke() {
    World* w = GetWorld();
    if (w && w->GetScheduler()) w->GetScheduler()->CancelInvoke(this);
}

bool Script::IsInvoking() const {
    World* w = GetWorld();
    return w && w->GetScheduler() && w->GetScheduler()->IsInvoking(this);
}

void Script::StartCoroutine(std::function<bool(float)> step) {
    World* w = GetWorld();
    if (w && w->GetScheduler() && gameObject) {
        w->GetScheduler()->StartCoroutine(this, gameObject->GetID(), std::move(step));
    }
}

void Script::StopAllCoroutines() {
    World* w = GetWorld();
    if (w && w->GetScheduler()) w->GetScheduler()->StopCoroutines(this);
}

Transform* Script::GetTransform() {
    if (gameObject) {
        return gameObject->GetComponent<Transform>();
    }
    return nullptr;
}

GameObject* Script::Instantiate(const GameObject* original) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->Instantiate(original);
    }
    return nullptr;
}

GameObject* Script::Instantiate(const GameObject* original, const Vector2& position) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->Instantiate(original, position);
    }
    return nullptr;
}

GameObject* Script::Instantiate(const GameObject* original, GameObject* parent) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->Instantiate(original, parent);
    }
    return nullptr;
}

GameObject* Script::InstantiatePrefab(const std::string& guid) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->InstantiatePrefab(guid);
    }
    return nullptr;
}

void Script::Destroy(GameObject* obj, float delay) {
    if (gameObject && gameObject->GetWorld()) {
        gameObject->GetWorld()->Destroy(obj, delay);
    }
}

void Script::Destroy(float delay) {
    if (gameObject && gameObject->GetWorld()) {
        gameObject->GetWorld()->Destroy(gameObject, delay);
    }
}

// 인스펙터 렌더링은 에디터(molga_engine, MOLGA_EDITOR + imgui)에서 수행한다.
// molga_core에는 MOLGA_EDITOR가 정의되지 않으므로 여기서 imgui로 그리면
// 빌트인/유저 스크립트 모두에서 표시되지 않는다. 대신 에디터의
// InspectorWindow가 public Fields()를 읽어 위젯을 그린다.
// (커스텀 UI가 필요한 스크립트는 OnInspectorGUI를 직접 오버라이드할 수 있다.)
void Script::OnInspectorGUI() {
}
