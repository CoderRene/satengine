#pragma once

#include "AssetData.hpp"

namespace sat {
bool decodeFbxMesh(std::string_view path, std::string_view bytes, const AssetReader& reader,
    bool flipV, MeshData& mesh, std::string& error);
bool decodeFbxAnimation(std::string_view bytes, AnimationData& animation, std::string& error);
}
