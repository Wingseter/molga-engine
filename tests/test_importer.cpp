#include "Core/Importers/Importer.h"
#include "Core/Importers/ImporterRegistry.h"
#include "Core/Importers/PrefabImporter.h"
#include "Core/Importers/TextureImporter.h"
#include "Core/Importers/AudioImporter.h"
#include "doctest.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

using molga::TextureImporter;
using molga::AudioImporter;
using molga::ImportResult;
namespace fs = std::filesystem;

namespace {

void WriteImporterU16(std::ofstream& file, std::uint16_t value) {
    const char bytes[2] = {static_cast<char>(value & 0xffu),
                           static_cast<char>((value >> 8u) & 0xffu)};
    file.write(bytes, sizeof(bytes));
}

void WriteImporterU32(std::ofstream& file, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 0xffu),
                           static_cast<char>((value >> 8u) & 0xffu),
                           static_cast<char>((value >> 16u) & 0xffu),
                           static_cast<char>((value >> 24u) & 0xffu)};
    file.write(bytes, sizeof(bytes));
}

void WriteImporterWav(const fs::path& path) {
    constexpr std::uint32_t sampleRate = 8000;
    constexpr std::uint32_t frames = 800;
    constexpr std::uint32_t dataBytes = frames * sizeof(std::int16_t);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write("RIFF", 4);
    WriteImporterU32(file, 36 + dataBytes);
    file.write("WAVEfmt ", 8);
    WriteImporterU32(file, 16);
    WriteImporterU16(file, 1);
    WriteImporterU16(file, 1);
    WriteImporterU32(file, sampleRate);
    WriteImporterU32(file, sampleRate * sizeof(std::int16_t));
    WriteImporterU16(file, sizeof(std::int16_t));
    WriteImporterU16(file, 16);
    file.write("data", 4);
    WriteImporterU32(file, dataBytes);
    for (std::uint32_t i = 0; i < frames; ++i) WriteImporterU16(file, 0);
}

class ThrowingImporter final : public molga::IImporter {
public:
    std::string Name() const override { return "ThrowingImporterForTest"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& extension) const override {
        return extension == ".throwtest";
    }
    ImportResult Import(const std::string&) const override {
        throw std::runtime_error("fixture exception");
    }
};

} // namespace

TEST_CASE("TextureImporter reports its name and a positive version") {
    TextureImporter imp;
    CHECK(imp.Name() == std::string("TextureImporter"));
    CHECK(imp.Version() >= 1);
}

TEST_CASE("TextureImporter accepts image extensions only") {
    TextureImporter imp;
    CHECK(imp.CanImport(".png"));
    CHECK(imp.CanImport(".jpg"));
    CHECK_FALSE(imp.CanImport(".wav"));
}

TEST_CASE("Import of a missing file fails gracefully") {
    TextureImporter imp;
    ImportResult r = imp.Import("does_not_exist.png");
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("AudioImporter validates decoder headers and reports metadata") {
    AudioImporter importer;
    CHECK(importer.Name() == std::string("AudioImporter"));
    CHECK(importer.Version() == 2);
    CHECK(importer.CanImport(".wav"));
    CHECK(importer.CanImport(".mp3"));
    CHECK(importer.CanImport(".ogg"));
    CHECK_FALSE(importer.CanImport(".png"));

    const fs::path root = fs::temp_directory_path() / "molga_audio_importer_test";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    const fs::path valid = root / "valid.wav";
    WriteImporterWav(valid);

    const ImportResult result = importer.Import(
        valid.string(), {{"loadMode", "Streaming"}});
    REQUIRE(result.success);
    CHECK(result.metadata["channels"] == 1);
    CHECK(result.metadata["sampleRate"] == 8000);
    CHECK(result.metadata["durationSeconds"].get<double>() == doctest::Approx(0.1));
    CHECK(result.metadata["loadMode"] == "Streaming");

    fs::remove_all(root, error);
}

TEST_CASE("AudioImporter rejects corrupt files and invalid load modes") {
    AudioImporter importer;
    const fs::path root = fs::temp_directory_path() / "molga_audio_importer_bad_test";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    const fs::path corrupt = root / "corrupt.wav";
    { std::ofstream(corrupt, std::ios::binary) << "not a wave"; }

    const ImportResult corruptResult = importer.Import(corrupt.string());
    CHECK_FALSE(corruptResult.success);
    CHECK_FALSE(corruptResult.error.empty());

    const fs::path valid = root / "valid.wav";
    WriteImporterWav(valid);
    const ImportResult invalidSetting = importer.Import(
        valid.string(), {{"loadMode", "Unknown"}});
    CHECK_FALSE(invalidSetting.success);
    CHECK_FALSE(invalidSetting.error.empty());

    fs::remove_all(root, error);
}

TEST_CASE("ImporterRegistry discovers builtins case-insensitively") {
    auto& registry = molga::ImporterRegistry::Get();
    const auto* texture = registry.FindForExtension(".PNG");
    REQUIRE(texture != nullptr);
    CHECK(texture->Name() == "TextureImporter");
    CHECK(texture->Version() == 2);

    const auto* clip = registry.FindForExtension(".ANIMCLIP");
    REQUIRE(clip != nullptr);
    CHECK(clip->Name() == "AnimationClipImporter");
    CHECK(registry.FindByName("AnimatorControllerImporter") != nullptr);
    CHECK(registry.FindByName("TileSetImporter") != nullptr);
}

TEST_CASE("Structured importers record unique GUID dependencies") {
    const fs::path root = fs::temp_directory_path() / "molga_structured_importer_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const std::string textureGuid = "1234567890abcdef1234567890abcdef";
    const fs::path clip = root / "run.animclip";
    { std::ofstream(clip) << nlohmann::json({
        {"schemaVersion", 4},
        {"textureGuid", textureGuid},
        {"preview", {{"textureGuid", textureGuid}}}
    }).dump(2); }

    const ImportResult result = molga::ImporterRegistry::Get().Import(
        "AnimationClipImporter", clip.string());
    REQUIRE(result.success);
    REQUIRE(result.dependencies.size() == 1);
    CHECK(result.dependencies[0] == textureGuid);
    CHECK(result.metadata["schemaVersion"] == 4);

    fs::remove_all(root);
}

TEST_CASE("ImporterRegistry distinguishes GenericImporter from a missing importer") {
    const fs::path root = fs::temp_directory_path() / "molga_registry_generic_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path source = root / "data.custom";
    { std::ofstream(source) << "opaque"; }

    const ImportResult generic = molga::ImporterRegistry::Get().Import(
        "GenericImporter", source.string());
    CHECK(generic.success);
    const ImportResult missing = molga::ImporterRegistry::Get().Import(
        "UninstalledThirdPartyImporter", source.string());
    CHECK_FALSE(missing.success);
    CHECK(missing.error.find("not registered") != std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("ImporterRegistry converts importer exceptions into failure state") {
    auto& registry = molga::ImporterRegistry::Get();
    registry.Register(std::make_shared<ThrowingImporter>());
    const ImportResult result = registry.Import("ThrowingImporterForTest", "unused.throwtest");
    CHECK_FALSE(result.success);
    CHECK(result.error.find("fixture exception") != std::string::npos);
}

TEST_CASE("Malformed prefab and structured schema types fail without throwing") {
    const fs::path root = fs::temp_directory_path() / "molga_importer_malformed_json_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path prefab = root / "bad.prefab";
    const fs::path clip = root / "bad.animclip";
    { std::ofstream(prefab) << R"({"guid": 42})"; }
    { std::ofstream(clip) << R"({"schemaVersion": "new", "textureGuid": "abc"})"; }

    CHECK(molga::PrefabImporter::ReadEmbeddedGuid(prefab.string()).empty());
    ImportResult prefabResult;
    CHECK_NOTHROW(prefabResult = molga::ImporterRegistry::Get().Import(
        "PrefabImporter", prefab.string()));
    CHECK_FALSE(prefabResult.success);

    ImportResult clipResult;
    CHECK_NOTHROW(clipResult = molga::ImporterRegistry::Get().Import(
        "AnimationClipImporter", clip.string()));
    CHECK_FALSE(clipResult.success);
    CHECK_FALSE(clipResult.error.empty());
    CHECK(clipResult.dependencies.empty());

    fs::remove_all(root);
}
