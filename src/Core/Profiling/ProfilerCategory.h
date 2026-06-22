#pragma once

namespace molga {

// CPU 시간을 어느 서브시스템으로 귀속시킬지 분류한다(갭 분석 §7 exit scenario).
enum class ProfileCategory {
    Scripts,
    Rendering,
    Physics,
    AssetLoad,
    EditorUI,
    Other,
    Count
};

inline const char* ProfileCategoryName(ProfileCategory c) {
    switch (c) {
        case ProfileCategory::Scripts:   return "Scripts";
        case ProfileCategory::Rendering: return "Rendering";
        case ProfileCategory::Physics:   return "Physics";
        case ProfileCategory::AssetLoad: return "Asset Load";
        case ProfileCategory::EditorUI:  return "Editor UI";
        case ProfileCategory::Other:     return "Other";
        default:                         return "?";
    }
}

} // namespace molga
