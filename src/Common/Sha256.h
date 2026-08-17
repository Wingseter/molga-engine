#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace molga {

std::string Sha256Bytes(const void* data, std::size_t size);
std::string Sha256String(std::string_view text);
std::string Sha256File(const std::filesystem::path& path,
                       std::string* errorOut = nullptr);

} // namespace molga
