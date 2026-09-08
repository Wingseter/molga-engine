#include "TextureManager.h"
#include "../Rendering/Texture.h"
#include "PathService.h"
#ifdef MOLGA_EDITOR
#include "../Editor/Project.h"
#endif
#include "Core/Profiling/ProfilerService.h"
#include "Core/Profiling/ScopedTimer.h"
#include "Common/Log.h"
#include "Core/AssetDatabase.h"
#include "Core/TextureImportSettings.h"
#include "UI/UILayoutSystem.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

TextureManager& TextureManager::Get() {
    static TextureManager instance;
    return instance;
}

Texture* TextureManager::Load(const std::string& path, const char* caller) {
    molga::TextureImportSettings settings = molga::TextureImportSettings::LegacyDefaults();
    std::string guid = molga::AssetDatabase::Get().GuidForAbsolutePath(path);
    if (guid.empty()) guid = molga::AssetDatabase::Get().GuidForSource(path);
    if (const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid)) {
        settings = molga::DeserializeTextureImportSettings(record->settings, true);
    }
    return LoadWithSettings(path, settings, caller);
}

std::string TextureManager::GuidForPath(const std::string& absolutePath,
                                        const std::string& authoredPath) {
    auto& database = molga::AssetDatabase::Get();
    std::string guid = database.GuidForAbsolutePath(absolutePath);
    if (guid.empty()) guid = database.GuidForSource(absolutePath);
    if (guid.empty()) guid = database.GuidForAbsolutePath(authoredPath);
    if (guid.empty()) guid = database.GuidForSource(authoredPath);
    return guid;
}

bool TextureManager::PublishRuntimeBinding(const std::string& textureGuid,
                                           const Texture& texture) {
    if (textureGuid.empty()) return false;
    const auto& lifetime = texture.BindingLifetime();
    // 장치가 없으면 바인딩도 없다. 그 상태로 빈 기록을 게시하면 소비자가
    // "해석되었지만 핸들이 없다"는 절반짜리 페이로드를 보게 된다.
    if (!lifetime) return false;
    return molga::ui::UITextureBindingRegistry::Get().Publish(textureGuid,
                                                              lifetime) ==
           molga::ui::UITextureBindingPublishResult::Published;
}

void TextureManager::RetireBinding(const std::string& cacheKey) {
    const auto found = guidByKey.find(cacheKey);
    if (found == guidByKey.end()) return;
    molga::ui::UITextureBindingRegistry::Get().Retire(found->second);
    guidByKey.erase(found);
}

std::string TextureManager::CacheKey(const std::string& path) {
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    if (!error) {
        fs::path canonical = fs::weakly_canonical(absolute, error);
        if (!error) return canonical.generic_string();
    }
    return fs::path(path).lexically_normal().generic_string();
}

Texture* TextureManager::LoadWithSettings(const std::string& path,
                                          const molga::TextureImportSettings& settings,
                                          const char* caller) {
    if (path.empty()) {
        return nullptr;
    }

    // Resolve path (could be relative to project or runtime exe)
    std::string absolutePath = path;
    if (!fs::path(path).is_absolute()) {
#ifdef MOLGA_EDITOR
        if (Project::Get().IsOpen()) {
            absolutePath = Project::Get().GetAbsolutePath(path);
        } else {
            absolutePath = PathService::Get().ResolveAsset(path);
        }
#else
        absolutePath = PathService::Get().ResolveAsset(path);
#endif
    }

    const std::string key = CacheKey(absolutePath);
    auto it = textures.find(key);
    if (it != textures.end()) return it->second.get();

    // Check if file exists
    if (!fs::exists(absolutePath)) {
        std::cerr << "[TextureManager] File not found: " << absolutePath << std::endl;
        return nullptr;
    }

    long long t0 = molga::NowNanos();

    // Load texture
    try {
        auto texture = std::make_unique<Texture>(absolutePath.c_str(), settings);
        if (!texture->IsValid()) return nullptr;
        Texture* ptr = texture.get();
        textures[key] = std::move(texture);
        // 이 GUID가 지금 묶여 있는 런타임 바인딩을 UI에 알린다. 이 한 줄이
        // 없으면 UILayoutSystem::Build의 sprite 분기는 프로덕션에서 절대
        // 성립하지 않는다.
        if (const std::string guid = GuidForPath(absolutePath, path);
            !guid.empty()) {
            guidByKey[key] = guid;
            PublishRuntimeBinding(guid, *ptr);
        }

        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ProfilerService::Get().AssetLoadCounter()++;

        constexpr double kSlowLoadMs = 8.0;
        if (ms > kSlowLoadMs) {
            Log::Warn("AssetLoad",
                "Slow texture load by [" + std::string(caller) + "]: " + path + " (" + std::to_string(ms) + " ms)");
        } else {
            std::cout << "[TextureManager] Loaded texture by [" << caller << "]: " << path << " (" << ms << " ms)" << std::endl;
        }
        return ptr;
    } catch (const std::exception& e) {
        std::cerr << "[TextureManager] Failed to load texture: " << path << " - " << e.what() << std::endl;
        return nullptr;
    }
}

bool TextureManager::Reload(const std::string& path,
                            const molga::TextureImportSettings& settings,
                            std::string* errorOut) {
    const std::string key = CacheKey(path);
    auto found = textures.find(key);
    if (found == textures.end()) {
        if (errorOut) errorOut->clear();
        return true;
    }
    std::string absolutePath = path;
    if (!fs::path(path).is_absolute()) absolutePath = PathService::Get().ResolveAsset(path);
    if (!found->second->Reload(absolutePath.c_str(), settings, errorOut)) {
        return false;
    }
    // 재업로드는 새 핸들과 새 수명 객체를 만든다. 다시 게시하지 않으면 UI가
    // 옛 바인딩을 계속 보게 되고, 그 핸들은 이미 은퇴한 기록의 것이다.
    if (const std::string guid = GuidForPath(absolutePath, path);
        !guid.empty()) {
        guidByKey[key] = guid;
        PublishRuntimeBinding(guid, *found->second);
    }
    return true;
}

Texture* TextureManager::Get(const std::string& path) {
    auto it = textures.find(CacheKey(path));
    if (it != textures.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool TextureManager::IsLoaded(const std::string& path) const {
    return textures.find(CacheKey(path)) != textures.end();
}

void TextureManager::Unload(const std::string& path) {
    const std::string key = CacheKey(path);
    auto it = textures.find(key);
    if (it != textures.end()) {
        // 기록을 먼저 놓는다. UI 등록부가 수명 토큰의 강한 소유자이므로,
        // 놓기 전에 Texture를 파괴하면 그 토큰이 만료되지 않아 핸들이 영원히
        // 반납되지 않는다.
        RetireBinding(key);
        textures.erase(it);
        std::cout << "[TextureManager] Unloaded texture: " << path << std::endl;
    }
}

std::size_t TextureManager::ReleaseBindings(std::uint64_t deviceGeneration) {
    if (deviceGeneration == 0U) return 0U;
    std::size_t released = 0U;
    for (auto it = textures.begin(); it != textures.end();) {
        if (!it->second) { ++it; continue; }
        const auto& lifetime = it->second->BindingLifetime();
        if (!lifetime ||
            lifetime->Identity().deviceGeneration != deviceGeneration) {
            ++it;
            continue;
        }
        // 기록을 먼저 놓는다(Unload와 같은 순서). UI 등록부가 수명 토큰의
        // 강한 소유자이므로, 놓기 전에 Texture를 파괴하면 그 토큰이 만료되지
        // 않아 핸들이 영원히 반납되지 않는다.
        RetireBinding(it->first);
        it = textures.erase(it);
        ++released;
    }
    return released;
}

void TextureManager::Clear() {
    // 이 캐시가 guid -> 바인딩의 유일한 게시자이므로, 텍스처를 전부 놓는 것과
    // 그 등록부를 비우는 것은 같은 사실이다. guidByKey를 훑는 것보다 이쪽이
    // 안전하다 — 어떤 이유로든 두 표가 어긋나면 남은 기록이 수명 토큰의 강한
    // 소유자로 남아 그 핸들이 영원히 반납되지 않는다.
    molga::ui::UITextureBindingRegistry::Get().Clear();
    guidByKey.clear();
    textures.clear();
    std::cout << "[TextureManager] Cleared all textures" << std::endl;
}
