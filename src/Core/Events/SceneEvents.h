#pragma once

#include "../Event.h"
#include <string>

// Fired after a scene finishes loading
struct SceneLoadEvent : EventBase {
    std::string sceneName;
};

// Fired before a scene begins unloading
struct SceneUnloadEvent : EventBase {
    std::string sceneName;
};
