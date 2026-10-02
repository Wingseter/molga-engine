#include "Rendering/Texture.h"

#include "Common/Log.h"

#include <atomic>
#include <limits>
#include <optional>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

std::atomic<std::uint64_t> nextTextureId{1};

// ── Step 5b: 프로세스 전역 0 아님 수명 정체성 ───────────────────────────────
// 감기지 않는다. UINT64_MAX 다음이 0으로 감기면 이미 발행된 적 있는 0이 새
// 자원에 붙어, 옛 스냅샷이 붙든 바인딩과 새 바인딩이 같은 정체성을 갖는다.
// 소진되면 nullopt이고, 그 자리에서 발행 자체를 포기한다.
std::atomic<std::uint64_t> nextBindingLifetimeIdentity{1};

std::optional<std::uint64_t> AcquireBindingLifetimeIdentity() noexcept {
    std::uint64_t current = nextBindingLifetimeIdentity.load();
    for (;;) {
        if (current == 0U || current == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        if (nextBindingLifetimeIdentity.compare_exchange_weak(current,
                                                              current + 1U)) {
            return current;
        }
    }
}

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
    // ── Step 5c: 은퇴는 파괴가 아니다 ──────────────────────────────────────
    // 이 텍스처의 지분만 놓는다. 그 다음 쓸어 내기는 *외부 소유자가 하나도
    // 남지 않은* 기록만 반납한다 — 아직 게시된 스냅샷이 이 바인딩을 붙들고
    // 있으면 등록부가 거절하고 핸들은 그대로 살아 있다.
    //
    // 예전처럼 여기서 DestroySampler/DestroyTexture를 직접 부르면 그 거절이
    // 아무것도 지키지 못한다: 등록부가 거절할 때쯤이면 핸들은 이미 없다.
    bindingLifetime_.reset();
    molga::TextureBindingRegistry::Get().SweepRetiredBindings();
    sampler_ = {};
    texture_ = {};
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

    // ── Step 5b: 두 세대를 전부 취득한 뒤에만 바인딩을 바꾼다 ──────────────
    // 하나라도 소진되면 새 자원을 파괴하고 옛 바인딩을 그대로 둔다. 절반만
    // 게시된 바인딩은 스냅샷이 죽은 핸들을 붙들게 하는 정확한 경로다.
    //
    // 집계 축 Advance(TextureBinding)은 여기서 취득하지 않는다. 그 축은
    // "게시된 guid→바인딩 기록이 달라졌다"를 말하는 캐시 무효화 축이고, 그
    // 기록을 바꾸는 곳은 UITextureBindingRegistry::Publish 하나뿐이다. 여기서
    // 함께 올리면 스칼라는 움직였는데 그것이 가리키는 벡터는 그대로인 프레임이
    // 생기고, 소진 시에는 이 텍스처와 무관한 프로세스 전역 조건 때문에 GPU를
    // 통째로 비우고 이미 끝난 업로드를 버리게 된다.
    const bool uploadGenerationExhausted =
        uploadGeneration_ == std::numeric_limits<std::uint64_t>::max();
    const auto lifetimeIdentity =
        uploadGenerationExhausted ? std::nullopt
                                  : AcquireBindingLifetimeIdentity();
    if (!lifetimeIdentity) {
        // 아직 아무 스냅샷도 이 핸들을 붙들지 않았지만, 방금 그 위로 업로드를
        // 제출했다. GPU가 그 복사를 끝내기 전에 파괴하면 드라이버가 죽은
        // 메모리를 읽는다.
        device->WaitIdle(nullptr);
        device->DestroySampler(newSampler);
        device->DestroyTexture(newTexture);
        const char* which =
            uploadGenerationExhausted ? "per-texture upload" : "binding lifetime";
        Log::Error("Texture",
                   std::string("the ") + which +
                       " generation sequence is exhausted; the new upload was "
                       "discarded, the previous binding is retained, and UI "
                       "snapshot caching is disabled");
        if (errorOut) {
            *errorOut = std::string("texture binding generation exhausted (") +
                        which + ")";
        }
        return false;
    }

    // Last-good swap: the old allocation remains intact until every new
    // resource and upload has succeeded.
    //
    // 옛 핸들을 여기서 파괴하지 않는다 — 그 핸들의 주인은 등록부의 옛 기록이고,
    // 이미 게시된 스냅샷이 그 기록의 토큰을 들고 있을 수 있다. 아래에서 새
    // 토큰을 받은 뒤 한 번 쓸어 내면, 외부 소유자가 없을 때만 반납된다.
    texture_ = newTexture;
    sampler_ = newSampler;
    ++uploadGeneration_;
    molga::ui::TextureRuntimeBindingIdentity binding;
    binding.deviceGeneration = device->Generation();
    binding.uploadGeneration = uploadGeneration_;
    binding.texture = texture_;
    binding.sampler = sampler_;
    binding.lifetimeIdentity = *lifetimeIdentity;
    // 새 수명 객체다. 옛 객체를 갱신하면 이미 게시된 스냅샷이 자기 몰래 새
    // 핸들을 가리키게 되고, 그 프레임의 렌더는 아직 올라가지 않은 픽셀을
    // 읽는다.
    bindingLifetime_ = molga::TextureBindingRegistry::Get().Publish(binding);
    // 옛 바인딩은 방금 은퇴했다. 외부 소유자가 하나도 없으면 여기서 반납되고,
    // 하나라도 있으면 그 소유자가 사라질 때까지 등록부가 붙들고 있는다.
    molga::TextureBindingRegistry::Get().SweepRetiredBindings();
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
