#include "FontManager.h"
#include <iostream>
#include <filesystem>

FontManager& FontManager::Get() {
    static FontManager instance;
    return instance;
}

bool FontManager::Init() {
    if (initialized) {
        return true;
    }

    if (!LoadFonts()) {
        std::cerr << "[FontManager] Failed to load fonts, using ImGui defaults" << std::endl;
        return false;
    }

    initialized = true;
    std::cout << "[FontManager] Fonts initialized successfully" << std::endl;
    return true;
}

bool FontManager::LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // Font paths
    const std::string fontDir = "assets/fonts/";
    const std::string interPath = fontDir + "Inter-Regular.ttf";
    const std::string monoPath = fontDir + "JetBrainsMono-Regular.ttf";
    const std::string iconPath = fontDir + "fa-solid-900.ttf";

    // Check if font files exist
    if (!std::filesystem::exists(interPath)) {
        std::cerr << "[FontManager] Inter font not found: " << interPath << std::endl;
        return false;
    }
    if (!std::filesystem::exists(monoPath)) {
        std::cerr << "[FontManager] JetBrains Mono font not found: " << monoPath << std::endl;
        return false;
    }
    if (!std::filesystem::exists(iconPath)) {
        std::cerr << "[FontManager] Font Awesome not found: " << iconPath << std::endl;
        return false;
    }

    // Font Awesome config for merging icons
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = iconFontSize;

    // Icon glyph ranges (Font Awesome 6)
    static const ImWchar iconRanges[] = { 0xf000, 0xf8ff, 0 };

    // Default font (Inter Regular) with merged icons
    fonts[FontType::Default] = io.Fonts->AddFontFromFileTTF(
        interPath.c_str(),
        defaultFontSize,
        nullptr,
        io.Fonts->GetGlyphRangesDefault()
    );
    if (!fonts[FontType::Default]) {
        std::cerr << "[FontManager] Failed to load Inter font" << std::endl;
        return false;
    }

    // Merge Font Awesome icons into default font
    io.Fonts->AddFontFromFileTTF(
        iconPath.c_str(),
        iconFontSize,
        &iconConfig,
        iconRanges
    );

    // Small font
    fonts[FontType::Small] = io.Fonts->AddFontFromFileTTF(
        interPath.c_str(),
        defaultFontSize * 0.85f,
        nullptr,
        io.Fonts->GetGlyphRangesDefault()
    );

    // Large font
    fonts[FontType::Large] = io.Fonts->AddFontFromFileTTF(
        interPath.c_str(),
        defaultFontSize * 1.25f,
        nullptr,
        io.Fonts->GetGlyphRangesDefault()
    );

    // Monospace font (JetBrains Mono) for code
    fonts[FontType::Monospace] = io.Fonts->AddFontFromFileTTF(
        monoPath.c_str(),
        defaultFontSize,
        nullptr,
        io.Fonts->GetGlyphRangesDefault()
    );
    if (!fonts[FontType::Monospace]) {
        std::cerr << "[FontManager] Failed to load JetBrains Mono font" << std::endl;
        return false;
    }

    // Icon-only font (for when you need just icons without merging)
    fonts[FontType::Icons] = io.Fonts->AddFontFromFileTTF(
        iconPath.c_str(),
        iconFontSize,
        nullptr,
        iconRanges
    );

    // Set default font (no need to call Build() - new ImGui does it automatically)
    io.FontDefault = fonts[FontType::Default];

    return true;
}

ImFont* FontManager::GetFont(FontType type) const {
    auto it = fonts.find(type);
    if (it != fonts.end()) {
        return it->second;
    }
    return nullptr;
}

void FontManager::PushFont(FontType type) {
    ImFont* font = GetFont(type);
    if (font) {
        ImGui::PushFont(font);
    }
}

void FontManager::PopFont() {
    ImGui::PopFont();
}
