#include "Core/World.h"
#include "Core/SceneSerializer.h"
#include "Core/Scheduler.h"
#include "ECS/GameObject.h"
#include "Physics/PhysicsWorld.h"

World::World()
    : physicsWorld(std::make_unique<PhysicsWorld>()),
      scheduler(std::make_unique<Scheduler>()) {
}

World::~World() = default;

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

GameObject* World::Add(std::shared_ptr<GameObject> obj) {
    if (!obj) return nullptr;
    obj->SetWorld(this);
    GameObject* raw = obj.get();
    objects_.push_back(std::move(obj));
    return raw;
}

GameObject* World::FindById(unsigned int id) const {
    for (const auto& o : objects_) {
        if (o && o->GetID() == id) return o.get();
    }
    return nullptr;
}

GameObject* World::FindWithTag(const std::string& tag) const {
    for (const auto& o : objects_) {
        if (o && o->IsActive() && o->CompareTag(tag)) return o.get();
    }
    return nullptr;
}

GameObject* World::Find(const std::string& name) const {
    for (const auto& o : objects_) {
        if (o && o->IsActive() && o->GetName() == name) return o.get();
    }
    return nullptr;
}

std::vector<GameObject*> World::FindAllWithTag(const std::string& tag) const {
    std::vector<GameObject*> result;
    for (const auto& o : objects_) {
        if (o && o->IsActive() && o->CompareTag(tag)) {
            result.push_back(o.get());
        }
    }
    return result;
}

void World::Clear() { objects_.clear(); }

void World::StartPending() {
    // Unity 순서: 모든 Awake → 모든 OnEnable → 모든 Start.
    for (auto& o : objects_) if (o) o->AwakeScripts();
    for (auto& o : objects_) if (o) o->EnableScripts();
    for (auto& o : objects_) if (o) o->StartScripts();
    running_ = true;  // 이후 SetActive가 라이프사이클 콜백을 발화
}
void World::FixedStep(float fixedDt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->FixedUpdateScripts(fixedDt);
    physicsWorld->Step(*this, fixedDt);
}
void World::Update(float dt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->Update(dt);
    scheduler->Tick(dt);  // Invoke/InvokeRepeating/코루틴 구동
}
void World::LateUpdate(float dt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->LateUpdateScripts(dt);
}

void World::ResolveAssets() {
    for (auto& o : objects_) if (o) o->ResolveAssets();
}

std::unique_ptr<World> World::Clone() const {
    auto copy = std::make_unique<World>();
    nlohmann::json doc = SceneSerializer::SerializeScene(objects_, name_);
    SceneSerializer::DeserializeScene(doc, copy->objects_);
    for (auto& o : copy->objects_) {
        if (o) o->SetWorld(copy.get());
    }
    copy->name_ = name_;
    return copy;
}

#include "ECS/Components/Transform.h"

bool World::LoadFromFile(const std::string& path) {
    bool success = SceneSerializer::LoadScene(path, objects_);
    if (success) {
        for (auto& o : objects_) {
            if (o) o->SetWorld(this);
        }
    }
    return success;
}
bool World::SaveToFile(const std::string& path) const {
    return SceneSerializer::SaveScene(path, objects_);
}

GameObject* World::Instantiate(const GameObject* original) {
    if (!original) return nullptr;
    nlohmann::json subtree = SceneSerializer::SerializeSubtree(original);
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* root = SceneSerializer::DeserializeSubtreeRemapped(subtree, pendingAdds_, idRemap);
    return root;
}

GameObject* World::Instantiate(const GameObject* original, const Vector2& worldPos) {
    GameObject* root = Instantiate(original);
    if (root) {
        if (auto* transform = root->GetComponent<Transform>()) {
            transform->SetPosition(worldPos);
        }
    }
    return root;
}

#include "Core/PrefabRegistry.h"

GameObject* World::Instantiate(const GameObject* original, GameObject* parent) {
    GameObject* root = Instantiate(original);
    if (root && parent) {
        root->SetParent(parent);
    }
    return root;
}

GameObject* World::InstantiatePrefab(const std::string& guid) {
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* root = PrefabRegistry::Get().Instantiate(guid, pendingAdds_, idRemap);
    return root;
}

void World::Destroy(GameObject* obj, float delay) {
    if (!obj) return;
    unsigned int id = obj->GetID();
    for (const auto& pd : pendingDestroys_) {
        if (pd.id == id) return;
    }
    pendingDestroys_.push_back({ id, delay });
}

void World::FlushDeferred(float dt) {
    // 1. Process pending destroys
    std::vector<unsigned int> idsToDestroyNow;
    auto it = pendingDestroys_.begin();
    while (it != pendingDestroys_.end()) {
        it->delay -= dt;
        if (it->delay <= 0.0f) {
            idsToDestroyNow.push_back(it->id);
            it = pendingDestroys_.erase(it);
        } else {
            ++it;
        }
    }

    if (!idsToDestroyNow.empty()) {
        std::vector<GameObject*> allSubtreeObjects;
        for (unsigned int id : idsToDestroyNow) {
            GameObject* found = nullptr;
            for (const auto& o : objects_) {
                if (o && o->GetID() == id) {
                    found = o.get();
                    break;
                }
            }
            if (!found) {
                for (const auto& o : pendingAdds_) {
                    if (o && o->GetID() == id) {
                        found = o.get();
                        break;
                    }
                }
            }
            if (found) {
                found->CollectSubtree(allSubtreeObjects);
            }
        }

        for (auto* obj : allSubtreeObjects) {
            if (obj) {
                scheduler->CancelByGameObject(obj->GetID());  // 대기 콜백 정리
                obj->NotifyDestroy();
            }
        }

        std::vector<unsigned int> subtreeIds;
        subtreeIds.reserve(allSubtreeObjects.size());
        for (auto* obj : allSubtreeObjects) {
            if (obj) {
                subtreeIds.push_back(obj->GetID());
            }
        }

        auto removePredicate = [&](const std::shared_ptr<GameObject>& o) {
            if (!o) return true;
            unsigned int oid = o->GetID();
            return std::find(subtreeIds.begin(), subtreeIds.end(), oid) != subtreeIds.end();
        };

        objects_.erase(std::remove_if(objects_.begin(), objects_.end(), removePredicate), objects_.end());
        pendingAdds_.erase(std::remove_if(pendingAdds_.begin(), pendingAdds_.end(), removePredicate), pendingAdds_.end());
    }

    // 2. Process pending adds
    if (!pendingAdds_.empty()) {
        std::vector<std::shared_ptr<GameObject>> newAdds = std::move(pendingAdds_);
        pendingAdds_.clear();

        for (auto& obj : newAdds) {
            if (obj) {
                obj->SetWorld(this);
                objects_.push_back(obj);
            }
        }

        // 새 오브젝트: 에셋 로드 → 모든 Awake → 모든 OnEnable → 모든 Start
        for (auto& obj : newAdds) {
            if (obj) obj->ResolveAssets();
        }
        for (auto& obj : newAdds) {
            if (obj) obj->AwakeScripts();
        }
        for (auto& obj : newAdds) {
            if (obj) obj->EnableScripts();
        }
        for (auto& obj : newAdds) {
            if (obj) obj->StartScripts();
        }
    }
}

