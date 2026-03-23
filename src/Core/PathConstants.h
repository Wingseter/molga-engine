#pragma once

namespace Paths {
    namespace Project {
        constexpr const char* ASSETS = "Assets";
        constexpr const char* TEXTURES = "Assets/Textures";
        constexpr const char* AUDIO = "Assets/Audio";
        constexpr const char* SCENES = "Scenes";
        constexpr const char* SCRIPTS = "Scripts";
        constexpr const char* SCRIPTS_BUILD = "Scripts/build";
        constexpr const char* SETTINGS = "ProjectSettings";
        constexpr const char* FILE = "project.molga";
    }
    namespace Build {
        constexpr const char* ASSETS = "assets";
        constexpr const char* SCENES = "scenes";
        constexpr const char* SHADERS = "Shaders";
    }
    namespace Engine {
        constexpr const char* SHADER_VERT = "Shaders/default.vert";
        constexpr const char* SHADER_FRAG = "Shaders/default.frag";
        constexpr const char* SHADER_SRC_DIR = "src/Shaders";
    }
    namespace Config {
        constexpr const char* DIR = ".molga";
        constexpr const char* RECENT_PROJECTS = "recent_projects.json";
    }
}
