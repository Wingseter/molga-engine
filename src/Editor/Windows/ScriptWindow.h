#ifndef MOLGA_SCRIPT_WINDOW_H
#define MOLGA_SCRIPT_WINDOW_H

#include "EditorWindow.h"
#include <vector>
#include <string>

struct ScriptFileInfo {
    std::string name;
    std::string headerPath;
    std::string sourcePath;
    bool exists;
};

class ScriptWindow : public EditorWindow {
public:
    ScriptWindow();
    ~ScriptWindow() override = default;

    void OnGUI() override;

    // Refresh script list
    void RefreshScriptList();

private:
    void DrawToolbar();
    void DrawScriptList();
    void DrawCreateScriptDialog();
    void DrawCompileStatus();

    // Script list
    std::vector<ScriptFileInfo> scripts;

    // Create dialog state
    bool showCreateDialog = false;
    char newScriptName[128] = "NewScript";

    // Compile state
    bool isCompiling = false;
    std::string compileStatus;
    bool lastCompileSuccess = true;
};

#endif // MOLGA_SCRIPT_WINDOW_H
