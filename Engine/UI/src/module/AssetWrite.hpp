#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace sat {
[[nodiscard]] bool writeAsset(const std::filesystem::path& destination, std::string_view path, std::string_view bytes, std::string& error);
}
