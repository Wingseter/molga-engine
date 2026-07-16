#include "GameObject.h"
#include "Component.h"
#include "../Scripting/Script.h"
#include "../Core/World.h"
#include "Common/Log.h"
#include <algorithm>
#include <cassert>
#include <exception>

unsigned int GameObject::nextID = 1;

GameObject::GameObject(const std::string& name)
    : id(nextID++), name(name) {
}

std::vector<GameObject::ComponentIdentity> GameObject::SnapshotComponentIdentities() const {
    std::vector<ComponentIdentity> plan;
    plan.reserve(componentOrder_.size());
    for (const Component* component : componentOrder_) {
        if (component) {
            plan.push_back({component->GetRuntimeTypeID(), component->GetInstanceID()});
        }
    }
    return plan;
}

Component* GameObject::ResolveComponent(const ComponentIdentity& identity) {
    auto it = componentMap.find(identity.typeId);
    if (it == componentMap.end() ||
        it->second->GetInstanceID() != identity.instanceId) {
        return nullptr;
    }
    return it->second.get();
}

void GameObject::InvokeOnDisable(Component* component) {
    if (!component) return;
    const std::uint64_t instanceId = component->GetInstanceID();
    if (destroyed &&
        std::find(disableCallbacksCompletedDuringDestroy_.begin(),
                  disableCallbacksCompletedDuringDestroy_.end(),
                  instanceId) != disableCallbacksCompletedDuringDestroy_.end()) {
        return;
    }
    if (std::find(disableCallbacksInProgress_.begin(),
                  disableCallbacksInProgress_.end(),
                  instanceId) != disableCallbacksInProgress_.end()) {
        return;
    }

    disableCallbacksInProgress_.push_back(instanceId);
    auto finish = [&]() {
        auto it = std::find(disableCallbacksInProgress_.begin(),
                            disableCallbacksInProgress_.end(), instanceId);
        if (it != disableCallbacksInProgress_.end()) {
            disableCallbacksInProgress_.erase(it);
        }
    };
    std::exception_ptr error;
    try {
        component->OnDisable();
    } catch (...) {
        error = std::current_exception();
    }
    finish();
    if (destroyed &&
        std::find(disableCallbacksCompletedDuringDestroy_.begin(),
                  disableCallbacksCompletedDuringDestroy_.end(),
                  instanceId) == disableCallbacksCompletedDuringDestroy_.end()) {
        disableCallbacksCompletedDuringDestroy_.push_back(instanceId);
    }
    if (error) std::rethrow_exception(error);
}

void GameObject::InvokeOnDestroy(Component* component) {
    if (!component) return;
    const std::uint64_t instanceId = component->GetInstanceID();
    if (std::find(destroyCallbacksCompleted_.begin(),
                  destroyCallbacksCompleted_.end(),
                  instanceId) != destroyCallbacksCompleted_.end() ||
        std::find(destroyCallbacksInProgress_.begin(),
                  destroyCallbacksInProgress_.end(),
                  instanceId) != destroyCallbacksInProgress_.end()) {
        return;
    }

    destroyCallbacksInProgress_.push_back(instanceId);
    std::exception_ptr error;
    try {
        component->OnDestroy();
    } catch (...) {
        error = std::current_exception();
    }
    auto active = std::find(destroyCallbacksInProgress_.begin(),
                            destroyCallbacksInProgress_.end(), instanceId);
    if (active != destroyCallbacksInProgress_.end()) {
        destroyCallbacksInProgress_.erase(active);
    }
    if (std::find(destroyCallbacksCompleted_.begin(),
                  destroyCallbacksCompleted_.end(),
                  instanceId) == destroyCallbacksCompleted_.end()) {
        destroyCallbacksCompleted_.push_back(instanceId);
    }
    if (error) std::rethrow_exception(error);
}

GameObject::~GameObject() noexcept {
    NotifyDestroy();  // destroyed flag prevents double-call
    const auto detachPlan = SnapshotComponentIdentities();
    for (const auto& identity : detachPlan) {
        Component* component = ResolveComponent(identity);
        if (!component) continue;
        try {
            component->OnDetach();
        } catch (const std::exception& error) {
            Log::Error("GameObject", "OnDetach failed on '" + name + "': " + error.what());
        } catch (...) {
            Log::Error("GameObject", "OnDetach failed on '" + name + "'.");
        }
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
    if (!active || destroyed) return;

    const auto plan = SnapshotComponentIdentities();
    for (const auto& identity : plan) {
        if (!active || destroyed) break;
        Component* component = ResolveComponent(identity);
        if (component && component->IsEnabled()) component->Update(dt);
    }
}

void GameObject::Render() {
    if (!active || destroyed) return;

    const auto plan = SnapshotComponentIdentities();
    for (const auto& identity : plan) {
        if (!active || destroyed) break;
        Component* component = ResolveComponent(identity);
        if (component && component->IsEnabled()) component->Render();
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

void GameObject::RemoveComponentById(size_t typeId) {
    auto it = componentMap.find(typeId);
    if (it == componentMap.end()) return;

    // Remove the identity from both containers before entering user code.
    // Reentrant removal now sees an absent component, while local ownership
    // keeps the removed instance alive through both lifecycle callbacks.
    std::unique_ptr<Component> removed = std::move(it->second);
    Component* raw = removed.get();
    const std::string typeName = removed->GetTypeName();
    componentOrder_.erase(
        std::remove(componentOrder_.begin(), componentOrder_.end(), raw),
        componentOrder_.end());
    componentMap.erase(it);

    if (removed->IsEnabled()) {
        try {
            InvokeOnDisable(removed.get());
        } catch (const std::exception& error) {
            Log::Error("GameObject", "OnDisable failed while removing '" +
                typeName + "' from '" + name + "': " + error.what());
        } catch (...) {
            Log::Error("GameObject", "OnDisable failed while removing '" +
                typeName + "' from '" + name + "'.");
        }
    }
    if (destroyed) {
        try {
            InvokeOnDestroy(removed.get());
        } catch (const std::exception& error) {
            Log::Error("GameObject", "OnDestroy failed while removing '" +
                typeName + "' from '" + name + "': " + error.what());
        } catch (...) {
            Log::Error("GameObject", "OnDestroy failed while removing '" +
                typeName + "' from '" + name + "'.");
        }
    }
    try {
        removed->OnDetach();
    } catch (const std::exception& error) {
        Log::Error("GameObject", "OnDetach failed while removing '" +
            typeName + "' from '" + name + "': " + error.what());
    } catch (...) {
        Log::Error("GameObject", "OnDetach failed while removing '" +
            typeName + "' from '" + name + "'.");
    }
    removed->SetGameObject(nullptr);
}

std::vector<Component*> GameObject::GetComponents() const {
    return componentOrder_;  // 삽입 순서(결정적)
}

void GameObject::NotifyDestroy() noexcept {
    if (destroyed) return;
    destroyed = true;

    // Re-resolve value identities before every callback: OnDisable can remove
    // or replace itself or a later component synchronously.
    const auto plan = SnapshotComponentIdentities();
    for (const auto& identity : plan) {
        if (Component* component = ResolveComponent(identity);
            component && component->IsEnabled()) {
            try {
                InvokeOnDisable(component);
            } catch (const std::exception& error) {
                Log::Error("GameObject", "OnDisable failed on '" + name + "': " + error.what());
            } catch (...) {
                Log::Error("GameObject", "OnDisable failed on '" + name + "'.");
            }
        }
        if (Component* component = ResolveComponent(identity)) {
            try {
                InvokeOnDestroy(component);
            } catch (const std::exception& error) {
                Log::Error("GameObject", "OnDestroy failed on '" + name + "': " + error.what());
            } catch (...) {
                Log::Error("GameObject", "OnDestroy failed on '" + name + "'.");
            }
        }
    }
}

void GameObject::FixedUpdateScripts(float fixedDt) {
    if (!active || destroyed) return;
    const auto plan = SnapshotComponentIdentities();
    for (const auto& identity : plan) {
        if (!active || destroyed) break;
        Component* component = ResolveComponent(identity);
        if (!component || !component->IsEnabled()) continue;
        if (auto* script = dynamic_cast<Script*>(component)) {
            script->FixedUpdate(fixedDt);
        }
    }
}

void GameObject::LateUpdateScripts(float dt) {
    if (!active || destroyed) return;
    const auto plan = SnapshotComponentIdentities();
    for (const auto& identity : plan) {
        if (!active || destroyed) break;
        Component* component = ResolveComponent(identity);
        if (!component || !component->IsEnabled()) continue;
        if (auto* script = dynamic_cast<Script*>(component)) {
            script->LateUpdate(dt);
        }
    }
}

void GameObject::AwakeScripts() {
    if (!active || destroyed) return;
    const auto plan = SnapshotComponentIdentities();
    std::exception_ptr firstError;
    for (const auto& identity : plan) {
        if (!active || destroyed) break;
        Component* comp = ResolveComponent(identity);
        if (comp && comp->IsEnabled() && !comp->HasAwoken()) {
            try {
                comp->Awake();
                if (Component* stillPresent = ResolveComponent(identity)) {
                    stillPresent->MarkAwoken();
                }
            } catch (...) {
                if (!firstError) firstError = std::current_exception();
            }
        }
    }
    if (firstError) std::rethrow_exception(firstError);
}

void GameObject::EnableScripts() {
    if (!active || destroyed) return;
    const auto plan = SnapshotComponentIdentities();
    std::exception_ptr firstError;
    for (const auto& identity : plan) {
        if (!active || destroyed) break;
        Component* comp = ResolveComponent(identity);
        if (!comp) continue;
        if (!comp->IsEnabled()) continue;
        try {
            comp->OnEnable();
        } catch (...) {
            if (!firstError) firstError = std::current_exception();
        }
    }
    if (firstError) std::rethrow_exception(firstError);
}

void GameObject::DisableScripts() {
    if (destroyed) return;
    const auto plan = SnapshotComponentIdentities();
    std::exception_ptr firstError;
    for (const auto& identity : plan) {
        Component* component = ResolveComponent(identity);
        if (!component || !component->IsEnabled()) continue;
        try {
            InvokeOnDisable(component);
        } catch (...) {
            if (!firstError) firstError = std::current_exception();
        }
    }
    if (firstError) std::rethrow_exception(firstError);
}

void GameObject::StartScripts() {
    if (!active || destroyed) return;
    const auto plan = SnapshotComponentIdentities();
    std::exception_ptr firstError;
    for (const auto& identity : plan) {
        if (!active || destroyed) break;
        Component* comp = ResolveComponent(identity);
        if (!comp) continue;
        if (!comp->IsEnabled()) continue;
        if (!comp->HasStarted()) {
            try {
                comp->Start();
                if (Component* stillPresent = ResolveComponent(identity)) {
                    stillPresent->MarkStarted();
                }
            } catch (...) {
                if (!firstError) firstError = std::current_exception();
            }
        }
    }
    if (firstError) std::rethrow_exception(firstError);
}

void GameObject::ResolveAssets() {
    if (destroyed) return;
    const auto plan = SnapshotComponentIdentities();
    for (const auto& identity : plan) {
        if (destroyed) break;
        if (Component* component = ResolveComponent(identity)) {
            component->ResolveAssets();
        }
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
