#pragma once

#include "../Event.h"
#include <string>

// Fired after a scene finishes loading
struct SceneLoadEvent : EventBase {
    std::string sceneName;
    std::string scenePath;
};

// Fired before a scene begins unloading
struct SceneUnloadEvent : EventBase {
    std::string sceneName;
    std::string scenePath;
};

// Fired when a registered scene cannot be prepared. The active scene is left
// untouched and error contains a user-facing diagnostic.
struct SceneLoadFailedEvent : EventBase {
    std::string scenePath;
    std::string error;
};
