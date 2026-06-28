#pragma once

#include <nlohmann/json.hpp>
#include <string>

class GameObject;
class Component;

namespace molga {

// Safe lookup for GameObject in the current editor world
GameObject* FindGameObjectById(unsigned int id);

// Snapshot utilities for components and GameObjects
nlohmann::json CaptureComponentSnapshot(const Component* comp);
void RestoreComponentSnapshot(GameObject* obj, const nlohmann::json& compJson);

nlohmann::json CaptureGameObjectProperties(const GameObject* obj);
void RestoreGameObjectProperties(GameObject* obj, const nlohmann::json& propJson);

nlohmann::json CaptureGameObjectComponents(const GameObject* obj);
void RestoreGameObjectComponents(GameObject* obj, const nlohmann::json& componentsJson);

} // namespace molga
