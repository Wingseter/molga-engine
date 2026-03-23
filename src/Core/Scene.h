#ifndef MOLGA_SCENE_H
#define MOLGA_SCENE_H

#include <string>
#include <memory>
#include <unordered_map>

class Renderer;
class Shader;
class Camera2D;

// Base Scene class - inherit to create game scenes
class Scene {
public:
    Scene(const std::string& name) : name(name) {}
    virtual ~Scene() = default;

    virtual void OnEnter() {}   // Called when scene becomes active
    virtual void OnExit() {}    // Called when leaving scene
    virtual void Update(float dt) = 0;
    virtual void Render(Renderer* renderer, Shader* shader, Camera2D* camera) = 0;

    const std::string& GetName() const { return name; }

protected:
    std::string name;
};

// Scene Manager - handles scene transitions
class SceneManager {
public:
    static void AddScene(const std::string& name, std::shared_ptr<Scene> scene);
    static void RemoveScene(const std::string& name);
    static void ChangeScene(const std::string& name);
    static void Update(float dt);
    static void Render(Renderer* renderer, Shader* shader, Camera2D* camera);

    static Scene* GetCurrentScene();
    static const std::string& GetCurrentSceneName();

    static void Clear();

private:
    static std::unordered_map<std::string, std::shared_ptr<Scene>> scenes;
    static std::shared_ptr<Scene> currentScene;
    static std::string pendingScene;
    static bool sceneChangeRequested;
};

#endif
