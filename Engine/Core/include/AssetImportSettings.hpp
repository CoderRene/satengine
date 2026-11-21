#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace sat {
inline constexpr std::size_t kMaxImportSettingsBytes = 1024 * 1024;

enum class TextureFilter { Point, Bilinear, Trilinear };
enum class AudioLoadMode { Auto, Decode, Stream };
enum class TextureWrap { Repeat, Clamp };
struct AssetImportSettings {
    float scale = 1.0f;
    bool rightHanded = true;
    bool flipV = true;
    int maximumSize = 4096;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap = TextureWrap::Repeat;
    bool mipmaps = true;
    AudioLoadMode audioLoadMode = AudioLoadMode::Auto;
    bool mergeGeometry = false;
    bool preserveNodeHierarchy = false;
    std::map<std::uint32_t, std::string> materialLinks; // Source material index to project asset path.
    bool operator==(const AssetImportSettings&) const = default;
};
[[nodiscard]] bool parseImportSettings(std::string_view text, AssetImportSettings& settings, std::string& error);
[[nodiscard]] std::string serializeImportSettings(const AssetImportSettings& settings);
[[nodiscard]] bool loadImportSettings(const std::filesystem::path& source, AssetImportSettings& settings, std::string& error);
[[nodiscard]] bool saveImportSettings(const std::filesystem::path& source, const AssetImportSettings& settings, std::string& error);
}
