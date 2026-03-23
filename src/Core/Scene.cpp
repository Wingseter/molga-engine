#include "Scene.h"
#include "../Common/Log.h"
#include <iostream>

std::unordered_map<std::string, std::shared_ptr<Scene>> SceneManager::scenes;
std::shared_ptr<Scene> SceneManager::currentScene = nullptr;
std::string SceneManager::pendingScene = "";
bool SceneManager::sceneChangeRequested = false;

void SceneManager::AddScene(const std::string& name, std::shared_ptr<Scene> scene) {
    scenes[name] = scene;
}

void SceneManager::RemoveScene(const std::string& name) {
    auto it = scenes.find(name);
    if (it != scenes.end()) {
        if (currentScene == it->second) {
            currentScene->OnExit();
            currentScene = nullptr;
        }
        scenes.erase(it);
    }
}

bool SceneManager::ChangeScene(const std::string& name) {
    if (scenes.find(name) == scenes.end()) {
        Log::Error("SceneManager", "Scene not found: " + name);
        return false;
    }
    pendingScene = name;
    sceneChangeRequested = true;
    return true;
}

void SceneManager::Update(float dt) {
    // Handle pending scene change
    if (sceneChangeRequested) {
        auto it = scenes.find(pendingScene);
        if (it != scenes.end()) {
            if (currentScene) {
                currentScene->OnExit();
            }
            currentScene = it->second;
            currentScene->OnEnter();
        } else {
            Log::Error("SceneManager", "Pending scene not found: " + pendingScene);
        }
        sceneChangeRequested = false;
        pendingScene = "";
    }

    // Update current scene
    if (currentScene) {
        currentScene->Update(dt);
    }
}

void SceneManager::Render(Renderer* renderer, Shader* shader, Camera2D* camera) {
    if (currentScene) {
        currentScene->Render(renderer, shader, camera);
    }
}

Scene* SceneManager::GetCurrentScene() {
    return currentScene.get();
}

const std::string& SceneManager::GetCurrentSceneName() {
    static std::string empty = "";
    return currentScene ? currentScene->GetName() : empty;
}

void SceneManager::Clear() {
    if (currentScene) {
        currentScene->OnExit();
    }
    currentScene = nullptr;
    scenes.clear();
    pendingScene = "";
    sceneChangeRequested = false;
}
