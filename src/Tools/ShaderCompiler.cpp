#include "Common/Sha256.h"
#include "Rendering/ShaderBundle.h"

#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* kToolRevision =
    "molga_shaderc-v1+SDL_shadercross-e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba";

struct Parameter {
    std::string name;
    std::string type;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct TextureBinding {
    std::string name;
    std::string stage;
    std::string dimension;
    std::uint32_t slot = 0;
};

struct VertexAttribute {
    std::uint32_t location = 0;
    std::string format;
    std::uint32_t offset = 0;
};

struct Descriptor {
    fs::path path;
    fs::path root;
    std::string name;
    fs::path sourcePath;
    std::string storedSource;
    std::string source;
    std::string vertexEntry;
    std::string fragmentEntry;
    std::uint32_t vertexStride = 0;
    std::vector<VertexAttribute> vertexAttributes;
    std::vector<Parameter> parameters;
    std::uint32_t parameterBlockSize = 0;
    std::vector<TextureBinding> textures;
    std::string descriptorSha256;
    std::string sourceSha256;
    std::string bindings;
    std::string bindingsSha256;
};

std::string ReadText(const fs::path& path, std::string& errorOut) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        errorOut = "could not open file: " + path.string();
        return {};
    }
    std::ostringstream output;
    output << input.rdbuf();
    if (!input.eof() && input.fail()) {
        errorOut = "could not read file: " + path.string();
        return {};
    }
    errorOut.clear();
    return output.str();
}

bool WriteBytes(const fs::path& path, const void* data, std::size_t size,
                std::string& errorOut) {
    std::error_code filesystemError;
    fs::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) {
        errorOut = "could not create shader artifact directory: " +
                   filesystemError.message();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        errorOut = "could not create shader artifact: " + path.string();
        return false;
    }
    output.write(static_cast<const char*>(data),
                 static_cast<std::streamsize>(size));
    if (!output) {
        errorOut = "could not write shader artifact: " + path.string();
        return false;
    }
    return true;
}

bool WriteText(const fs::path& path, const std::string& text,
               std::string& errorOut) {
    return WriteBytes(path, text.data(), text.size(), errorOut);
}

std::string FoldCase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool IsIdentifier(const std::string& value) {
    if (value.empty() ||
        !(std::isalpha(static_cast<unsigned char>(value.front())) ||
          value.front() == '_')) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(),
                       [](unsigned char character) {
                           return std::isalnum(character) || character == '_';
                       });
}

bool IsShaderName(const std::string& value) {
    return !value.empty() && std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '_' ||
                   character == '-' || character == '.';
        });
}

std::uint32_t VertexFormatSize(const std::string& format) {
    if (format == "Float") return 4U;
    if (format == "Float2") return 8U;
    if (format == "Float3") return 12U;
    if (format == "Float4") return 16U;
    if (format == "UByte4Norm") return 4U;
    return 0U;
}

bool ParseDescriptor(const fs::path& root, const fs::path& path,
                     Descriptor& output, std::string& errorOut) {
    const std::string descriptorText = ReadText(path, errorOut);
    if (!errorOut.empty()) return false;
    Json json;
    try {
        json = Json::parse(descriptorText);
    } catch (const std::exception& exception) {
        errorOut = path.string() + ": invalid JSON: " + exception.what();
        return false;
    }
    if (!json.is_object() || json.value("schemaVersion", 0) != 1 ||
        !json.contains("name") || !json["name"].is_string() ||
        !json.contains("source") || !json["source"].is_string() ||
        !json.contains("vertex") || !json["vertex"].is_object() ||
        !json.contains("fragment") || !json["fragment"].is_object() ||
        !json.contains("parameters") || !json["parameters"].is_array() ||
        !json.contains("textures") || !json["textures"].is_array()) {
        errorOut = path.string() + ": descriptor does not match schema v1";
        return false;
    }

    Descriptor candidate;
    candidate.path = path;
    candidate.root = root;
    candidate.name = json["name"].get<std::string>();
    candidate.storedSource = json["source"].get<std::string>();
    if (!IsShaderName(candidate.name)) {
        errorOut = path.string() + ": invalid shader name";
        return false;
    }
    const fs::path storedSource(candidate.storedSource);
    if (storedSource.empty() || storedSource.is_absolute() ||
        storedSource.has_root_name() || storedSource.has_root_directory() ||
        storedSource.extension() != ".hlsl") {
        errorOut = path.string() + ": source must be a relative .hlsl path";
        return false;
    }
    for (const auto& component : storedSource.lexically_normal()) {
        if (component == "..") {
            errorOut = path.string() + ": shader source escapes descriptor root";
            return false;
        }
    }
    candidate.sourcePath = path.parent_path() / storedSource;
    candidate.source = ReadText(candidate.sourcePath, errorOut);
    if (!errorOut.empty()) return false;

    static const std::regex manualRegister(
        R"(\bregister\s*\()", std::regex_constants::icase);
    static const std::regex forbiddenLayoutQualifier(
        R"(\blayout\s*\()", std::regex_constants::icase);
    if (std::regex_search(candidate.source, manualRegister)) {
        errorOut = candidate.sourcePath.string() +
                   ": manual HLSL register declarations are forbidden";
        return false;
    }
    if (candidate.source.find("#version") != std::string::npos ||
        std::regex_search(candidate.source, forbiddenLayoutQualifier)) {
        errorOut = candidate.sourcePath.string() +
                   ": legacy shader source is not accepted";
        return false;
    }
    if (candidate.source.find("\"molga_bindings.hlsl\"") ==
        std::string::npos) {
        errorOut = candidate.sourcePath.string() +
                   ": source must include generated molga_bindings.hlsl";
        return false;
    }

    const Json& vertex = json["vertex"];
    const Json& fragment = json["fragment"];
    if (!vertex.contains("entry") || !vertex["entry"].is_string() ||
        !vertex.contains("layout") || !vertex["layout"].is_object() ||
        !fragment.contains("entry") || !fragment["entry"].is_string()) {
        errorOut = path.string() + ": invalid vertex or fragment descriptor";
        return false;
    }
    candidate.vertexEntry = vertex["entry"].get<std::string>();
    candidate.fragmentEntry = fragment["entry"].get<std::string>();
    if (!IsIdentifier(candidate.vertexEntry) ||
        !IsIdentifier(candidate.fragmentEntry)) {
        errorOut = path.string() + ": entry points must be HLSL identifiers";
        return false;
    }
    const Json& layout = vertex["layout"];
    if (!layout.contains("stride") || !layout["stride"].is_number_unsigned() ||
        !layout.contains("attributes") || !layout["attributes"].is_array()) {
        errorOut = path.string() + ": invalid vertex layout";
        return false;
    }
    candidate.vertexStride = layout["stride"].get<std::uint32_t>();
    std::set<std::uint32_t> locations;
    for (const Json& attribute : layout["attributes"]) {
        if (!attribute.is_object() ||
            !attribute.contains("location") ||
            !attribute["location"].is_number_unsigned() ||
            !attribute.contains("format") || !attribute["format"].is_string() ||
            !attribute.contains("offset") ||
            !attribute["offset"].is_number_unsigned()) {
            errorOut = path.string() + ": invalid vertex attribute";
            return false;
        }
        VertexAttribute record;
        record.location = attribute["location"].get<std::uint32_t>();
        record.format = attribute["format"].get<std::string>();
        record.offset = attribute["offset"].get<std::uint32_t>();
        const std::uint32_t size = VertexFormatSize(record.format);
        if (size == 0U || record.offset + size > candidate.vertexStride ||
            !locations.insert(record.location).second) {
            errorOut = path.string() +
                       ": invalid, duplicate, or out-of-range vertex attribute";
            return false;
        }
        candidate.vertexAttributes.push_back(std::move(record));
    }
    if (candidate.vertexStride == 0U || candidate.vertexAttributes.empty()) {
        errorOut = path.string() + ": vertex layout cannot be empty";
        return false;
    }

    std::set<std::string> parameterNames;
    std::uint32_t nextOffset = 0U;
    for (const Json& parameter : json["parameters"]) {
        if (!parameter.is_object() ||
            !parameter.contains("name") || !parameter["name"].is_string() ||
            !parameter.contains("type") || !parameter["type"].is_string()) {
            errorOut = path.string() + ": invalid material parameter";
            return false;
        }
        Parameter record;
        record.name = parameter["name"].get<std::string>();
        record.type = parameter["type"].get<std::string>();
        record.offset = nextOffset;
        record.size = record.type == "Float" ? 4U : 16U;
        if (!IsIdentifier(record.name) ||
            (record.type != "Float" && record.type != "Vec4") ||
            !parameterNames.insert(record.name).second) {
            errorOut = path.string() +
                       ": parameters require unique names and Float/Vec4 types";
            return false;
        }
        candidate.parameters.push_back(std::move(record));
        nextOffset += 16U;
    }
    candidate.parameterBlockSize = nextOffset;

    std::map<std::string, std::set<std::string>> textureNames;
    for (const Json& texture : json["textures"]) {
        if (!texture.is_object() ||
            !texture.contains("name") || !texture["name"].is_string() ||
            !texture.contains("stage") || !texture["stage"].is_string() ||
            !texture.contains("dimension") ||
            !texture["dimension"].is_string()) {
            errorOut = path.string() + ": invalid texture binding";
            return false;
        }
        TextureBinding record;
        record.name = texture["name"].get<std::string>();
        record.stage = texture["stage"].get<std::string>();
        record.dimension = texture["dimension"].get<std::string>();
        if (!IsIdentifier(record.name) ||
            (record.stage != "vertex" && record.stage != "fragment") ||
            (record.dimension != "2D" && record.dimension != "2DArray") ||
            !textureNames[record.stage].insert(record.name).second) {
            errorOut = path.string() + ": invalid or duplicate texture binding";
            return false;
        }
        candidate.textures.push_back(std::move(record));
    }

    std::ostringstream bindings;
    bindings << "#ifndef MOLGA_GENERATED_BINDINGS_HLSL\n"
             << "#define MOLGA_GENERATED_BINDINGS_HLSL\n"
             << "#define MOLGA_VS_UNIFORM0 : register(b0, space1)\n"
             << "#define MOLGA_PS_UNIFORM0 : register(b0, space3)\n";
    std::uint32_t vertexTextureSlot = 0U;
    std::uint32_t fragmentTextureSlot = 0U;
    for (const TextureBinding& texture : candidate.textures) {
        const bool vertexStage = texture.stage == "vertex";
        const std::uint32_t slot = vertexStage
            ? vertexTextureSlot++ : fragmentTextureSlot++;
        const char* prefix = vertexStage ? "MOLGA_VS" : "MOLGA_PS";
        const int space = vertexStage ? 0 : 2;
        bindings << "#define " << prefix << "_TEXTURE_" << texture.name
                 << " register(t" << slot << ", space" << space << ")\n"
                 << "#define " << prefix << "_SAMPLER_" << texture.name
                 << " register(s" << slot << ", space" << space << ")\n";
    }
    if (!candidate.parameters.empty()) {
        bindings << "cbuffer MolgaMaterialParameters MOLGA_PS_UNIFORM0 {\n";
        std::uint32_t padIndex = 0U;
        for (const Parameter& parameter : candidate.parameters) {
            if (parameter.type == "Float") {
                bindings << "    float " << parameter.name << ";\n"
                         << "    float3 _molgaPad" << padIndex++ << ";\n";
            } else {
                bindings << "    float4 " << parameter.name << ";\n";
            }
        }
        bindings << "};\n";
    }
    bindings << "#endif\n";
    candidate.bindings = bindings.str();
    candidate.descriptorSha256 = molga::Sha256String(descriptorText);
    candidate.sourceSha256 = molga::Sha256String(candidate.source);
    candidate.bindingsSha256 = molga::Sha256String(candidate.bindings);
    output = std::move(candidate);
    errorOut.clear();
    return true;
}

std::string IOVarTypeName(SDL_ShaderCross_IOVarType type,
                          std::uint32_t vectorSize) {
    const char* base = "unknown";
    switch (type) {
        case SDL_SHADERCROSS_IOVAR_TYPE_INT8: base = "byte"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_UINT8: base = "ubyte"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_INT16: base = "short"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_UINT16: base = "ushort"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_INT32: base = "int"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_UINT32: base = "uint"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_INT64: base = "long"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_UINT64: base = "ulong"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_FLOAT16: base = "half"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_FLOAT32: base = "float"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_FLOAT64: base = "double"; break;
        case SDL_SHADERCROSS_IOVAR_TYPE_UNKNOWN: break;
    }
    return vectorSize > 1U ? std::string(base) + std::to_string(vectorSize)
                           : std::string(base);
}

Json ReflectionJson(const SDL_ShaderCross_GraphicsShaderMetadata& metadata) {
    Json json;
    json["resources"] = {
        {"samplers", metadata.resource_info.num_samplers},
        {"storageTextures", metadata.resource_info.num_storage_textures},
        {"storageBuffers", metadata.resource_info.num_storage_buffers},
        {"uniformBuffers", metadata.resource_info.num_uniform_buffers},
    };
    json["inputs"] = Json::array();
    for (std::uint32_t index = 0; index < metadata.num_inputs; ++index) {
        const SDL_ShaderCross_IOVarMetadata& variable = metadata.inputs[index];
        json["inputs"].push_back({
            {"name", variable.name ? variable.name : ""},
            {"location", variable.location},
            {"type", IOVarTypeName(variable.vector_type, variable.vector_size)},
        });
    }
    json["outputs"] = Json::array();
    for (std::uint32_t index = 0; index < metadata.num_outputs; ++index) {
        const SDL_ShaderCross_IOVarMetadata& variable = metadata.outputs[index];
        json["outputs"].push_back({
            {"name", variable.name ? variable.name : ""},
            {"location", variable.location},
            {"type", IOVarTypeName(variable.vector_type, variable.vector_size)},
        });
    }
    return json;
}

std::string ExtractMslEntryPoint(const std::string& source,
                                 const std::string& stage,
                                 const std::string& fallback) {
    const std::regex expression(
        std::string("\\b") + (stage == "vertex" ? "vertex" : "fragment") +
        R"(\s+[A-Za-z_][A-Za-z0-9_<>:, ]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()",
        std::regex_constants::icase);
    std::smatch match;
    return std::regex_search(source, match, expression) && match.size() > 1U
        ? match[1].str() : fallback;
}

Json ArtifactJson(const fs::path& stagingRoot, const fs::path& path,
                  const std::string& entryPoint, std::string& errorOut) {
    const std::string digest = molga::Sha256File(path, &errorOut);
    if (digest.empty()) return {};
    return {
        {"path", fs::relative(path, stagingRoot).generic_string()},
        {"sha256", digest},
        {"entryPoint", entryPoint},
    };
}

bool CompileStage(const Descriptor& descriptor, const fs::path& stagingRoot,
                  const fs::path& includeDirectory, const std::string& stage,
                  Json& output, std::string& errorOut) {
    const bool vertex = stage == "vertex";
    const std::string& entryPoint =
        vertex ? descriptor.vertexEntry : descriptor.fragmentEntry;
    SDL_ShaderCross_HLSL_Info hlsl{};
    hlsl.source = descriptor.source.c_str();
    hlsl.entrypoint = entryPoint.c_str();
    const std::string includeDirectoryString = includeDirectory.string();
    hlsl.include_dir = includeDirectoryString.c_str();
    hlsl.shader_stage = vertex ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX
                               : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;

    std::size_t spirvSize = 0U;
    void* spirv = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl, &spirvSize);
    if (!spirv || spirvSize == 0U) {
        errorOut = descriptor.name + " " + stage +
                   " SPIR-V compile failed: " + SDL_GetError();
        if (spirv) SDL_free(spirv);
        return false;
    }
    SDL_ShaderCross_GraphicsShaderMetadata* reflection =
        SDL_ShaderCross_ReflectGraphicsSPIRV(
            static_cast<const Uint8*>(spirv), spirvSize, 0);
    if (!reflection) {
        errorOut = descriptor.name + " " + stage +
                   " reflection failed: " + SDL_GetError();
        SDL_free(spirv);
        return false;
    }

    const std::uint32_t expectedSamplers = static_cast<std::uint32_t>(
        std::count_if(descriptor.textures.begin(), descriptor.textures.end(),
                      [&](const TextureBinding& texture) {
                          return texture.stage == stage;
                      }));
    if (reflection->resource_info.num_samplers != expectedSamplers) {
        errorOut = descriptor.name + " " + stage +
                   " sampler reflection mismatch: expected " +
                   std::to_string(expectedSamplers) + ", got " +
                   std::to_string(reflection->resource_info.num_samplers);
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }
    if (vertex && reflection->num_inputs != descriptor.vertexAttributes.size()) {
        errorOut = descriptor.name +
                   " vertex input reflection does not match descriptor layout";
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }

    const fs::path artifactDirectory = stagingRoot / "artifacts";
    const fs::path reflectionDirectory = stagingRoot / "reflection";
    const fs::path spirvPath = artifactDirectory /
        (descriptor.name + "." + stage + ".spv");
    const fs::path mslPath = artifactDirectory /
        (descriptor.name + "." + stage + ".msl");
    const fs::path dxilPath = artifactDirectory /
        (descriptor.name + "." + stage + ".dxil");
    const fs::path reflectionPath = reflectionDirectory /
        (descriptor.name + "." + stage + ".json");

    if (!WriteBytes(spirvPath, spirv, spirvSize, errorOut)) {
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }
    const std::string reflectionText = ReflectionJson(*reflection).dump(2) + "\n";
    if (!WriteText(reflectionPath, reflectionText, errorOut)) {
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }

    SDL_PropertiesID mslProperties = SDL_CreateProperties();
    if (!mslProperties ||
        !SDL_SetStringProperty(mslProperties,
            SDL_SHADERCROSS_PROP_SPIRV_MSL_VERSION_STRING, "1.2.0")) {
        errorOut = "could not configure MSL 1.2 shader translation: " +
                   std::string(SDL_GetError());
        if (mslProperties) SDL_DestroyProperties(mslProperties);
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }
    SDL_ShaderCross_SPIRV_Info spirvInfo{};
    spirvInfo.bytecode = static_cast<const Uint8*>(spirv);
    spirvInfo.bytecode_size = spirvSize;
    spirvInfo.entrypoint = entryPoint.c_str();
    spirvInfo.shader_stage = hlsl.shader_stage;
    spirvInfo.props = mslProperties;
    char* msl = static_cast<char*>(
        SDL_ShaderCross_TranspileMSLFromSPIRV(&spirvInfo));
    SDL_DestroyProperties(mslProperties);
    if (!msl) {
        errorOut = descriptor.name + " " + stage +
                   " MSL compile failed: " + SDL_GetError();
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }
    const std::size_t mslSize = SDL_strlen(msl) + 1U;
    const std::string mslSource(msl, mslSize - 1U);
    const std::string mslEntryPoint =
        ExtractMslEntryPoint(mslSource, stage, entryPoint);
    if (!WriteBytes(mslPath, msl, mslSize, errorOut)) {
        SDL_free(msl);
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }

    std::size_t dxilSize = 0U;
    void* dxil = SDL_ShaderCross_CompileDXILFromHLSL(&hlsl, &dxilSize);
    if (!dxil || dxilSize == 0U) {
        errorOut = descriptor.name + " " + stage +
                   " DXIL compile failed: " + SDL_GetError();
        if (dxil) SDL_free(dxil);
        SDL_free(msl);
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }
    if (!WriteBytes(dxilPath, dxil, dxilSize, errorOut)) {
        SDL_free(dxil);
        SDL_free(msl);
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }

    output["stage"] = stage;
    output["entryPoint"] = entryPoint;
    output["resources"] = {
        {"samplers", reflection->resource_info.num_samplers},
        {"storageTextures", reflection->resource_info.num_storage_textures},
        {"storageBuffers", reflection->resource_info.num_storage_buffers},
        {"uniformBuffers", reflection->resource_info.num_uniform_buffers},
    };
    output["reflection"] = {
        {"path", fs::relative(reflectionPath, stagingRoot).generic_string()},
        {"sha256", molga::Sha256File(reflectionPath, &errorOut)},
    };
    if (!errorOut.empty()) {
        SDL_free(dxil);
        SDL_free(msl);
        SDL_free(reflection);
        SDL_free(spirv);
        return false;
    }
    output["artifacts"] = Json::object();
    output["artifacts"]["spirv"] =
        ArtifactJson(stagingRoot, spirvPath, entryPoint, errorOut);
    output["artifacts"]["msl"] =
        ArtifactJson(stagingRoot, mslPath, mslEntryPoint, errorOut);
    output["artifacts"]["dxil"] =
        ArtifactJson(stagingRoot, dxilPath, entryPoint, errorOut);

    SDL_free(dxil);
    SDL_free(msl);
    SDL_free(reflection);
    SDL_free(spirv);
    return errorOut.empty();
}

std::uint64_t RevisionFromDigest(const std::string& digest) {
    try {
        return std::stoull(digest.substr(0, 16), nullptr, 16);
    } catch (const std::exception&) {
        return 0U;
    }
}

bool CompileDescriptor(const Descriptor& descriptor, const fs::path& stagingRoot,
                       Json& output, std::string& errorOut) {
    const fs::path includeDirectory =
        stagingRoot / "bindings" / descriptor.name;
    const fs::path bindingsPath = includeDirectory / "molga_bindings.hlsl";
    if (!WriteText(bindingsPath, descriptor.bindings, errorOut)) return false;

    output["name"] = descriptor.name;
    output["source"] = descriptor.storedSource;
    output["sourceSha256"] = descriptor.sourceSha256;
    output["descriptorSha256"] = descriptor.descriptorSha256;
    output["bindingsSha256"] = descriptor.bindingsSha256;
    const std::string revisionDigest = molga::Sha256String(
        descriptor.descriptorSha256 + descriptor.sourceSha256 +
        descriptor.bindingsSha256 + kToolRevision);
    output["revision"] = RevisionFromDigest(revisionDigest);
    output["vertexLayout"] = {
        {"stride", descriptor.vertexStride},
        {"attributes", Json::array()},
    };
    for (const VertexAttribute& attribute : descriptor.vertexAttributes) {
        output["vertexLayout"]["attributes"].push_back({
            {"location", attribute.location},
            {"format", attribute.format},
            {"offset", attribute.offset},
        });
    }
    output["parameterBlockSize"] = descriptor.parameterBlockSize;
    output["parameters"] = Json::array();
    for (const Parameter& parameter : descriptor.parameters) {
        output["parameters"].push_back({
            {"name", parameter.name},
            {"type", parameter.type},
            {"offset", parameter.offset},
            {"size", parameter.size},
        });
    }
    output["textures"] = Json::array();
    std::uint32_t vertexTextureSlot = 0U;
    std::uint32_t fragmentTextureSlot = 0U;
    for (const TextureBinding& texture : descriptor.textures) {
        const std::uint32_t slot = texture.stage == "vertex"
            ? vertexTextureSlot++ : fragmentTextureSlot++;
        output["textures"].push_back({
            {"name", texture.name},
            {"stage", texture.stage},
            {"dimension", texture.dimension},
            {"slot", slot},
        });
    }

    Json vertex;
    Json fragment;
    if (!CompileStage(descriptor, stagingRoot, includeDirectory, "vertex",
                      vertex, errorOut) ||
        !CompileStage(descriptor, stagingRoot, includeDirectory, "fragment",
                      fragment, errorOut)) {
        return false;
    }
    output["vertex"] = std::move(vertex);
    output["fragment"] = std::move(fragment);
    return true;
}

bool DiscoverDescriptors(const std::vector<fs::path>& roots,
                         std::vector<Descriptor>& descriptors,
                         std::string& errorOut) {
    std::vector<std::pair<fs::path, fs::path>> paths;
    for (const fs::path& root : roots) {
        std::error_code filesystemError;
        if (!fs::is_directory(root, filesystemError)) {
            errorOut = "descriptor root is not a directory: " + root.string();
            return false;
        }
        for (fs::recursive_directory_iterator iterator(root, filesystemError), end;
             iterator != end && !filesystemError; iterator.increment(filesystemError)) {
            if (!iterator->is_regular_file()) continue;
            const std::string filename = iterator->path().filename().string();
            if (filename.size() >= 12U &&
                filename.compare(filename.size() - 12U, 12U,
                                 ".shader.json") == 0) {
                paths.emplace_back(root, iterator->path());
            }
        }
        if (filesystemError) {
            errorOut = "could not enumerate descriptor root: " +
                       filesystemError.message();
            return false;
        }
    }
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.second.generic_string() < right.second.generic_string();
    });
    if (paths.empty()) {
        errorOut = "no *.shader.json descriptors were found";
        return false;
    }

    std::set<std::string> names;
    for (const auto& [root, path] : paths) {
        Descriptor descriptor;
        if (!ParseDescriptor(root, path, descriptor, errorOut)) return false;
        if (!names.insert(FoldCase(descriptor.name)).second) {
            errorOut = "duplicate or case-colliding shader name: " + descriptor.name;
            return false;
        }
        descriptors.push_back(std::move(descriptor));
    }
    return true;
}

long ProcessId() {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

bool InstallAtomically(const fs::path& staging, const fs::path& destination,
                       std::string& errorOut) {
    if (destination.empty() || destination == destination.root_path()) {
        errorOut = "refusing unsafe shader bundle destination";
        return false;
    }
    const fs::path backup = destination.parent_path() /
        (destination.filename().string() + ".last-good");
    std::error_code filesystemError;
    fs::remove_all(backup, filesystemError);
    filesystemError.clear();
    const bool hadDestination = fs::exists(destination, filesystemError);
    if (filesystemError) {
        errorOut = "could not inspect shader bundle destination: " +
                   filesystemError.message();
        return false;
    }
    if (hadDestination) {
        fs::rename(destination, backup, filesystemError);
        if (filesystemError) {
            errorOut = "could not preserve last-good shader bundle: " +
                       filesystemError.message();
            return false;
        }
    }
    fs::rename(staging, destination, filesystemError);
    if (filesystemError) {
        if (hadDestination) {
            std::error_code restoreError;
            fs::rename(backup, destination, restoreError);
        }
        errorOut = "could not install shader bundle: " + filesystemError.message();
        return false;
    }
    fs::remove_all(backup, filesystemError);
    errorOut.clear();
    return true;
}

bool BuildBundle(const std::vector<fs::path>& roots, const fs::path& output,
                 std::string& errorOut) {
    std::vector<Descriptor> descriptors;
    if (!DiscoverDescriptors(roots, descriptors, errorOut)) return false;

    std::error_code filesystemError;
    fs::create_directories(output.parent_path(), filesystemError);
    if (filesystemError) {
        errorOut = "could not create shader bundle parent directory: " +
                   filesystemError.message();
        return false;
    }
    const fs::path staging = output.parent_path() /
        (output.filename().string() + ".staging-" + std::to_string(ProcessId()));
    fs::remove_all(staging, filesystemError);
    filesystemError.clear();
    fs::create_directories(staging, filesystemError);
    if (filesystemError) {
        errorOut = "could not create shader staging directory: " +
                   filesystemError.message();
        return false;
    }

    if (!SDL_ShaderCross_Init()) {
        errorOut = "SDL_shadercross initialization failed: " +
                   std::string(SDL_GetError());
        fs::remove_all(staging, filesystemError);
        return false;
    }

    Json manifest;
    manifest["schemaVersion"] = 1;
    manifest["artifactFormats"] = {"spirv", "msl", "dxil"};
    manifest["tool"] = {
        {"name", "molga_shaderc"},
        {"revision", kToolRevision},
        {"shadercrossRevision",
         "e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba"},
        {"mslVersion", "1.2.0"},
    };
    manifest["entries"] = Json::array();
    bool success = true;
    for (const Descriptor& descriptor : descriptors) {
        Json entry;
        if (!CompileDescriptor(descriptor, staging, entry, errorOut)) {
            success = false;
            break;
        }
        manifest["entries"].push_back(std::move(entry));
    }
    SDL_ShaderCross_Quit();
    if (!success) {
        fs::remove_all(staging, filesystemError);
        return false;
    }

    const fs::path manifestPath = staging / "manifest.json";
    if (!WriteText(manifestPath, manifest.dump(2) + "\n", errorOut)) {
        fs::remove_all(staging, filesystemError);
        return false;
    }
    molga::ShaderBundleManifest parsed;
    if (!molga::ShaderBundleManifest::Load(manifestPath, parsed, errorOut) ||
        !parsed.Validate(staging, true, errorOut)) {
        fs::remove_all(staging, filesystemError);
        return false;
    }
    return InstallAtomically(staging, output, errorOut);
}

bool PackageMslBundle(const fs::path& source, const fs::path& output,
                      std::string& errorOut) {
    molga::ShaderBundleManifest parsed;
    if (!molga::ShaderBundleManifest::Load(source / "manifest.json", parsed,
                                           errorOut) ||
        !parsed.Validate(source, true, errorOut)) {
        return false;
    }

    Json manifest;
    try {
        std::ifstream input(source / "manifest.json");
        input >> manifest;
    } catch (const std::exception& exception) {
        errorOut = "could not read source shader manifest: " +
                   std::string(exception.what());
        return false;
    }

    std::error_code filesystemError;
    fs::create_directories(output.parent_path(), filesystemError);
    if (filesystemError) {
        errorOut = "could not create packaged shader parent directory: " +
                   filesystemError.message();
        return false;
    }
    const fs::path staging = output.parent_path() /
        (output.filename().string() + ".staging-" +
         std::to_string(ProcessId()));
    fs::remove_all(staging, filesystemError);
    filesystemError.clear();
    fs::create_directories(staging, filesystemError);
    if (filesystemError) {
        errorOut = "could not create packaged shader staging directory: " +
                   filesystemError.message();
        return false;
    }

    manifest["artifactFormats"] = Json::array({"msl"});
    for (Json& entry : manifest["entries"]) {
        for (const char* stageName : {"vertex", "fragment"}) {
            Json& stage = entry[stageName];
            if (!stage.contains("artifacts") ||
                !stage["artifacts"].contains("msl")) {
                errorOut = "missing MSL artifact while packaging shader " +
                           entry.value("name", std::string{});
                fs::remove_all(staging, filesystemError);
                return false;
            }
            Json msl = stage["artifacts"]["msl"];
            stage["artifacts"] = Json::object({{"msl", msl}});
            stage.erase("reflection");

            const fs::path relative = msl.value("path", std::string{});
            const fs::path sourceArtifact = source / relative;
            const fs::path destinationArtifact = staging / relative;
            fs::create_directories(destinationArtifact.parent_path(),
                                   filesystemError);
            if (!filesystemError) {
                fs::copy_file(sourceArtifact, destinationArtifact,
                              fs::copy_options::overwrite_existing,
                              filesystemError);
            }
            if (filesystemError) {
                errorOut = "could not copy packaged MSL artifact: " +
                           filesystemError.message();
                fs::remove_all(staging, filesystemError);
                return false;
            }
        }
    }

    const fs::path manifestPath = staging / "manifest.json";
    if (!WriteText(manifestPath, manifest.dump(2) + "\n", errorOut)) {
        fs::remove_all(staging, filesystemError);
        return false;
    }
    molga::ShaderBundleManifest packaged;
    if (!molga::ShaderBundleManifest::Load(manifestPath, packaged, errorOut) ||
        !packaged.Validate(staging, false, errorOut)) {
        fs::remove_all(staging, filesystemError);
        return false;
    }
    return InstallAtomically(staging, output, errorOut);
}

void PrintUsage() {
    std::cerr
        << "usage:\n"
        << "  molga_shaderc build --descriptors <dir> [--descriptors <dir> ...] "
           "--output <bundle-dir>\n"
        << "  molga_shaderc package-msl --bundle <full-bundle-dir> "
           "--output <bundle-dir>\n"
        << "  molga_shaderc validate --bundle <bundle-dir> [--msl-only]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 2;
    }
    const std::string command = argv[1];
    std::vector<fs::path> descriptorRoots;
    fs::path output;
    fs::path bundle;
    bool mslOnly = false;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--descriptors" && index + 1 < argc) {
            descriptorRoots.emplace_back(argv[++index]);
        } else if (argument == "--output" && index + 1 < argc) {
            output = argv[++index];
        } else if (argument == "--bundle" && index + 1 < argc) {
            bundle = argv[++index];
        } else if (argument == "--msl-only") {
            mslOnly = true;
        } else {
            PrintUsage();
            return 2;
        }
    }

    std::string error;
    if (command == "build") {
        if (descriptorRoots.empty() || output.empty()) {
            PrintUsage();
            return 2;
        }
        if (!BuildBundle(descriptorRoots, output, error)) {
            std::cerr << "molga_shaderc: " << error << '\n';
            return 1;
        }
        std::cout << "shader bundle installed: " << output << '\n';
        return 0;
    }
    if (command == "validate") {
        if (bundle.empty()) {
            PrintUsage();
            return 2;
        }
        molga::ShaderBundleManifest manifest;
        if (!molga::ShaderBundleManifest::Load(bundle / "manifest.json",
                                               manifest, error) ||
            !manifest.Validate(bundle, !mslOnly, error)) {
            std::cerr << "molga_shaderc: " << error << '\n';
            return 1;
        }
        std::cout << "shader bundle valid: " << bundle << '\n';
        return 0;
    }
    if (command == "package-msl") {
        if (bundle.empty() || output.empty()) {
            PrintUsage();
            return 2;
        }
        if (!PackageMslBundle(bundle, output, error)) {
            std::cerr << "molga_shaderc: " << error << '\n';
            return 1;
        }
        std::cout << "MSL shader bundle installed: " << output << '\n';
        return 0;
    }
    PrintUsage();
    return 2;
}
