#pragma once

#include "Common/Sha256.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace test_support {

inline std::string WriteMinimalMslShaderBundle(
    const std::filesystem::path& packageRoot) {
    namespace fs = std::filesystem;
    const fs::path bundle = packageRoot / "ShaderBundle";
    const std::string vertexSource =
        "#include <metal_stdlib>\nusing namespace metal;\n";
    const std::string fragmentSource = vertexSource;
    fs::create_directories(bundle / "artifacts");
    {
        std::ofstream(bundle / "artifacts/test.vertex.msl", std::ios::binary)
            << vertexSource;
        std::ofstream(bundle / "artifacts/test.fragment.msl", std::ios::binary)
            << fragmentSource;
    }

    const auto resources = nlohmann::json{
        {"samplers", 0U}, {"storageTextures", 0U},
        {"storageBuffers", 0U}, {"uniformBuffers", 0U}};
    const auto stage = [&](const char* name, const char* path,
                           const std::string& source) {
        const bool vertex = std::string(name) == "vertex";
        return nlohmann::json{
            {"stage", name},
            {"entryPoint", vertex ? "VSMain" : "PSMain"},
            {"resources", resources},
            {"artifacts", {{"msl", {
                {"path", path},
                {"sha256", molga::Sha256String(source)},
                {"entryPoint", vertex ? "VSMain" : "PSMain"},
            }}}},
        };
    };
    nlohmann::json manifest = {
        {"schemaVersion", 1},
        {"artifactFormats", {"msl"}},
        {"tool", {{"name", "test"}, {"revision", "test-revision"}}},
        {"entries", {{
            {"name", "test"},
            {"source", "test.hlsl"},
            {"sourceSha256", molga::Sha256String("source")},
            {"descriptorSha256", molga::Sha256String("descriptor")},
            {"bindingsSha256", molga::Sha256String("bindings")},
            {"revision", std::uint64_t{1}},
            {"vertexLayout", {
                {"stride", 8U},
                {"attributes", {{{"location", 0U},
                                  {"format", "Float2"},
                                  {"offset", 0U}}}},
            }},
            {"parameterBlockSize", 0U},
            {"parameters", nlohmann::json::array()},
            {"textures", nlohmann::json::array()},
            {"vertex", stage("vertex", "artifacts/test.vertex.msl",
                              vertexSource)},
            {"fragment", stage("fragment", "artifacts/test.fragment.msl",
                                fragmentSource)},
        }}},
    };
    {
        std::ofstream(bundle / "manifest.json") << manifest.dump(2) << '\n';
    }
    return molga::Sha256File(bundle / "manifest.json");
}

inline nlohmann::json MinimalPackageGameConfig(
    const std::string& shaderManifestSha256) {
    return {
        {"schemaVersion", 4},
        {"mainScene", "Scenes/main.json"},
        {"scenes", {"Scenes/main.json"}},
        {"startupSceneId", "Scenes/main.json"},
        {"sceneCatalog", {{{"id", "Scenes/main.json"},
                            {"packagePath", "Scenes/main.json"}}}},
        {"graphics", {
            {"api", "sdlgpu"},
            {"driver", "metal"},
            {"shaderFormat", "msl"},
            {"shaderManifest", "ShaderBundle/manifest.json"},
            {"shaderManifestSha256", shaderManifestSha256},
        }},
    };
}

} // namespace test_support
