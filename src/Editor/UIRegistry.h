#pragma once

#include <imgui.h>
#include <string>
#include <unordered_map>

class UIRegistry {
public:
    struct FileTypeInfo {
        const char* icon;
        ImVec4 color;
    };

    struct ComponentTypeInfo {
        const char* icon;
    };

    static const FileTypeInfo& GetFileTypeInfo(const std::string& extension, bool isDirectory);
    static const ComponentTypeInfo& GetComponentInfo(const std::string& typeName);

private:
    static const std::unordered_map<std::string, FileTypeInfo> s_fileTypes;
    static const std::unordered_map<std::string, ComponentTypeInfo> s_componentTypes;
    static const FileTypeInfo s_folderType;
    static const FileTypeInfo s_defaultFileType;
    static const ComponentTypeInfo s_defaultComponentType;
    static const ComponentTypeInfo s_scriptComponentType;
};
