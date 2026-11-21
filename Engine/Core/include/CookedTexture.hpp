#pragma once

#include "AssetData.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace sat {

inline constexpr std::size_t kMaxCookedTextureBytes = 24 * 1024 * 1024;

[[nodiscard]] std::string cookedTexturePath(std::string_view source, bool preview);
[[nodiscard]] std::string cookedEmbeddedTexturePath(std::string_view mesh, std::string_view image);
[[nodiscard]] AssetImportSettings embeddedTextureSettings(const MeshData& mesh, std::string_view image);
// Returns false for textures outside the BC7 path. Leaves both outputs intact.
[[nodiscard]] bool cookTexture(const TextureData& source, std::string& cooked, std::string& preview);
[[nodiscard]] bool decodeCookedTexture(std::string_view bytes, TextureData& texture, std::string& error);

// Editor cache entries are keyed by source bytes and validated import settings.
[[nodiscard]] AssetReader projectRenderAssetReader(const std::filesystem::path& root);
void cacheCookedTexture(const std::filesystem::path& root, std::string_view path, std::uint64_t sourceHash,
    const AssetImportSettings& settings, const TextureData& texture);
[[nodiscard]] std::uint64_t cookedTextureSourceHash(std::string_view bytes);

} // namespace sat
