#ifndef MOLGA_COMPONENT_FACTORY_H
#define MOLGA_COMPONENT_FACTORY_H

#include <string>
#include <functional>
#include <unordered_map>
#include <memory>

class Component;
class GameObject;

class ComponentFactory {
    using Creator = std::function<Component*(GameObject*)>;
    std::unordered_map<std::string, Creator> creators;

public:
    static ComponentFactory& Get() {
        static ComponentFactory instance;
        return instance;
    }

    template<typename T>
    void Register(const std::string& typeName) {
        creators[typeName] = [](GameObject* obj) -> Component* {
            return obj->AddComponent<T>();
        };
    }

    void Register(const std::string& typeName, Creator creator) {
        creators[typeName] = std::move(creator);
    }

    Component* Create(const std::string& typeName, GameObject* obj) {
        auto it = creators.find(typeName);
        if (it != creators.end()) {
            return it->second(obj);
        }
        return nullptr;
    }

    bool HasType(const std::string& typeName) const {
        return creators.count(typeName) > 0;
    }
};

// Auto-registration macro for components
#define REGISTER_COMPONENT(CompClass) \
    namespace { \
        struct CompClass##ComponentRegistrar { \
            CompClass##ComponentRegistrar() { \
                ComponentFactory::Get().Register<CompClass>(CompClass::StaticTypeName()); \
            } \
        }; \
        static CompClass##ComponentRegistrar g_##CompClass##Registrar; \
    }

#endif // MOLGA_COMPONENT_FACTORY_H
