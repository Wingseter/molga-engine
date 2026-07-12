#include "doctest.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Material.h"
#include "ECS/Components/SpriteRenderer.h"
#include "Core/Bootstrap.h"
#include "Core/PathService.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
// Helper to write file content
void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path);
    ofs << content;
}
}

TEST_CASE("Shader and Material subsystem test") {
    // 1. Initialize Headless Engine Context
    WindowConfig config;
    config.visible = false;
    GLFWwindow* window = EngineInit(config);
    REQUIRE(window != nullptr);

    // Prepare temporary files
    std::string vertPath = "test_temp_shader.vert";
    std::string fragPath = "test_temp_shader.frag";

    std::string vertSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoords;
        out vec2 TexCoords;
        uniform mat4 projection;
        uniform mat4 model;
        void main() {
            TexCoords = aTexCoords;
            gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
        }
    )";

    std::string fragSrc = R"(
        #version 330 core
        out vec4 FragColor;
        in vec2 TexCoords;
        uniform vec4 uColor;
        void main() {
            FragColor = uColor;
        }
    )";

    WriteFile(vertPath, vertSrc);
    WriteFile(fragPath, fragSrc);

    SUBCASE("ShaderManager caching and loading") {
        Shader* shader1 = ShaderManager::Get().Load("test_shader", vertPath, fragPath);
        REQUIRE(shader1 != nullptr);

        Shader* shader2 = ShaderManager::Get().Get("test_shader");
        CHECK(shader1 == shader2);

        Shader* shader3 = ShaderManager::Get().Load("test_shader", vertPath, fragPath);
        CHECK(shader1 == shader3);

        // Get non-existent
        CHECK(ShaderManager::Get().Get("non_existent") == nullptr);
    }

    SUBCASE("Shader Reload - Success and Failure Protection") {
        Shader* shader = ShaderManager::Get().Load("test_shader", vertPath, fragPath);
        REQUIRE(shader != nullptr);

        unsigned int originalProgramID = shader->GetID();

        // 1. Test success reload (no code change)
        bool reloadSuccess = shader->Reload();
        CHECK(reloadSuccess == true);
        unsigned int reloadedProgramID = shader->GetID();
        CHECK(reloadedProgramID != originalProgramID); // Program ID should change

        // 2. Test compilation failure
        std::string brokenFragSrc = R"(
            #version 330 core
            out vec4 FragColor;
            void main() {
                FragColor = vec4(1.0) + syntax_error_here;
            }
        )";
        WriteFile(fragPath, brokenFragSrc);

        bool reloadFailed = shader->Reload();
        CHECK(reloadFailed == false);
        // Program ID should remain the same as the working one
        CHECK(shader->GetID() == reloadedProgramID);

        // 3. Test recovery from failure
        WriteFile(fragPath, fragSrc);
        bool reloadRecovered = shader->Reload();
        CHECK(reloadRecovered == true);
        CHECK(shader->GetID() != reloadedProgramID);
    }

    SUBCASE("Material Properties and Serialization") {
        Material material;
        material.shaderName = "test_shader";
        material.tint = Color(0.5f, 0.6f, 0.7f, 0.8f);
        material.blendMode = BlendMode::Additive;

        MaterialProperty prop1;
        prop1.type = MaterialProperty::Type::Float;
        prop1.floatVal = 42.0f;
        material.properties["uMyFloat"] = prop1;

        MaterialProperty prop2;
        prop2.type = MaterialProperty::Type::Vec4;
        prop2.vec4Val = Vector4(1.0f, 2.0f, 3.0f, 4.0f);
        material.properties["uMyVec4"] = prop2;

        MaterialProperty prop3;
        prop3.type = MaterialProperty::Type::Texture;
        prop3.texturePath = "dummy_texture.png";
        material.properties["uMyTexture"] = prop3;

        // Serialization
        nlohmann::json jsonVal;
        material.Serialize(jsonVal);

        CHECK(jsonVal["shaderName"] == "test_shader");
        CHECK(jsonVal["tint"][0] == 0.5f);
        CHECK(jsonVal["blendMode"] == static_cast<int>(BlendMode::Additive));
        CHECK(jsonVal["properties"]["uMyFloat"]["type"] == static_cast<int>(MaterialProperty::Type::Float));
        CHECK(jsonVal["properties"]["uMyFloat"]["floatVal"] == 42.0f);
        CHECK(jsonVal["properties"]["uMyVec4"]["vec4Val"][0] == 1.0f);
        CHECK(jsonVal["properties"]["uMyTexture"]["texturePath"] == "dummy_texture.png");

        // Deserialization
        Material material2;
        material2.Deserialize(jsonVal);

        CHECK(material2.shaderName == "test_shader");
        CHECK(material2.tint == Color(0.5f, 0.6f, 0.7f, 0.8f));
        CHECK(material2.blendMode == BlendMode::Additive);
        REQUIRE(material2.properties.find("uMyFloat") != material2.properties.end());
        CHECK(material2.properties["uMyFloat"].floatVal == 42.0f);
        REQUIRE(material2.properties.find("uMyVec4") != material2.properties.end());
        CHECK(material2.properties["uMyVec4"].vec4Val == Vector4(1.0f, 2.0f, 3.0f, 4.0f));
        REQUIRE(material2.properties.find("uMyTexture") != material2.properties.end());
        CHECK(material2.properties["uMyTexture"].texturePath == "dummy_texture.png");
    }

    SUBCASE("Default material batch key uses the sprite batch shader") {
        std::string batchVertPath = "test_temp_batch.vert";
        std::string batchFragPath = "test_temp_batch.frag";

        std::string batchVertSrc = R"(
            #version 330 core
            layout (location = 0) in vec2 aPos;
            layout (location = 1) in vec2 aTexCoord;
            layout (location = 2) in vec4 aColor;
            out vec2 TexCoord;
            out vec4 Color;
            uniform mat4 projection;
            void main() {
                TexCoord = aTexCoord;
                Color = aColor;
                gl_Position = projection * vec4(aPos, 0.0, 1.0);
            }
        )";

        std::string batchFragSrc = R"(
            #version 330 core
            in vec2 TexCoord;
            in vec4 Color;
            out vec4 FragColor;
            void main() {
                FragColor = Color;
            }
        )";

        WriteFile(batchVertPath, batchVertSrc);
        WriteFile(batchFragPath, batchFragSrc);

        Shader* defaultShader = ShaderManager::Get().Load("default", vertPath, fragPath);
        Shader* batchShader = ShaderManager::Get().Load("batch", batchVertPath, batchFragPath);
        REQUIRE(defaultShader != nullptr);
        REQUIRE(batchShader != nullptr);

        Material defaultMaterial;
        auto defaultKey = defaultMaterial.GetBatchKey();
        CHECK(defaultKey.isBatchable);
        CHECK(defaultKey.shader == batchShader);
        CHECK(defaultKey.shader != defaultShader);

        Material customMaterial;
        customMaterial.shaderName = "test_shader";
        Shader* customShader = ShaderManager::Get().Load("test_shader", vertPath, fragPath);
        REQUIRE(customShader != nullptr);
        auto customKey = customMaterial.GetBatchKey();
        CHECK_FALSE(customKey.isBatchable);
        CHECK(customKey.shader == customShader);

        Material propertyMaterial;
        propertyMaterial.properties["uValue"] = MaterialProperty{};
        auto propertyKey = propertyMaterial.GetBatchKey();
        CHECK_FALSE(propertyKey.isBatchable);
        CHECK(propertyKey.shader == defaultShader);

        fs::remove(batchVertPath);
        fs::remove(batchFragPath);
    }

    SUBCASE("SpriteRenderer Integration") {
        SpriteRenderer renderer;
        renderer.material.shaderName = "test_shader";
        renderer.material.tint = Color(0.1f, 0.2f, 0.3f, 0.4f);

        nlohmann::json srJson;
        renderer.Serialize(srJson);

        CHECK(srJson.contains("material"));
        CHECK(srJson["material"]["shaderName"] == "test_shader");
        CHECK(srJson["material"]["tint"][0] == 0.1f);

        SpriteRenderer renderer2;
        renderer2.Deserialize(srJson);

        CHECK(renderer2.material.shaderName == "test_shader");
        CHECK(renderer2.material.tint == Color(0.1f, 0.2f, 0.3f, 0.4f));
    }

    // Cleanup files
    fs::remove(vertPath);
    fs::remove(fragPath);

    // Shutdown
    ShaderManager::Get().Shutdown();
    EngineShutdown();
}
