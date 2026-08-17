#include "Rendering/Texture.h"

#include <atomic>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

std::atomic<std::uint64_t> nextTextureId{1};

std::vector<std::uint8_t> ToRgba(const unsigned char* data, int width,
                                 int height, int channels) {
    if (!data || width <= 0 || height <= 0 ||
        (channels != 1 && channels != 3 && channels != 4)) {
        return {};
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> output(pixelCount * 4U);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const unsigned char* source = data + index * static_cast<std::size_t>(channels);
        std::uint8_t* destination = output.data() + index * 4U;
        if (channels == 1) {
            destination[0] = 255;
            destination[1] = 255;
            destination[2] = 255;
            destination[3] = source[0];
        } else {
            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2];
            destination[3] = channels == 4 ? source[3] : 255;
        }
    }
    return output;
}

molga::TextureAddressMode ToAddress(molga::TextureWrapMode mode) {
    switch (mode) {
        case molga::TextureWrapMode::Repeat:
            return molga::TextureAddressMode::Repeat;
        case molga::TextureWrapMode::MirroredRepeat:
            return molga::TextureAddressMode::MirroredRepeat;
        case molga::TextureWrapMode::Clamp:
            return molga::TextureAddressMode::ClampToEdge;
    }
    return molga::TextureAddressMode::ClampToEdge;
}

} // namespace

Texture::Texture(const char* imagePath)
    : Texture(imagePath, molga::TextureImportSettings::LegacyDefaults()) {}

Texture::Texture(const char* imagePath,
                 const molga::TextureImportSettings& settings)
    : stableId_(nextTextureId.fetch_add(1)) {
    Reload(imagePath, settings, nullptr);
}

Texture::Texture(int width, int height, unsigned char* data, int channels)
    : stableId_(nextTextureId.fetch_add(1)) {
    CreateFromData(width, height, data, channels,
                   molga::TextureImportSettings::LegacyDefaults(), nullptr);
}

Texture::~Texture() { Release(); }

void Texture::Release() {
    if (molga::GraphicsDevice* device = molga::GraphicsDevice::Current()) {
        device->DestroySampler(sampler_);
        device->DestroyTexture(texture_);
    } else {
        sampler_ = {};
        texture_ = {};
    }
    width_ = 0;
    height_ = 0;
    channels_ = 0;
}

bool Texture::IsValid() const {
    molga::GraphicsDevice* device = molga::GraphicsDevice::Current();
    return device && width_ > 0 && height_ > 0 &&
           device->IsAlive(texture_) && device->IsAlive(sampler_);
}

bool Texture::Reload(const char* imagePath,
                     const molga::TextureImportSettings& settings,
                     std::string* errorOut) {
    if (!imagePath || !*imagePath) {
        if (errorOut) *errorOut = "texture path is empty";
        return false;
    }
    // Public texture coordinates and decoded image rows are both top-left.
    stbi_set_flip_vertically_on_load(false);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded = stbi_load(imagePath, &width, &height, &channels, 0);
    if (!decoded) {
        if (errorOut) {
            *errorOut = std::string("stbi_load failed: ") +
                (stbi_failure_reason() ? stbi_failure_reason() : imagePath);
        }
        return false;
    }
    const bool success = CreateFromData(width, height, decoded, channels,
                                        settings, errorOut);
    stbi_image_free(decoded);
    return success;
}

bool Texture::CreateFromData(int width, int height, const unsigned char* data,
                             int channels,
                             const molga::TextureImportSettings& settings,
                             std::string* errorOut) {
    molga::GraphicsDevice* device = molga::GraphicsDevice::Current();
    if (!device) {
        if (errorOut) *errorOut = "texture creation requires an active graphics device";
        return false;
    }
    std::vector<std::uint8_t> rgba = ToRgba(data, width, height, channels);
    if (rgba.empty()) {
        if (errorOut) *errorOut = "invalid decoded texture data";
        return false;
    }

    molga::TextureDescriptor textureDescriptor;
    textureDescriptor.width = static_cast<std::uint32_t>(width);
    textureDescriptor.height = static_cast<std::uint32_t>(height);
    textureDescriptor.format =
        settings.usage == molga::TextureUsage::Color &&
                settings.colorSpace == molga::TextureColorSpace::SRGB
            ? molga::TextureFormat::SRGBA8
            : molga::TextureFormat::RGBA8;
    textureDescriptor.usage = molga::GpuTextureUsage::Sampler;
    textureDescriptor.debugName = "Texture";

    molga::SamplerDescriptor samplerDescriptor;
    samplerDescriptor.minFilter =
        settings.filter == molga::TextureFilterMode::Nearest
            ? molga::TextureFilter::Nearest
            : molga::TextureFilter::Linear;
    samplerDescriptor.magFilter = samplerDescriptor.minFilter;
    samplerDescriptor.addressU = ToAddress(settings.wrapU);
    samplerDescriptor.addressV = ToAddress(settings.wrapV);
    samplerDescriptor.debugName = "TextureSampler";

    std::string error;
    molga::TextureHandle newTexture = device->CreateTexture(textureDescriptor, error);
    if (!newTexture) {
        if (errorOut) *errorOut = error;
        return false;
    }
    molga::SamplerHandle newSampler = device->CreateSampler(samplerDescriptor, error);
    if (!newSampler) {
        device->DestroyTexture(newTexture);
        if (errorOut) *errorOut = error;
        return false;
    }
    if (!device->UploadTextureImmediate(
            {newTexture, 0, 0},
            {0, 0, static_cast<std::uint32_t>(width),
             static_cast<std::uint32_t>(height)},
            rgba.data(), rgba.size(), static_cast<std::uint32_t>(width * 4),
            error)) {
        device->DestroySampler(newSampler);
        device->DestroyTexture(newTexture);
        if (errorOut) *errorOut = error;
        return false;
    }

    // Last-good swap: the old allocation remains intact until every new
    // resource and upload has succeeded.
    device->DestroySampler(sampler_);
    device->DestroyTexture(texture_);
    texture_ = newTexture;
    sampler_ = newSampler;
    width_ = width;
    height_ = height;
    channels_ = channels;
    settings_ = settings;
    if (errorOut) errorOut->clear();
    return true;
}

bool Texture::UpdateSubData(int x, int y, int updateWidth, int updateHeight,
                            const unsigned char* data, int updateChannels) {
    if (!IsValid() || x < 0 || y < 0 || updateWidth <= 0 || updateHeight <= 0 ||
        x + updateWidth > width_ || y + updateHeight > height_) {
        return false;
    }
    std::vector<std::uint8_t> rgba =
        ToRgba(data, updateWidth, updateHeight, updateChannels);
    if (rgba.empty()) return false;
    std::string error;
    return molga::GraphicsDevice::Current()->UploadTextureImmediate(
        {texture_, 0, 0},
        {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
         static_cast<std::uint32_t>(updateWidth),
         static_cast<std::uint32_t>(updateHeight)},
        rgba.data(), rgba.size(), static_cast<std::uint32_t>(updateWidth * 4),
        error);
}
