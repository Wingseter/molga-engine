#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Core/TextureImportSettings.h"

class Texture;

class TextureManager {
public:
    static TextureManager& Get();

    // Load texture (cached)
    Texture* Load(const std::string& path, const char* caller = "Unknown");
    Texture* LoadWithSettings(const std::string& path,
                              const molga::TextureImportSettings& settings,
                              const char* caller = "Unknown");
    bool Reload(const std::string& path, const molga::TextureImportSettings& settings,
                std::string* errorOut = nullptr);

    // Get already loaded texture
    Texture* Get(const std::string& path);

    // Check if texture is loaded
    bool IsLoaded(const std::string& path) const;

    // Unload specific texture
    void Unload(const std::string& path);

    // Unload all textures
    void Clear();

    // Get texture count
    size_t GetCount() const { return textures.size(); }

private:
    TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // 캐시 키가 그 파일의 저작 GUID. 언로드할 때 어떤 GUID의 바인딩 기록을
    // 놓아야 하는지 알아야 한다 — 놓지 않으면 UI 등록부가 수명 토큰의 강한
    // 소유자로 남아 그 핸들이 영원히 반납되지 않는다.
    std::unordered_map<std::string, std::string> guidByKey;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    static std::string CacheKey(const std::string& path);
    // 절대 경로 하나의 저작 GUID. 없으면 빈 문자열.
    static std::string GuidForPath(const std::string& absolutePath,
                                   const std::string& authoredPath);
    // ── Task 11.1 Step 5c: 저작 GUID와 런타임 바인딩을 잇는 유일한 자리 ─────
    // Texture는 GUID를 모르고(경로로만 만들어진다), AssetDatabase는 GPU 핸들을
    // 모른다(헤드리스 import 시점에는 장치가 없다). 둘을 함께 아는 곳은 이
    // 캐시뿐이므로 guid → 바인딩 게시는 여기서 일어난다. 이것이 없으면
    // UILayoutSystem::Build의 소비자는 프로덕션에서 언제나 빈 손이고, 모든
    // UIImage가 missing-texture 페이로드로 게시된다.
    //
    // 성공적으로 게시했으면 참. 장치가 없어 바인딩이 아직 없거나 GUID를
    // 해석하지 못하면 거짓이다.
    bool PublishRuntimeBinding(const std::string& textureGuid,
                               const Texture& texture);
    void RetireBinding(const std::string& cacheKey);
};
