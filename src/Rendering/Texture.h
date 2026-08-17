#pragma once

#include "Core/TextureImportSettings.h"
#include "Rendering/GraphicsDevice.h"

#include <cstdint>
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

private:
    bool CreateFromData(int width, int height, const unsigned char* data,
                        int channels,
                        const molga::TextureImportSettings& settings,
                        std::string* errorOut = nullptr);
    void Release();

    molga::TextureHandle texture_;
    molga::SamplerHandle sampler_;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::uint64_t stableId_ = 0;
    molga::TextureImportSettings settings_ =
        molga::TextureImportSettings::LegacyDefaults();
};
