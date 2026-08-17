#include "ECS/Components/SpriteRenderer.h"
#include "Rendering/Material.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

namespace {

fs::path TemporaryBundle() {
    const fs::path root = fs::temp_directory_path() /
        ("molga-shader-bundle-" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()));
    fs::copy(fs::path(MOLGA_TEST_SHADER_BUNDLE), root,
             fs::copy_options::recursive);
    return root;
}

} // namespace

TEST_CASE("validated shader bundle publishes immutable cached entries") {
    ShaderManager::Get().Shutdown();
    std::string error;
    REQUIRE(ShaderManager::Get().Initialize(
        fs::path(MOLGA_TEST_SHADER_BUNDLE), &error, true));
    Shader* first = ShaderManager::Get().Load("batch");
    REQUIRE(first);
    CHECK(first == ShaderManager::Get().Get("batch"));
    CHECK(first->IsValid());
    CHECK(first->VertexStride() == sizeof(molga::Vertex2D));
    CHECK(ShaderManager::Get().Get("does-not-exist") == nullptr);
    ShaderManager::Get().Shutdown();
}

TEST_CASE("shader reload rejects a tampered candidate and keeps last-good") {
    const fs::path bundle = TemporaryBundle();
    ShaderManager::Get().Shutdown();
    std::string error;
    REQUIRE(ShaderManager::Get().Initialize(bundle, &error, true));
    Shader* shader = ShaderManager::Get().Get("batch");
    REQUIRE(shader);
    const std::uint64_t revision = shader->Revision();
    std::ofstream(bundle / "artifacts/batch.fragment.msl",
                  std::ios::binary | std::ios::app)
        << "tamper";
    CHECK_FALSE(ShaderManager::Get().ReloadAll(&error));
    CHECK(error.find("SHA-256 mismatch") != std::string::npos);
    CHECK(ShaderManager::Get().Get("batch") == shader);
    CHECK(shader->Revision() == revision);
    ShaderManager::Get().Shutdown();
    fs::remove_all(bundle);
}

TEST_CASE("material schema and stable shader identity are preserved") {
    ShaderManager::Get().Shutdown();
    std::string error;
    REQUIRE(ShaderManager::Get().Initialize(
        fs::path(MOLGA_TEST_SHADER_BUNDLE), &error, true));

    Material material;
    material.shaderName = "default";
    material.tint = Color(0.5f, 0.6f, 0.7f, 0.8f);
    material.blendMode = BlendMode::Screen;
    MaterialProperty property;
    property.type = MaterialProperty::Type::Float;
    property.floatVal = 42.0f;
    material.properties["gain"] = property;

    nlohmann::json json;
    material.Serialize(json);
    Material restored;
    restored.Deserialize(json);
    CHECK(restored.shaderName == "default");
    CHECK(restored.tint == material.tint);
    CHECK(restored.blendMode == BlendMode::Screen);
    CHECK(restored.properties.at("gain").floatVal == doctest::Approx(42.0f));

    Material defaultMaterial;
    const molga::BatchKey key = defaultMaterial.GetBatchKey();
    REQUIRE(key.isBatchable);
    CHECK(key.shaderName == "batch");
    CHECK(key.shaderRevision == ShaderManager::Get().Get("batch")->Revision());

    SpriteRenderer component;
    component.material = restored;
    nlohmann::json componentJson;
    component.Serialize(componentJson);
    SpriteRenderer roundTrip;
    roundTrip.Deserialize(componentJson);
    CHECK(roundTrip.material.shaderName == "default");
    ShaderManager::Get().Shutdown();
}
