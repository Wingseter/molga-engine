#include "Core/Bootstrap.h"
#include "AssetDatabaseTestAuthority.h"
#include "Core/AssetDatabase.h"
#include "Core/TextureManager.h"
#include "Rendering/Texture.h"
#include "UI/UILayoutSystem.h"
#include "doctest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("SDL_GPU creates a native device and submits a swapchain pass") {
    WindowConfig config;
    config.title = "Molga SDL_GPU platform contract";
    config.width = 160;
    config.height = 90;
    // Vulkan deliberately returns no swapchain texture for a hidden window.
    // The capability gate runs inside Xvfb on Linux, so keep this tiny window
    // drawable in order to exercise an actual acquire, submit, and present.
    config.visible = true;
    config.graphicsBackend = molga::GraphicsBackend::SdlGpu;
    config.graphicsValidation = true;

    auto host = EngineInit(config);
    REQUIRE(host);
    const molga::GraphicsDeviceInfo& info = host->GraphicsInfo();
    CHECK(info.backend == molga::GraphicsBackend::SdlGpu);
    CHECK_FALSE(info.driver.empty());
    CHECK(info.validationEnabled);
    CHECK(info.capabilityPipelineReady);
#if defined(__APPLE__)
    CHECK(info.driver == "metal");
    CHECK(info.supportsMsl);
#elif defined(_WIN32)
    CHECK(info.driver == "direct3d12");
    CHECK((info.supportsDxbc || info.supportsDxil));
#else
    CHECK(info.driver == "vulkan");
    CHECK(info.supportsSpirv);
#endif
    REQUIRE(host->RenderCapabilityFrame(0.04f, 0.08f, 0.16f, 1.0f));
    std::string waitError;
    REQUIRE(host->Graphics().WaitIdle(&waitError));
    CHECK(host->Graphics().ValidationErrorCount() == 0U);
}

// ── Task 11.1 F2: guid → 런타임 바인딩의 생산자는 프로덕션에 있어야 한다 ─────
// UILayoutSystem::Build의 sprite 분기는 UITextureBindingRegistry에서 GUID로
// 바인딩을 찾는다. 그 등록부에 게시하는 프로덕션 코드가 없으면 모든 UIImage가
// `content && bound` 가드를 통과하지 못하고 missing-texture 페이로드로 게시된다
// — 게임 안의 모든 이미지가 빈 상자가 된다.
//
// 이 케이스가 GPU 라벨 아래 있는 이유는 하나뿐이다: 바인딩은 살아 있는 장치가
// 만든 핸들이다. 헤드리스에서는 Texture가 아무것도 담지 못하므로 그 이음매를
// 시험할 수 없고, 그래서 이 결함이 두 검토 렌즈에 걸릴 때까지 남아 있었다.
namespace {

// 32bpp 무압축 TGA 하나. stb_image가 읽을 수 있는 가장 작은 진짜 이미지이고,
// PNG 픽스처를 저장소에 새로 넣지 않아도 된다.
void WriteSolidTga(const std::filesystem::path& path, int width, int height) {
    std::vector<std::uint8_t> bytes;
    const std::uint8_t header[18] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        static_cast<std::uint8_t>(width & 0xFF),
        static_cast<std::uint8_t>((width >> 8) & 0xFF),
        static_cast<std::uint8_t>(height & 0xFF),
        static_cast<std::uint8_t>((height >> 8) & 0xFF),
        32,
        0x28 /* 위에서 아래로, alpha 8bit */};
    bytes.insert(bytes.end(), std::begin(header), std::end(header));
    for (int index = 0; index < width * height; ++index) {
        bytes.push_back(0x40);  // B
        bytes.push_back(0x80);  // G
        bytes.push_back(0xC0);  // R
        bytes.push_back(0xFF);  // A
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("TextureManager joins the authored GUID to the live runtime binding") {
    namespace fs = std::filesystem;

    WindowConfig config;
    config.title = "Molga texture binding producer";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsBackend = molga::GraphicsBackend::SdlGpu;
    auto host = EngineInit(config);
    REQUIRE(host);

    auto& authority = test_support::AssetDatabaseTestAuthority::Get();
    std::string bindError;
    REQUIRE_MESSAGE(authority.Bind(molga::AssetDatabase::Get(), &bindError),
                    bindError);
    const fs::path assets = authority.AssetsCaseRoot("texture-binding-producer");
    const fs::path source = assets / "probe.tga";
    WriteSolidTga(source, 2, 2);
    molga::AssetDatabase::Get().ScanProject(assets);
    const std::string guid =
        molga::AssetDatabase::Get().GuidForAbsolutePath(source);
    REQUIRE(guid.size() == 32);

    // 시작점을 못 박는다. 이 확인이 없으면 아래 성공이 다른 케이스가 남긴
    // 기록 위에서 공짜로 참이 될 수 있다.
    molga::ui::UITextureBindingRegistry::Get().Retire(guid);
    REQUIRE_FALSE(
        molga::ui::UITextureBindingRegistry::Get().Find(guid).has_value());

    Texture* texture = TextureManager::Get().Load(source.string(), "gpu-test");
    REQUIRE(texture != nullptr);
    REQUIRE(texture->IsValid());
    REQUIRE(texture->BindingLifetime() != nullptr);

    // 이 사본은 스냅샷이 하는 일을 그대로 한다: 게시된 수명 토큰의 지분을
    // 하나 든다. 아래에서 그 지분이 실제로 핸들을 지키는지 확인한다.
    auto bound = molga::ui::UITextureBindingRegistry::Get().Find(guid);
    REQUIRE(bound.has_value());
    // 같은 토큰이어야 한다. 값만 같은 두 번째 토큰을 게시하면 Texture가 자기
    // 지분을 놓아도 등록부의 지분 때문에 핸들이 영원히 반납되지 않는다.
    CHECK(bound->lifetime == texture->BindingLifetime());
    CHECK(bound->binding == texture->BindingLifetime()->Identity());
    CHECK(bound->binding.texture == texture->Handle());
    CHECK(bound->binding.sampler == texture->Sampler());
    CHECK(bound->binding.deviceGeneration == host->Graphics().Generation());
    CHECK(bound->binding.uploadGeneration == texture->UploadGeneration());
    CHECK(bound->binding.lifetimeIdentity != 0U);

    const molga::TextureHandle handle = texture->Handle();
    const molga::SamplerHandle sampler = texture->Sampler();
    const std::uint64_t releasedBefore =
        molga::TextureBindingRegistry::Get().ReleasedBindingCount();

    // ── Task 11.1 F4: Texture는 살아 있는 스냅샷의 핸들을 파괴하지 않는다 ───
    // 언로드는 UI 기록을 놓고 Texture를 파괴한다. 그것은 *은퇴*일 뿐이다:
    // 위에서 뜬 사본이 아직 수명 토큰을 들고 있으므로 등록부가 반납을
    // 거절해야 하고, 핸들은 그대로 살아 있어야 한다. 예전처럼 Texture가
    // DestroySampler/DestroyTexture를 직접 부르면 그 거절이 아무것도 지키지
    // 못한다 — 등록부가 거절할 때쯤이면 핸들은 이미 없다.
    TextureManager::Get().Unload(source.string());
    CHECK_FALSE(
        molga::ui::UITextureBindingRegistry::Get().Find(guid).has_value());
    CHECK(host->Graphics().IsAlive(handle));
    CHECK(host->Graphics().IsAlive(sampler));
    CHECK(molga::TextureBindingRegistry::Get().ReleasedBindingCount() ==
          releasedBefore);

    // 마지막 외부 소유자가 사라지면 그때 등록부가 반납한다.
    bound.reset();
    CHECK(molga::TextureBindingRegistry::Get().SweepRetiredBindings() >= 1U);
    CHECK(molga::TextureBindingRegistry::Get().ReleasedBindingCount() >
          releasedBefore);
    CHECK_FALSE(host->Graphics().IsAlive(handle));
    CHECK_FALSE(host->Graphics().IsAlive(sampler));

    TextureManager::Get().Clear();
    fs::remove_all(assets);
}
