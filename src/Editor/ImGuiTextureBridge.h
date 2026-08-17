#pragma once

#include "Rendering/GraphicsDevice.h"

#include <imgui.h>

// Editor-internal bridge only. Native SDL_GPU objects never enter the public
// rendering API; Dear ImGui's official backend receives them at this boundary.
class ImGuiTextureBridge {
public:
    static ImTextureID From(molga::TextureHandle texture);
};
