#pragma once

#include "AssetData.hpp"
#include "AssetImportSettings.hpp"

#include <string>
#include <string_view>

namespace sat {
// Shared by editor, archive player and build validation. Preserves destinations
// on failure. No filesystem access or GPU dependencies.
[[nodiscard]] bool importMesh(std::string_view bytes, const AssetImportSettings& settings, MeshData& mesh, std::string& error);
[[nodiscard]] bool importTexture(std::string_view bytes, const AssetImportSettings& settings, TextureData& texture, std::string& error);
}
