#include "Core/World.h"
#include "Core/SceneSerializer.h"
#include "Core/Scheduler.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Animator2D.h"
#include "Physics/PhysicsWorld.h"
#include "Core/Profiling/ProfileScope.h"
#include "Core/Profiling/ProfilerService.h"
#include "Common/Log.h"
#include "UI/UIRuntimeInvalidation.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace {

// Task 9.1: 프로세스 전역 월드 세대 시퀀스. 하나의 원자값이 이 프로세스의
// 모든 World 세대를 발급한다.
std::atomic<std::uint64_t> gNextWorldGeneration{1};

std::uint64_t AcquireWorldGeneration() {
    std::uint64_t candidate =
        gNextWorldGeneration.load(std::memory_order_relaxed);
    for (;;) {
        // 증가 전에 소진을 확인한다. fetch_add였다면 UINT64_MAX 다음이 0으로
        // 감기고, 0은 "식별자 없음"이라서 죽은 식별자가 다시 살아난다.
        if (candidate == 0 ||
            candidate == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("UI world generation exhausted");
        }
        if (gNextWorldGeneration.compare_exchange_weak(
                candidate, candidate + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return candidate;
        }
    }
}

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

std::uint64_t WorldGenerationForTesting() {
    return gNextWorldGeneration.load(std::memory_order_relaxed);
}

ScopedWorldGenerationForTesting::ScopedWorldGenerationForTesting(
    std::uint64_t next)
    : previous_(gNextWorldGeneration.exchange(next,
                                              std::memory_order_relaxed)),
      seeded_(next) {}

ScopedWorldGenerationForTesting::~ScopedWorldGenerationForTesting() {
    const std::uint64_t current =
        gNextWorldGeneration.load(std::memory_order_relaxed);
    // 훅 범위에서 아무것도 발급되지 않았다면(소진 경계값 0/UINT64_MAX가 그렇다)
    // 직전 후보를 그대로 돌려놓는다. 반대로 무언가 발급됐다면 그 세대들은 살아
    // 있을 수 있으므로 시퀀스를 뒤로 되감지 않는다 — 되감으면 프로덕션이 같은
    // 값을 한 번 더 발급해 "재사용 없음"이 훅 하나로 깨진다.
    //
    // 남는 구멍 하나: 훅을 현재 후보보다 낮게 세우고 그 안에서 발급하면 이미
    // 살아 있는 세대와 겹치는 값이 그 자리에서 나온다. 그건 복원으로 막을 수
    // 없으니 세우지 말 것.
    gNextWorldGeneration.store(
        current == seeded_ ? previous_ : std::max(previous_, current),
        std::memory_order_relaxed);
}

// 이동 생성자가 noexcept로 되돌아가면 세대 소진이 예외 대신 std::terminate가
// 된다. 두 번째 단언이 없으면 이동 생성자를 삭제해도 첫 단언이 통과한다.
static_assert(std::is_move_constructible<World>::value,
              "World must stay move constructible");
static_assert(!std::is_nothrow_move_constructible<World>::value,
              "World move construction allocates a generation and can throw");

World::World()
    : generation_(AcquireWorldGeneration()),
      physicsWorld(std::make_unique<PhysicsWorld>()),
      scheduler(std::make_unique<Scheduler>(this)) {
}

World::~World() {
    // 이 세대의 UI 런타임 캐시를 회수한다. Shutdown 전에 부른다 — 아래에서
    // 내용이 사라져도 세대 번호는 그대로지만, 순서를 고정해 두는 편이 읽기 쉽다.
    molga::ui::NotifyUIWorldReleased(generation_);
    Shutdown();
}

World::World(World&& other) {
    // 기본 생성자로 위임하지 않는다. 위임하면 곧바로 버려질 세대를 한 번 더
    // 발급해 이동 하나가 세 개를 소모하고, 쓰지도 않을 PhysicsWorld/Scheduler를
    // 만들었다가 즉시 버린다. 모든 소유 상태는 other에서 온다. 아래 대입이
    // 던지면 이 생성자는 완료되지 않으므로 other도 그대로 남는다.
    *this = std::move(other);
}

World& World::operator=(World&& other) {
    if (this == &other) return *this;
    // 두 세대를 내용에 손대기 전에 확보한다. 확보가 던지면 어느 World도
    // 바뀌지 않은 상태로 남는다.
    const auto replacementGeneration = AcquireWorldGeneration();
    const auto movedFromGeneration = AcquireWorldGeneration();
    // Dispatch and flush guards keep references to these fields. Moving either
    // World while a guard is live would reset state underneath it and can make
    // the outer callback continue on unrelated containers.
    if (IsLifecycleMutationActive() || other.IsLifecycleMutationActive()) {
        throw std::logic_error("cannot move a World during callbacks");
    }
    // 두 세대가 여기서 은퇴한다: 덮어써지는 이쪽의 것과, 비워지는 저쪽의 것.
    molga::ui::NotifyUIWorldReleased(generation_);
    molga::ui::NotifyUIWorldReleased(other.generation_);
    TransferOwnedStateFrom(std::move(other));
    // 옮겨진 쪽에도 새 세대를 발행한다. 비워진 World가 다시 채워지더라도 예전
    // 식별자가 되살아나지 않도록 하는 거절 장치다(물리/스케줄러까지 넘어갔으니
    // 그 World 자체가 곧바로 구동 가능한 상태라는 뜻은 아니다).
    generation_ = replacementGeneration;
    other.generation_ = movedFromGeneration;
    return *this;
}

void World::TransferOwnedStateFrom(World&& other) {
    Shutdown();
    objects_ = std::move(other.objects_);
    name_ = std::move(other.name_);
    physicsWorld = std::move(other.physicsWorld);
    scheduler = std::move(other.scheduler);
    if (scheduler) scheduler->SetWorld(this);
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
    // 컨테이너 이동 "대입"은 원본을 유효하되 미지정 상태로 남긴다 — 비어 있다는
    // 보장이 없다. 나머지 필드를 전부 초기화하면서 이것만 표준 구현에 맡기면
    // 옮겨진 World가 죽은 오브젝트를 계속 들고 있는 것처럼 보일 수 있다.
    other.objects_.clear();
    other.pendingAdds_.clear();
    other.pendingDestroys_.clear();
    other.running_ = false;
    other.sceneRuntime_ = nullptr;
    other.flushingDeferred_ = false;
    other.flushDeferredRequested_ = false;
    other.shuttingDown_ = false;
    other.callbackDispatchDepth_ = 0;
}

GameObject* World::Add(std::shared_ptr<GameObject> obj) {
    if (!obj || shuttingDown_) return nullptr;
    obj->SetWorld(this);
    GameObject* raw = obj.get();
    objects_.push_back(std::move(obj));
    // 오브젝트가 씬에 나타나는 것도 계층 변경이다. Canvas와 무관한 오브젝트는
    // NotifyUIHierarchyChanged가 스스로 걸러낸다.
    NotifyUIHierarchyChanged(raw);
    return raw;
}

GameObject* World::InsertAt(std::shared_ptr<GameObject> obj,
                            std::size_t index) {
    if (!obj || shuttingDown_) return nullptr;
    obj->SetWorld(this);
    GameObject* raw = obj.get();
    index = std::min(index, objects_.size());
    objects_.insert(objects_.begin() + static_cast<std::ptrdiff_t>(index),
                    std::move(obj));
    NotifyUIHierarchyChanged(raw);
    return raw;
}

void World::RemoveByIds(const std::vector<unsigned int>& ids) {
    if (ids.empty()) return;
    // 지우기 전에 알린다. NotifyUIHierarchyChanged는 오브젝트의 부모 사슬을
    // 읽어 Canvas 아래인지 판정하므로, 벡터에서 빠진 뒤에 부르면 판정 자체는
    // 살아 있어도(명령이 undo용으로 shared_ptr을 붙들고 있다) "무엇이 사라졌나"를
    // 말할 수 없다.
    for (const auto& object : objects_) {
        if (!object) continue;
        if (std::find(ids.begin(), ids.end(), object->GetID()) != ids.end()) {
            NotifyUIHierarchyChanged(object.get());
        }
    }
    objects_.erase(
        std::remove_if(objects_.begin(), objects_.end(),
                       [&](const std::shared_ptr<GameObject>& object) {
                           if (!object) return false;
                           return std::find(ids.begin(), ids.end(),
                                            object->GetID()) != ids.end();
                       }),
        objects_.end());
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

void World::Clear() {
    // 세대를 먼저 얻고 콘텐츠를 비우기 전에 발행한다. OnDestroy 콜백이 남아
    // 있는 런타임 식별자로 이 World를 다시 해석하려 들면, 그 시점에 이미
    // 세대가 달라 실패해야 한다.
    const auto generation = AcquireWorldGeneration();
    molga::ui::NotifyUIWorldReleased(generation_);
    generation_ = generation;
    Shutdown();
}

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
void World::EvaluateAnimations(float dt) {
    CallbackDispatchGuard dispatchGuard(callbackDispatchDepth_);
    MOLGA_PROFILE_SCOPE("World.EvaluateAnimations", molga::ProfileCategory::Scripts);
    const auto phaseObjects = objects_;
    for (const auto& object : phaseObjects) {
        if (!OwnsObject(object) || !object->IsActive()) continue;
        Animator2D* animator = object->GetComponent<Animator2D>();
        if (animator && animator->IsEnabled()) animator->Evaluate(dt);
    }
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
    // 임시 벡터로 먼저 싣는다. DeserializeScene은 목적지를 즉시 비우므로,
    // objects_에 바로 실으면 파싱 실패가 기존 씬을 지워 버린다. 실패한 로드는
    // 내용도 세대도 그대로 두어야 살아 있는 식별자가 계속 유효하다.
    std::vector<std::shared_ptr<GameObject>> loaded;
    if (!SceneSerializer::LoadScene(path, loaded)) return false;
    const auto generation = AcquireWorldGeneration();
    molga::ui::NotifyUIWorldReleased(generation_);
    objects_.swap(loaded);
    generation_ = generation;
    for (auto& o : objects_) {
        if (o) o->SetWorld(this);
    }
    return true;
}
void World::RepublishGenerationAfterExternalReplacement() {
    // 에디터의 New/Open Scene은 Objects()를 통해 밖에서 내용을 통째로
    // 교체하므로 LoadFromFile/Clear를 지나지 않는다. 세대를 그대로 두면
    // 이전 씬의 기하 캐시 항목이 새 씬에 그대로 적중한다 — 오브젝트 id와
    // 저작 revision은 씬이 달라도 겹칠 수 있고, 기하 키에는 그 둘을 넘어
    // 실패로 닫을 네 번째 필드가 없다. 교체에 성공한 직후에만 부른다.
    molga::ui::NotifyUIWorldReleased(generation_);
    generation_ = AcquireWorldGeneration();
    for (auto& object : objects_) {
        if (object) object->SetWorld(this);
    }
    molga::ui::NotifyUISemanticMutation();
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
            // 사라지는 오브젝트가 UI Canvas 서브트리에 닿아 있었다면 스냅샷이
            // 달라진다. 벡터에서 지운 뒤에는 부모 사슬을 더 볼 수 없으므로
            // 지우기 전에 판정한다.
            for (const auto& object : objects_) {
                if (object && subtreeIdSet.count(object->GetID()) != 0) {
                    NotifyUIHierarchyChanged(object.get());
                }
            }
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
