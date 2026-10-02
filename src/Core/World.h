#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "Common/Types.h"
#include "ECS/GameObject.h"   // 템플릿 FindObjectOfType<T> 등에서 GetComponent<T> 사용

class PhysicsWorld;
class Scheduler;
class SceneRuntime;

// Task 9.1: 세대 할당기 테스트 훅. 소진 경로는 프로세스 전역 원자값을 직접
// 세워 놓고 관찰해야만 재현되는데, 그 원자값은 World.cpp의 익명 이름공간에
// 산다. 테스트 전용이며 스코프를 벗어나면 직전 값으로 되돌린다.
std::uint64_t WorldGenerationForTesting();

class ScopedWorldGenerationForTesting {
public:
    explicit ScopedWorldGenerationForTesting(std::uint64_t next);
    ~ScopedWorldGenerationForTesting();

    ScopedWorldGenerationForTesting(const ScopedWorldGenerationForTesting&) = delete;
    ScopedWorldGenerationForTesting& operator=(
        const ScopedWorldGenerationForTesting&) = delete;

private:
    std::uint64_t previous_;
    std::uint64_t seeded_;
};

// 편집/플레이/런타임이 공유하는 단일 씬 데이터 모델.
class World {
public:
    World();
    ~World();

    // Task 9.1: 이동은 세대를 새로 할당해야 하고 할당은 소진 시 던진다.
    // 그래서 noexcept가 아니다 — noexcept로 두면 소진이 std::terminate가 된다.
    World(World&&);
    World& operator=(World&&);

    // 프로세스 전역·단조·0이 아닌·재사용 없는 월드 세대. 생성/Clear/성공한
    // 씬 로드/이동 교체가 각각 새 값을 발행하므로, 예전 세대를 담은 런타임
    // 식별자는 오브젝트 id와 컴포넌트 타입이 그대로여도 다시 해석되지 않는다.
    std::uint64_t Generation() const noexcept { return generation_; }

    GameObject* Add(std::shared_ptr<GameObject> obj);
    // 에디터의 붙여넣기/실행취소는 순서를 보존해야 하고, 삭제는 id 집합으로
    // 들어온다. 그 두 경로가 objects_를 직접 만지면 Add에 달린 계층 알림을
    // 건너뛰므로, 세 mutator를 모두 여기 둔다 — World가 계층 변경을 발행하는
    // 유일한 자리라는 계약이 그때만 참이 된다.
    GameObject* InsertAt(std::shared_ptr<GameObject> obj, std::size_t index);
    void RemoveByIds(const std::vector<unsigned int>& ids);
    GameObject* FindById(unsigned int id) const;
    GameObject* FindWithTag(const std::string& tag) const;
    std::vector<GameObject*> FindAllWithTag(const std::string& tag) const;

    // 이름으로 첫 활성 오브젝트를 찾는다 (Unity GameObject.Find).
    GameObject* Find(const std::string& name) const;

    // 타입 T 컴포넌트를 가진 첫 활성 오브젝트의 컴포넌트를 반환 (Unity FindObjectOfType).
    template<typename T>
    T* FindObjectOfType() const {
        for (const auto& o : objects_) {
            if (o && o->IsActive()) {
                if (T* c = o->GetComponent<T>()) return c;
            }
        }
        return nullptr;
    }

    // 타입 T 컴포넌트를 가진 모든 활성 오브젝트의 컴포넌트 목록.
    template<typename T>
    std::vector<T*> FindObjectsOfType() const {
        std::vector<T*> result;
        for (const auto& o : objects_) {
            if (o && o->IsActive()) {
                if (T* c = o->GetComponent<T>()) result.push_back(c);
            }
        }
        return result;
    }

    void Clear();
    void Shutdown() noexcept;

    // Task 10.2: 씬 교체가 LoadFromFile/Clear를 거치지 않고 Objects()를 통해
    // 밖에서 일어났을 때, 성공한 직후 새 세대를 발행한다. 이것이 없으면 그
    // 교체는 살아 있는 런타임 식별자와 세대로 키를 잡는 캐시에 보이지 않고,
    // 다음 프레임이 이전 씬의 기하를 그대로 재사용한다.
    //
    // 소진되면 던진다. AcquireWorldGeneration과 같은 계약이다 — 감아서
    // 재사용하면 옛 세대가 새 씬을 가리킨다.
    void RepublishGenerationAfterExternalReplacement();

    std::vector<std::shared_ptr<GameObject>>& Objects() { return objects_; }
    const std::vector<std::shared_ptr<GameObject>>& Objects() const { return objects_; }

    const std::string& Name() const { return name_; }
    void SetName(const std::string& n) { name_ = n; }

    // 월드가 Play(시뮬레이션) 중인지. StartPending 이후 true.
    // 이 값이 true일 때만 GameObject::SetActive가 라이프사이클 콜백을 발화한다.
    bool IsRunning() const { return running_; }
    // Script/component/scheduler/physics callbacks are currently executing on
    // this World. Scene replacement must wait until this returns false.
    bool IsDispatchingCallbacks() const { return callbackDispatchDepth_ != 0; }

    // 명시적 업데이트 순서
    // 모든 컴포넌트 Awake() → 모든 컴포넌트 Start() (배치 순서 보장) 후 running_=true.
    void StartPending();
    void FixedStep(float fixedDt);   // 스크립트 FixedUpdate
    void Update(float dt);           // 전 컴포넌트 Update
    void EvaluateAnimations(float dt); // Animator2D 전용, Update 이후/LateUpdate 이전
    void LateUpdate(float dt);       // 스크립트 LateUpdate
    void ResolveAssets();            // 모든 컴포넌트의 지연 에셋 로드

    // 런타임 생명주기 API
    GameObject* Instantiate(const GameObject* original);
    GameObject* Instantiate(const GameObject* original, const Vector2& worldPos);
    GameObject* Instantiate(const GameObject* original, GameObject* parent);
    GameObject* InstantiatePrefab(const std::string& guid);
    void Destroy(GameObject* obj, float delay = 0.0f);
    void FlushDeferred(float dt);

    // 직렬화 기반 독립 복제
    std::unique_ptr<World> Clone() const;

    // 공용 로드/세이브
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    PhysicsWorld* GetPhysicsWorld() const { return physicsWorld.get(); }
    Scheduler* GetScheduler() const { return scheduler.get(); }
    SceneRuntime* GetSceneRuntime() const { return sceneRuntime_; }
    void SetSceneRuntime(SceneRuntime* runtime) { sceneRuntime_ = runtime; }

private:
    std::vector<std::shared_ptr<GameObject>> objects_;
    std::string name_ = "Untitled";
    std::uint64_t generation_ = 0;
    std::unique_ptr<PhysicsWorld> physicsWorld;
    std::unique_ptr<Scheduler> scheduler;
    bool running_ = false;
    SceneRuntime* sceneRuntime_ = nullptr;
    bool flushingDeferred_ = false;
    bool flushDeferredRequested_ = false;
    bool shuttingDown_ = false;
    unsigned int callbackDispatchDepth_ = 0;

    bool OwnsObject(const std::shared_ptr<GameObject>& object) const;
    bool IsLifecycleMutationActive() const;

    // 필드 이전과 소유자/스케줄러 재바인딩만 한다. 세대를 얻지도 발행하지도
    // 않으므로, 호출자가 두 세대를 먼저 확보한 뒤에야 내용을 옮길 수 있다.
    void TransferOwnedStateFrom(World&& other);

    // 지연 추가/삭제 큐
    std::vector<std::shared_ptr<GameObject>> pendingAdds_;
    struct PendingDestroy {
        unsigned int id;
        float delay;
    };
    std::vector<PendingDestroy> pendingDestroys_;
};
