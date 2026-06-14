#include "Core/World.h"
#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"
#include "Physics/PhysicsWorld.h"

World::World()
    : physicsWorld(std::make_unique<PhysicsWorld>()) {
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
    for (auto& o : objects_) if (o) o->StartScripts();
}
void World::FixedStep(float fixedDt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->FixedUpdateScripts(fixedDt);
    physicsWorld->Step(*this, fixedDt);
}
void World::Update(float dt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->Update(dt);
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
