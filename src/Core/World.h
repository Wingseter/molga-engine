#pragma once

#include <memory>
#include <string>
#include <vector>

class GameObject;
class PhysicsWorld;

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
    void Clear();

    std::vector<std::shared_ptr<GameObject>>& Objects() { return objects_; }
    const std::vector<std::shared_ptr<GameObject>>& Objects() const { return objects_; }

    const std::string& Name() const { return name_; }
    void SetName(const std::string& n) { name_ = n; }

    // 명시적 업데이트 순서
    void StartPending();             // 미시작 스크립트 Start()
    void FixedStep(float fixedDt);   // 스크립트 FixedUpdate
    void Update(float dt);           // 전 컴포넌트 Update
    void LateUpdate(float dt);       // 스크립트 LateUpdate
    void ResolveAssets();            // 모든 컴포넌트의 지연 에셋 로드

    // 직렬화 기반 독립 복제
    std::unique_ptr<World> Clone() const;

    // 공용 로드/세이브
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;

    PhysicsWorld* GetPhysicsWorld() const { return physicsWorld.get(); }

private:
    std::vector<std::shared_ptr<GameObject>> objects_;
    std::string name_ = "Untitled";
    std::unique_ptr<PhysicsWorld> physicsWorld;
};
