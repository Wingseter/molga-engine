#include "Editor/ImGuiTextureBridge.h"

#include <cstdint>

ImTextureID ImGuiTextureBridge::From(molga::TextureHandle texture) {
    molga::GraphicsDevice* device = molga::GraphicsDevice::Current();
    if (!device || !texture) return ImTextureID_Invalid;
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(
        device->NativeTextureForImGui(texture)));
}
