#pragma once
#include "PakArchive.hpp"
#include <span>
#include <string>
#include <string_view>
namespace sat {
[[nodiscard]] bool pakRandom(std::span<unsigned char> bytes);
[[nodiscard]] bool pakCrypt(bool encrypt, const PakKey& key, std::string_view nonce,
                            std::string_view aad, std::string_view input, std::string& output,
                            std::span<unsigned char, 16> tag);
}
