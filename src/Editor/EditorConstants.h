#ifndef MOLGA_EDITOR_CONSTANTS_H
#define MOLGA_EDITOR_CONSTANTS_H

namespace EditorConstants {
    // Dockspace
    constexpr const char* DOCKSPACE_ID = "MolgaDockSpace";

    // Window Names (used for both ImGui::Begin and DockBuilder)
    constexpr const char* WIN_HIERARCHY = "Hierarchy";
    constexpr const char* WIN_INSPECTOR = "Inspector";
    constexpr const char* WIN_PROJECT_BROWSER = "Project Browser";
    constexpr const char* WIN_SCENE = "Scene";
    constexpr const char* WIN_SCRIPTS = "Scripts";
    constexpr const char* WIN_STATS = "Stats";
    constexpr const char* WIN_BUILD_SETTINGS = "Build Settings";
    constexpr const char* WIN_SCRIPT_EDITOR = "Script Editor";

    // Default Files
    constexpr const char* DEFAULT_SCENE_FILE = "scene.json";

    // Build Defaults
    constexpr const char* DEFAULT_BUILD_TYPE = "Debug";
}

#endif // MOLGA_EDITOR_CONSTANTS_H
