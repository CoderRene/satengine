#pragma once

#include "AssetData.hpp"
#include "AssetImportSettings.hpp"

#include <string>
#include <string_view>

namespace sat {
// Shared by editor, archive player and build validation. Preserves destinations
// on failure. Companion files are read through the supplied reader.
[[nodiscard]] bool importMesh(std::string_view path, std::string_view bytes, const AssetReader& reader,
    const AssetImportSettings& settings, MeshData& mesh, std::string& error);
[[nodiscard]] bool importTexture(std::string_view bytes, const AssetImportSettings& settings, TextureData& texture, std::string& error);
// Source dimensions and import settings must already be validated.
[[nodiscard]] TextureInfo textureImportInfo(int width, int height, const AssetImportSettings& settings);
}
