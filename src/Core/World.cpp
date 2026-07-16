#include "Core/World.h"
#include "Core/SceneSerializer.h"
#include "Core/Scheduler.h"
#include "ECS/GameObject.h"
#include "Physics/PhysicsWorld.h"
#include "Core/Profiling/ProfileScope.h"
#include "Core/Profiling/ProfilerService.h"
#include "Common/Log.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <unordered_set>

namespace {

class CallbackDispatchGuard {
public:
    explicit CallbackDispatchGuard(unsigned int& depth) : depth_(depth) { ++depth_; }
    ~CallbackDispatchGuard() { --depth_; }

    CallbackDispatchGuard(const CallbackDispatchGuard&) = delete;
    CallbackDispatchGuard& operator=(const CallbackDispatchGuard&) = delete;

private:
    unsigned int& depth_;
};

} // namespace

World::World()
    : physicsWorld(std::make_unique<PhysicsWorld>()),
      scheduler(std::make_unique<Scheduler>()) {
}

World::~World() {
    Shutdown();
}

World::World(World&& other) noexcept
    : World() {
    *this = std::move(other);
}

World& World::operator=(World&& other) noexcept {
    if (this == &other) return *this;
    // Dispatch and flush guards keep references to these fields. Moving either
    // World while a guard is live would reset state underneath it and can make
    // the outer callback continue on unrelated containers.
    if (IsLifecycleMutationActive() || other.IsLifecycleMutationActive()) {
        return *this;
    }
    Shutdown();
    objects_ = std::move(other.objects_);
    name_ = std::move(other.name_);
    physicsWorld = std::move(other.physicsWorld);
    scheduler = std::move(other.scheduler);
    running_ = other.running_;
    sceneRuntime_ = other.sceneRuntime_;
    pendingAdds_ = std::move(other.pendingAdds_);
    pendingDestroys_ = std::move(other.pendingDestroys_);
    flushingDeferred_ = false;
    flushDeferredRequested_ = false;
    shuttingDown_ = false;
    callbackDispatchDepth_ = 0;
    for (auto& object : objects_) if (object) object->SetWorld(this);
    for (auto& object : pendingAdds_) if (object) object->SetWorld(this);
    other.running_ = false;
    other.sceneRuntime_ = nullptr;
    other.flushingDeferred_ = false;
    other.flushDeferredRequested_ = false;
    other.shuttingDown_ = false;
    other.callbackDispatchDepth_ = 0;
    return *this;
}

GameObject* World::Add(std::shared_ptr<GameObject> obj) {
    if (!obj || shuttingDown_) return nullptr;
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

bool World::OwnsObject(const std::shared_ptr<GameObject>& object) const {
    if (!object || object->GetWorld() != this) return false;
    return std::any_of(
        objects_.begin(), objects_.end(),
        [&](const std::shared_ptr<GameObject>& candidate) {
            return candidate && candidate.get() == object.get() &&
                   candidate->GetID() == object->GetID();
        });
}

bool World::IsLifecycleMutationActive() const {
    return callbackDispatchDepth_ != 0 || flushingDeferred_ || shuttingDown_;
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

void World::Clear() { Shutdown(); }

void World::Shutdown() noexcept {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);

    auto resetPhysics = [&]() noexcept {
        try {
            if (physicsWorld) physicsWorld->Reset();
        } catch (const std::exception& error) {
            Log::Error("World", "Physics shutdown failed: " + std::string(error.what()));
        } catch (...) {
            Log::Error("World", "Physics shutdown failed.");
        }
    };

    // Clear once before callbacks so no stale closure can run while teardown is
    // in progress. OnDestroy is allowed to schedule cleanup work, so both the
    // scheduler and physics backend are cleared again after every callback has
    // completed.
    if (scheduler) scheduler->Clear();
    resetPhysics();

    // Detach the runtime first so unload callbacks cannot enqueue a new scene
    // request from a World that is already leaving.
    sceneRuntime_ = nullptr;
    running_ = false;

    // Swap ownership into a stable batch before callbacks. This lets an
    // OnDestroy callback add another object without mutating the container we
    // are traversing; newly added objects are drained by the next iteration.
    while (!objects_.empty() || !pendingAdds_.empty()) {
        std::vector<std::shared_ptr<GameObject>> batch;
        batch.swap(objects_);
        batch.reserve(batch.size() + pendingAdds_.size());
        for (auto& object : pendingAdds_) {
            batch.push_back(std::move(object));
        }
        pendingAdds_.clear();

        for (const auto& object : batch) {
            if (!object) continue;
            if (scheduler) scheduler->CancelByGameObject(object->GetID());
            object->NotifyDestroy();
            object->SetWorld(nullptr);
        }
    }

    pendingDestroys_.clear();
    if (scheduler) scheduler->Clear();
    resetPhysics();
    shuttingDown_ = false;
}

void World::StartPending() {
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);
    // Unity 순서: 모든 Awake → 모든 OnEnable → 모든 Start.
    std::exception_ptr firstError;
    const auto phaseObjects = objects_;
    auto runPhase = [&](auto callback) {
        // Hold shared ownership so callbacks can remove objects without
        // invalidating the phase traversal.
        for (const auto& object : phaseObjects) {
            if (!OwnsObject(object)) continue;
            try {
                callback(*object);
            } catch (...) {
                if (!firstError) firstError = std::current_exception();
            }
        }
    };
    runPhase([](GameObject& object) { object.AwakeScripts(); });
    runPhase([](GameObject& object) { object.EnableScripts(); });
    runPhase([](GameObject& object) { object.StartScripts(); });
    running_ = true;  // 이후 SetActive가 라이프사이클 콜백을 발화
    if (firstError) std::rethrow_exception(firstError);
}
void World::FixedStep(float fixedDt) {
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);
    MOLGA_PROFILE_SCOPE("World.FixedStep", molga::ProfileCategory::Physics);
    const auto phaseObjects = objects_;
    for (const auto& object : phaseObjects) {
        if (OwnsObject(object) && object->IsActive()) {
            object->FixedUpdateScripts(fixedDt);
        }
    }
    physicsWorld->Step(*this, fixedDt);
}
void World::Update(float dt) {
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);
    MOLGA_PROFILE_SCOPE("World.Update", molga::ProfileCategory::Scripts);
    const auto phaseObjects = objects_;
    for (const auto& object : phaseObjects) {
        if (OwnsObject(object) && object->IsActive()) object->Update(dt);
    }
    scheduler->Tick(dt);  // Invoke/InvokeRepeating/코루틴 구동
}
void World::LateUpdate(float dt) {
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);
    MOLGA_PROFILE_SCOPE("World.LateUpdate", molga::ProfileCategory::Scripts);
    const auto phaseObjects = objects_;
    for (const auto& object : phaseObjects) {
        if (OwnsObject(object) && object->IsActive()) object->LateUpdateScripts(dt);
    }
}

void World::ResolveAssets() {
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);
    const auto phaseObjects = objects_;
    for (const auto& object : phaseObjects) {
        if (OwnsObject(object)) object->ResolveAssets();
    }
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
    if (!original || shuttingDown_) return nullptr;
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
    if (shuttingDown_) return nullptr;
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
    // A lifecycle callback can request another flush. It cannot recursively
    // mutate the containers being traversed; the outer call drains that request
    // once its current phase reaches a safe boundary.
    if (flushingDeferred_) {
        flushDeferredRequested_ = true;
        return;
    }
    flushingDeferred_ = true;
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);
    struct FlushFlagReset {
        bool& active;
        bool& requested;
        ~FlushFlagReset() {
            active = false;
            requested = false;
        }
    } flagReset{flushingDeferred_, flushDeferredRequested_};

    float destroyDt = dt;
    do {
        flushDeferredRequested_ = false;

        // 1. Move due destroy requests into an ID-only plan. Hierarchy raw
        // pointers are observed only while constructing the plan; every user
        // callback re-resolves a shared owner by ID before it runs.
        std::vector<unsigned int> idsToDestroyNow;
        auto destroyIt = pendingDestroys_.begin();
        while (destroyIt != pendingDestroys_.end()) {
            destroyIt->delay -= destroyDt;
            if (destroyIt->delay <= 0.0f) {
                idsToDestroyNow.push_back(destroyIt->id);
                destroyIt = pendingDestroys_.erase(destroyIt);
            } else {
                ++destroyIt;
            }
        }
        // A reentrant flush is part of this same frame and must not decrement
        // delayed destroys a second time.
        destroyDt = 0.0f;

        auto findOwned = [&](unsigned int id) -> std::shared_ptr<GameObject> {
            for (const auto& object : objects_) {
                if (object && object->GetID() == id) return object;
            }
            for (const auto& object : pendingAdds_) {
                if (object && object->GetID() == id) return object;
            }
            return {};
        };

        std::vector<unsigned int> subtreeIds;
        std::unordered_set<unsigned int> subtreeIdSet;
        std::function<void(const GameObject*)> collectSubtreeIds =
            [&](const GameObject* object) {
                if (!object || !subtreeIdSet.insert(object->GetID()).second) return;
                subtreeIds.push_back(object->GetID());
                for (const GameObject* child : object->GetChildren()) {
                    collectSubtreeIds(child);
                }
            };
        for (unsigned int id : idsToDestroyNow) {
            if (const auto object = findOwned(id)) collectSubtreeIds(object.get());
        }

        for (unsigned int id : subtreeIds) {
            const auto object = findOwned(id);
            if (!object) continue;
            if (scheduler) scheduler->CancelByGameObject(id);
            object->NotifyDestroy();
            object->SetWorld(nullptr);
        }

        // A removed object can outlive its World ownership through an external
        // shared_ptr. Detach every destroyed/surviving hierarchy boundary now,
        // rather than leaving a live parent pointing at an out-of-World child.
        for (unsigned int id : subtreeIds) {
            const auto object = findOwned(id);
            if (!object) continue;

            GameObject* parent = object->GetParent();
            if (parent && subtreeIdSet.count(parent->GetID()) == 0) {
                object->SetParent(nullptr);
            }

            const auto children = object->GetChildren();
            for (GameObject* child : children) {
                if (child && subtreeIdSet.count(child->GetID()) == 0) {
                    child->SetParent(nullptr);
                }
            }
        }

        if (!subtreeIdSet.empty()) {
            auto removePredicate = [&](const std::shared_ptr<GameObject>& object) {
                return !object || subtreeIdSet.count(object->GetID()) != 0;
            };
            objects_.erase(
                std::remove_if(objects_.begin(), objects_.end(), removePredicate),
                objects_.end());
            pendingAdds_.erase(
                std::remove_if(pendingAdds_.begin(), pendingAdds_.end(), removePredicate),
                pendingAdds_.end());
            pendingDestroys_.erase(
                std::remove_if(
                    pendingDestroys_.begin(), pendingDestroys_.end(),
                    [&](const PendingDestroy& pending) {
                        return subtreeIdSet.count(pending.id) != 0;
                    }),
                pendingDestroys_.end());
        }

        // 2. Process pending adds. Re-resolve membership before every phase:
        // an earlier callback may synchronously Destroy+Flush a later object.
        if (!pendingAdds_.empty()) {
            std::vector<std::shared_ptr<GameObject>> newAdds = std::move(pendingAdds_);
            pendingAdds_.clear();

            for (const auto& object : newAdds) {
                if (!object) continue;
                object->SetWorld(this);
                objects_.push_back(object);
            }

            auto isCurrentAndAlive = [&](const std::shared_ptr<GameObject>& object) {
                if (!object || object->GetWorld() != this) return false;
                const auto owned = std::find_if(
                    objects_.begin(), objects_.end(),
                    [&](const std::shared_ptr<GameObject>& candidate) {
                        return candidate && candidate.get() == object.get() &&
                               candidate->GetID() == object->GetID();
                    });
                if (owned == objects_.end()) return false;
                return std::none_of(
                    pendingDestroys_.begin(), pendingDestroys_.end(),
                    [&](const PendingDestroy& pending) {
                        return pending.id == object->GetID() && pending.delay <= 0.0f;
                    });
            };
            auto runPhase = [&](auto callback) {
                for (const auto& object : newAdds) {
                    if (isCurrentAndAlive(object)) callback(*object);
                }
            };

            runPhase([](GameObject& object) { object.ResolveAssets(); });
            runPhase([](GameObject& object) { object.AwakeScripts(); });
            runPhase([](GameObject& object) { object.EnableScripts(); });
            runPhase([](GameObject& object) { object.StartScripts(); });
        }
    } while (flushDeferredRequested_);
}
