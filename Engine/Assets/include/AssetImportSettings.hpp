#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace sat {
enum class TextureFilter { Point, Bilinear, Trilinear };
enum class TextureWrap { Repeat, Clamp };
struct AssetImportSettings {
    float scale = 1.0f;
    bool rightHanded = true;
    bool flipV = true;
    int maximumSize = 4096;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap = TextureWrap::Repeat;
    bool mipmaps = false;
    bool operator==(const AssetImportSettings&) const = default;
};
[[nodiscard]] bool parseImportSettings(std::string_view text, AssetImportSettings& settings, std::string& error);
[[nodiscard]] std::string serializeImportSettings(const AssetImportSettings& settings);
[[nodiscard]] bool loadImportSettings(const std::filesystem::path& source, AssetImportSettings& settings, std::string& error);
[[nodiscard]] bool saveImportSettings(const std::filesystem::path& source, const AssetImportSettings& settings, std::string& error);
}
