#pragma once

#include <imgui.h>
#include <string>
#include <unordered_map>

// Font Awesome 6 Free Solid Icons (common icons)
namespace Icons {
    // Files & Folders
    constexpr const char* Folder = "\uf07b";
    constexpr const char* FolderOpen = "\uf07c";
    constexpr const char* File = "\uf15b";
    constexpr const char* FileCode = "\uf1c9";
    constexpr const char* FileImage = "\uf1c5";
    constexpr const char* FileAudio = "\uf1c7";

    // Objects & Shapes
    constexpr const char* Cube = "\uf1b2";
    constexpr const char* Cubes = "\uf1b3";
    constexpr const char* Circle = "\uf111";
    constexpr const char* Square = "\uf0c8";

    // Actions
    constexpr const char* Play = "\uf04b";
    constexpr const char* Pause = "\uf04c";
    constexpr const char* Stop = "\uf04d";
    constexpr const char* StepForward = "\uf051";

    // UI Elements
    constexpr const char* Cog = "\uf013";
    constexpr const char* Cogs = "\uf085";
    constexpr const char* Wrench = "\uf0ad";
    constexpr const char* Eye = "\uf06e";
    constexpr const char* EyeSlash = "\uf070";
    constexpr const char* Lock = "\uf023";
    constexpr const char* Unlock = "\uf09c";
    constexpr const char* Trash = "\uf1f8";
    constexpr const char* Plus = "\uf067";
    constexpr const char* Minus = "\uf068";
    constexpr const char* Times = "\uf00d";
    constexpr const char* Check = "\uf00c";

    // Components
    constexpr const char* Camera = "\uf030";
    constexpr const char* Lightbulb = "\uf0eb";
    constexpr const char* Image = "\uf03e";
    constexpr const char* Code = "\uf121";
    constexpr const char* Music = "\uf001";
    constexpr const char* VolumeUp = "\uf028";

    // Transform
    constexpr const char* ArrowsAlt = "\uf0b2";
    constexpr const char* Expand = "\uf065";
    constexpr const char* Compress = "\uf066";
    constexpr const char* SyncAlt = "\uf2f1";

    // Hierarchy
    constexpr const char* Sitemap = "\uf0e8";
    constexpr const char* ListUl = "\uf0ca";
    constexpr const char* ChevronRight = "\uf054";
    constexpr const char* ChevronDown = "\uf078";

    // Project
    constexpr const char* Save = "\uf0c7";
    constexpr const char* Upload = "\uf093";
    constexpr const char* Download = "\uf019";
    constexpr const char* Undo = "\uf0e2";
    constexpr const char* Redo = "\uf01e";

    // Build
    constexpr const char* Hammer = "\uf6e3";
    constexpr const char* Box = "\uf466";
    constexpr const char* RocketLaunch = "\uf135";
}

class FontManager {
public:
    enum class FontType {
        Default,        // Inter - UI font
        Monospace,      // JetBrains Mono - code font
        Icons,          // Font Awesome - icons
        DefaultBold,    // Inter Bold
        Large,          // Inter Large
        Small           // Inter Small
    };

    static FontManager& Get();

    // Initialize fonts - call after ImGui context is created
    bool Init();

    // Get font by type
    ImFont* GetFont(FontType type) const;

    // Push/pop font for scoped usage
    void PushFont(FontType type);
    void PopFont();

    // Get default font size
    float GetDefaultFontSize() const { return defaultFontSize; }

private:
    FontManager() = default;
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    bool LoadFonts();

    std::unordered_map<FontType, ImFont*> fonts;
    float defaultFontSize = 16.0f;
    float iconFontSize = 14.0f;
    bool initialized = false;
};
