#ifndef MOLGA_SCENE_SERIALIZER_H
#define MOLGA_SCENE_SERIALIZER_H

#include <string>
#include <vector>
#include <memory>

class GameObject;

class SceneSerializer {
public:
    // Save scene to JSON file
    static bool SaveScene(const std::string& filepath,
                          const std::vector<std::shared_ptr<GameObject>>& objects);

    // Load scene from JSON file
    static bool LoadScene(const std::string& filepath,
                          std::vector<std::shared_ptr<GameObject>>& objects);

    // Serialize single GameObject to JSON string
    static std::string SerializeGameObject(const GameObject* obj);

    // Deserialize single GameObject from JSON string
    static std::shared_ptr<GameObject> DeserializeGameObject(const std::string& jsonStr);
};

#endif // MOLGA_SCENE_SERIALIZER_H
