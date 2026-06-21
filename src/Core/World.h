#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "Common/Types.h"
#include "ECS/GameObject.h"   // 템플릿 FindObjectOfType<T> 등에서 GetComponent<T> 사용

class PhysicsWorld;
class Scheduler;

// 편집/플레이/런타임이 공유하는 단일 씬 데이터 모델.
class World {
public:
    World();
    ~World();

    World(World&&) noexcept;
    World& operator=(World&&) noexcept;

    GameObject* Add(std::shared_ptr<GameObject> obj);
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

    std::vector<std::shared_ptr<GameObject>>& Objects() { return objects_; }
    const std::vector<std::shared_ptr<GameObject>>& Objects() const { return objects_; }

    const std::string& Name() const { return name_; }
    void SetName(const std::string& n) { name_ = n; }

    // 월드가 Play(시뮬레이션) 중인지. StartPending 이후 true.
    // 이 값이 true일 때만 GameObject::SetActive가 라이프사이클 콜백을 발화한다.
    bool IsRunning() const { return running_; }

    // 명시적 업데이트 순서
    // 모든 컴포넌트 Awake() → 모든 컴포넌트 Start() (배치 순서 보장) 후 running_=true.
    void StartPending();
    void FixedStep(float fixedDt);   // 스크립트 FixedUpdate
    void Update(float dt);           // 전 컴포넌트 Update
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

private:
    std::vector<std::shared_ptr<GameObject>> objects_;
    std::string name_ = "Untitled";
    std::unique_ptr<PhysicsWorld> physicsWorld;
    std::unique_ptr<Scheduler> scheduler;
    bool running_ = false;

    // 지연 추가/삭제 큐
    std::vector<std::shared_ptr<GameObject>> pendingAdds_;
    struct PendingDestroy {
        unsigned int id;
        float delay;
    };
    std::vector<PendingDestroy> pendingDestroys_;
};

