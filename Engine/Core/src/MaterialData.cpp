#include "AssetData.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace sat {

bool decodeMaterial(std::string_view bytes, MaterialData& material, std::string& error) {
    error = "Invalid SatMaterial data";
    if (bytes.size() > 8192) { return false; }

    std::istringstream in{std::string(bytes)};
    in.imbue(std::locale::classic());
    MaterialData candidate;
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "SatMaterial" || (version < 1 || version > 5)) { return false; }

    if (version >= 2) {
        std::string shader;
        if (!(in >> shader)) { return false; }
        if (shader == "unlit") { candidate.shader = MaterialShader::Unlit; }
        else if (shader == "custom" && version >= 5) {
            in >> std::ws;
            if (in.peek() != '"' || !(in >> std::quoted(candidate.shaderPath)) ||
                !isShaderAssetPath(candidate.shaderPath)) { return false; }
            candidate.shader = MaterialShader::Custom;
        } else if (shader != "lit") { return false; }
    }

    const auto channel = [&](float& value) {
        if (!(in >> value) || !std::isfinite(value)) { return false; }
        value = std::clamp(value, 0.0f, 1.0f);
        return true;
    };
    if (version >= 3) {
        std::string mode;
        if (!(in >> mode) || !channel(candidate.alphaCutoff)) { return false; }
        if (mode == "mask") { candidate.alphaMode = AlphaMode::Mask; }
        else if (mode == "blend") { candidate.alphaMode = AlphaMode::Blend; }
        else if (mode != "opaque") { return false; }
    }
    if (version >= 4 && !channel(candidate.specularStrength)) { return false; }
    for (float& value : candidate.baseColor) { if (!channel(value)) { return false; } }
    if (!channel(candidate.metallic) || !channel(candidate.roughness)) { return false; }
    for (float& value : candidate.emissive) { if (!channel(value)) { return false; } }
    if (!channel(candidate.normalScale)) { return false; }

    for (auto* path : {&candidate.baseColorTexture, &candidate.metallicRoughnessTexture,
                       &candidate.normalTexture, &candidate.emissiveTexture}) {
        in >> std::ws;
        if (in.peek() != '"' || !(in >> std::quoted(*path)) ||
            (!path->empty() && !isTextureAssetPath(*path))) { return false; }
    }
    in >> std::ws;
    if (!in.eof()) { return false; }
    material = std::move(candidate);
    error.clear();
    return true;
}

std::string encodeMaterial(const MaterialData& material) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "SatMaterial 5\n" << std::setprecision(std::numeric_limits<float>::max_digits10);
    if (material.shader == MaterialShader::Custom) {
        out << "custom " << std::quoted(material.shaderPath) << '\n';
    } else {
        out << (material.shader == MaterialShader::Unlit ? "unlit\n" : "lit\n");
    }
    const char* mode = "opaque";
    if (material.alphaMode == AlphaMode::Mask) { mode = "mask"; }
    if (material.alphaMode == AlphaMode::Blend) { mode = "blend"; }
    out << mode << ' ' << material.alphaCutoff << '\n';
    out << material.specularStrength << '\n';
    for (float value : material.baseColor) { out << value << ' '; }
    out << '\n' << material.metallic << ' ' << material.roughness << '\n'
        << material.emissive[0] << ' ' << material.emissive[1] << ' ' << material.emissive[2] << ' '
        << material.normalScale << '\n'
        << std::quoted(material.baseColorTexture) << '\n' << std::quoted(material.metallicRoughnessTexture) << '\n'
        << std::quoted(material.normalTexture) << '\n' << std::quoted(material.emissiveTexture) << '\n';
    return out.str();
}

} // namespace sat
