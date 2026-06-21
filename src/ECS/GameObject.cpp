#include "GameObject.h"
#include "Component.h"
#include "../Scripting/Script.h"
#include "../Core/World.h"
#include <algorithm>
#include <cassert>

unsigned int GameObject::nextID = 1;

GameObject::GameObject(const std::string& name)
    : id(nextID++), name(name) {
}

#include "Common/Log.h"

GameObject::~GameObject() {
    NotifyDestroy();  // destroyed flag prevents double-call
    for (auto* comp : componentOrder_) {
        comp->OnDetach();
    }
    componentOrder_.clear();
    componentMap.clear();
    if (parent) {
        parent->RemoveChild(this);
    }
    // 살아남는 자식들이 해제될 우리를 가리키지 않도록 parent를 끊는다.
    for (auto* child : children) {
        if (child) child->parent = nullptr;
    }
    children.clear();
}

bool GameObject::SetParent(GameObject* newParent) {
    if (parent == newParent) return true;
    if (newParent == this) {
        Log::Warn("GameObject", "Ignoring attempt to parent '" + name + "' to itself");
        return false;
    }
    if (newParent && IsAncestorOf(newParent)) {
        Log::Warn("GameObject", "Ignoring reparent of '" + name + "' that would create a cycle");
        return false;
    }

    // Remove from old parent
    if (parent) {
        parent->RemoveChild(this);
    }

    // Set new parent
    parent = newParent;

    // Add to new parent's children
    if (parent) {
        auto& siblings = parent->children;
        if (std::find(siblings.begin(), siblings.end(), this) == siblings.end()) {
            siblings.push_back(this);
        }
    }
    return true;
}

bool GameObject::IsAncestorOf(const GameObject* node) const {
    for (const GameObject* p = (node ? node->parent : nullptr); p; p = p->parent) {
        if (p == this) return true;
    }
    return false;
}

void GameObject::CollectSubtree(std::vector<GameObject*>& out) {
    out.push_back(this);
    for (auto* child : children) {
        if (child) child->CollectSubtree(out);
    }
}

void GameObject::AddChild(GameObject* child) {
    if (!child || child == this) return;

    // Check if already a child
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) return;

    child->SetParent(this);
}

void GameObject::RemoveChild(GameObject* child) {
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) {
        (*it)->parent = nullptr;
        children.erase(it);
    }
}

void GameObject::Update(float dt) {
    if (!active) return;

    for (auto* comp : componentOrder_) {
        if (comp->IsEnabled()) {
            comp->Update(dt);
        }
    }
}

void GameObject::Render() {
    if (!active) return;

    for (auto* comp : componentOrder_) {
        if (comp->IsEnabled()) {
            comp->Render();
        }
    }
}

Component* GameObject::AddComponentRaw(Component* component) {
    if (!component) return nullptr;
    auto typeId = component->GetRuntimeTypeID();
    if (componentMap.count(typeId) > 0) {
        assert(false && "Duplicate component type via AddComponentRaw.");
        return nullptr;
    }
    component->SetGameObject(this);
    component->OnAttach();
    componentMap[typeId] = std::unique_ptr<Component>(component);
    componentOrder_.push_back(component);
    return component;
}

std::vector<Component*> GameObject::GetComponents() const {
    return componentOrder_;  // 삽입 순서(결정적)
}

void GameObject::NotifyDestroy() {
    if (destroyed) return;
    destroyed = true;

    // Snapshot-based iteration (safe if callbacks modify the component list)
    std::vector<Component*> snapshot = componentOrder_;
    for (auto* comp : snapshot) {
        if (comp->IsEnabled()) comp->OnDisable();
        comp->OnDestroy();
    }
}

void GameObject::FixedUpdateScripts(float fixedDt) {
    if (!active) return;
    for (auto* comp : componentOrder_) {
        if (comp->IsEnabled()) {
            if (auto* script = dynamic_cast<Script*>(comp)) {
                script->FixedUpdate(fixedDt);
            }
        }
    }
}

void GameObject::LateUpdateScripts(float dt) {
    if (!active) return;
    for (auto* comp : componentOrder_) {
        if (comp->IsEnabled()) {
            if (auto* script = dynamic_cast<Script*>(comp)) {
                script->LateUpdate(dt);
            }
        }
    }
}

void GameObject::AwakeScripts() {
    if (!active) return;
    for (auto* comp : componentOrder_) {
        if (comp->IsEnabled() && !comp->HasAwoken()) {
            comp->Awake();
            comp->MarkAwoken();
        }
    }
}

void GameObject::EnableScripts() {
    if (!active) return;
    for (auto* comp : componentOrder_) {
        if (comp->IsEnabled()) comp->OnEnable();
    }
}

void GameObject::DisableScripts() {
    for (auto* comp : componentOrder_) {
        if (comp->IsEnabled()) comp->OnDisable();
    }
}

void GameObject::StartScripts() {
    if (!active) return;
    for (auto* comp : componentOrder_) {
        if (!comp->IsEnabled()) continue;
        if (!comp->HasStarted()) {
            comp->Start();
            comp->MarkStarted();
        }
    }
}

void GameObject::ResolveAssets() {
    for (auto* comp : componentOrder_) {
        if (comp) comp->ResolveAssets();
    }
}

void GameObject::SetActive(bool value) {
    if (active == value) return;
    active = value;

    // 라이프사이클 콜백은 월드가 Play(running) 중일 때만 발화한다.
    // 에디트/로드 중 SetActive는 플래그만 바꾼다(오발화 방지).
    if (!world || !world->IsRunning()) return;

    if (value) {
        AwakeScripts();   // 최초 활성화면 Awake 1회
        EnableScripts();  // OnEnable
        StartScripts();   // 최초 활성화면 Start 1회
    } else {
        DisableScripts(); // OnDisable
    }
}
