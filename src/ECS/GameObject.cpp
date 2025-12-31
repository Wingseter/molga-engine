#include "GameObject.h"
#include "Component.h"
#include <algorithm>

unsigned int GameObject::nextID = 1;

GameObject::GameObject(const std::string& name)
    : id(nextID++), name(name) {
}

GameObject::~GameObject() {
    // Detach all components
    for (auto& comp : components) {
        comp->OnDetach();
    }
    components.clear();

    // Remove from parent
    if (parent) {
        parent->RemoveChild(this);
    }

    // Clear children (but don't delete them - scene manages ownership)
    children.clear();
}

void GameObject::SetParent(GameObject* newParent) {
    if (parent == newParent) return;

    // Remove from old parent
    if (parent) {
        parent->RemoveChild(this);
    }

    // Set new parent
    parent = newParent;

    // Add to new parent's children
    if (parent) {
        parent->children.push_back(this);
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

    for (auto& comp : components) {
        if (comp->IsEnabled()) {
            comp->Update(dt);
        }
    }
}

void GameObject::Render() {
    if (!active) return;

    for (auto& comp : components) {
        if (comp->IsEnabled()) {
            comp->Render();
        }
    }
}

Component* GameObject::AddComponentRaw(Component* component) {
    if (!component) return nullptr;
    component->SetGameObject(this);
    component->OnAttach();
    components.push_back(std::unique_ptr<Component>(component));
    return component;
}
