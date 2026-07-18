#include "UIRegistry.h"
#include "EditorTheme.h"
#include "FontManager.h"

const UIRegistry::FileTypeInfo UIRegistry::s_folderType = { Icons::Folder, EditorTheme::FileColor::FOLDER };
const UIRegistry::FileTypeInfo UIRegistry::s_defaultFileType = { Icons::File, EditorTheme::FileColor::DEFAULT };
const UIRegistry::ComponentTypeInfo UIRegistry::s_defaultComponentType = { Icons::Cog };
const UIRegistry::ComponentTypeInfo UIRegistry::s_scriptComponentType = { Icons::Code };

const std::unordered_map<std::string, UIRegistry::FileTypeInfo> UIRegistry::s_fileTypes = {
    { ".png",  { Icons::FileImage, EditorTheme::FileColor::IMAGE } },
    { ".jpg",  { Icons::FileImage, EditorTheme::FileColor::IMAGE } },
    { ".jpeg", { Icons::FileImage, EditorTheme::FileColor::IMAGE } },
    { ".json", { Icons::FileCode,  EditorTheme::FileColor::JSON  } },
    { ".wav",  { Icons::FileAudio, EditorTheme::FileColor::AUDIO } },
    { ".mp3",  { Icons::FileAudio, EditorTheme::FileColor::AUDIO } },
    { ".ogg",  { Icons::FileAudio, EditorTheme::FileColor::AUDIO } },
    { ".ttf",  { Icons::File,      EditorTheme::FileColor::DEFAULT } },
    { ".otf",  { Icons::File,      EditorTheme::FileColor::DEFAULT } },
    { ".animclip", { Icons::FileCode, EditorTheme::FileColor::JSON } },
    { ".animator", { Icons::Sitemap,  EditorTheme::FileColor::JSON } },
    { ".tileset",  { Icons::Image,    EditorTheme::FileColor::IMAGE } },
};

const std::unordered_map<std::string, UIRegistry::ComponentTypeInfo> UIRegistry::s_componentTypes = {
    { "Transform",      { Icons::ArrowsAlt } },
    { "SpriteRenderer", { Icons::Image     } },
    { "BoxCollider2D",  { Icons::Square    } },
    { "CircleCollider2D", { Icons::Circle  } },
    { "Rigidbody2D",    { Icons::Cogs      } },
    { "TextRenderer2D", { Icons::ListUl    } },
    { "AudioSource",    { Icons::Music     } },
    { "AudioListener",  { Icons::VolumeUp  } },
    { "Animator2D",     { Icons::Sitemap   } },
    { "TilemapRenderer",{ Icons::Image     } },
    { "ParticleSystem", { Icons::Circle    } },
    { "Camera",         { Icons::Camera    } },
    { "UICanvas",       { Icons::Square    } },
    { "RectTransform",  { Icons::ArrowsAlt } },
    { "UIImage",        { Icons::Image     } },
    { "UILabel",        { Icons::ListUl    } },
    { "UIButton",       { Icons::Square    } },
};

const UIRegistry::FileTypeInfo& UIRegistry::GetFileTypeInfo(const std::string& extension, bool isDirectory) {
    if (isDirectory) {
        return s_folderType;
    }
    auto it = s_fileTypes.find(extension);
    if (it != s_fileTypes.end()) {
        return it->second;
    }
    return s_defaultFileType;
}

const UIRegistry::ComponentTypeInfo& UIRegistry::GetComponentInfo(const std::string& typeName) {
    auto it = s_componentTypes.find(typeName);
    if (it != s_componentTypes.end()) {
        return it->second;
    }
    // Scripts are identified by suffix or by not being a known component
    if (typeName.find("Script") != std::string::npos || typeName.find("Controller") != std::string::npos) {
        return s_scriptComponentType;
    }
    return s_defaultComponentType;
}
