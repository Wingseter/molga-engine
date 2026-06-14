#include "Core/World.h"
#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"

GameObject* World::Add(std::shared_ptr<GameObject> obj) {
    if (!obj) return nullptr;
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

void World::Clear() { objects_.clear(); }

void World::StartPending() {
    for (auto& o : objects_) if (o) o->StartScripts();
}
void World::FixedStep(float fixedDt) {
    for (auto& o : objects_) if (o && o->IsActive()) o->FixedUpdateScripts(fixedDt);
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
    copy->name_ = name_;
    return copy;
}

bool World::LoadFromFile(const std::string& path) {
    return SceneSerializer::LoadScene(path, objects_);
}
bool World::SaveToFile(const std::string& path) const {
    return SceneSerializer::SaveScene(path, objects_);
}
