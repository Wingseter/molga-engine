#pragma once

#include "Core/TextureImportSettings.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/TextureBindingRegistry.h"

#include <cstdint>
#include <memory>
#include <string>

class Texture {
public:
    explicit Texture(const char* imagePath);
    Texture(const char* imagePath, const molga::TextureImportSettings& settings);
    Texture(int width, int height, unsigned char* data, int channels = 4);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Updates a rectangular region without replacing the Texture object.
    // Dynamic font atlases rely on this pointer identity remaining stable.
    bool UpdateSubData(int x, int y, int updateWidth, int updateHeight,
                       const unsigned char* data, int updateChannels = 4);
    bool Reload(const char* imagePath,
                const molga::TextureImportSettings& settings,
                std::string* errorOut = nullptr);

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    bool IsValid() const;
    const molga::TextureImportSettings& GetImportSettings() const {
        return settings_;
    }
    molga::TextureHandle Handle() const { return texture_; }
    molga::SamplerHandle Sampler() const { return sampler_; }
    std::uint64_t StableId() const { return stableId_; }

    // ── Step 5c: 스냅샷이 붙들 수 있는 불변 런타임 바인딩 ───────────────────
    // 새 핸들과 새 수명 객체를 먼저 만든 뒤에야 현재 바인딩이 원자적으로
    // 교체된다. 이미 게시된 스냅샷이 붙든 수명 객체는 갱신되지도 파괴되지도
    // 않는다 — 그 스냅샷은 자기가 게시될 때의 핸들을 계속 본다.
    //
    // 이 포인터를 놓는 것은 그저 은퇴시키는 것이다. 등록부는 마지막 스냅샷/
    // 명령 소유자와 GPU fence가 끝난 뒤에만 핸들을 실제로 놓는다.
    const std::shared_ptr<const molga::TextureBindingLifetime>&
    BindingLifetime() const noexcept {
        return bindingLifetime_;
    }
    // 이 텍스처가 지금까지 성공적으로 올린 횟수. 검증된 값이며 감기지 않는다.
    std::uint64_t UploadGeneration() const noexcept { return uploadGeneration_; }

private:
    bool CreateFromData(int width, int height, const unsigned char* data,
                        int channels,
                        const molga::TextureImportSettings& settings,
                        std::string* errorOut = nullptr);
    void Release();

    molga::TextureHandle texture_;
    molga::SamplerHandle sampler_;
    std::shared_ptr<const molga::TextureBindingLifetime> bindingLifetime_;
    std::uint64_t uploadGeneration_ = 0;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::uint64_t stableId_ = 0;
    molga::TextureImportSettings settings_ =
        molga::TextureImportSettings::LegacyDefaults();
};
