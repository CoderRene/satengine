#include "AssetData.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iterator>
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
    if (!(in >> magic >> version) || magic != "SatMaterial" || (version < 1 || version > 11)) { return false; }

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

    const auto finite = [&](float& value) { return (in >> value) && std::isfinite(value); };
    const auto channel = [&](float& value) {
        if (!finite(value)) { return false; }
        value = std::clamp(value, 0.0f, 1.0f);
        return true;
    };
    if (version >= 3) {
        std::string mode;
        if (!(in >> mode) || (version >= 7 ? !finite(candidate.alphaCutoff) || candidate.alphaCutoff < 0.0f :
            !channel(candidate.alphaCutoff))) { return false; }
        if (mode == "mask") { candidate.alphaMode = AlphaMode::Mask; }
        else if (mode == "blend") { candidate.alphaMode = AlphaMode::Blend; }
        else if (mode != "opaque") { return false; }
    }
    if (version >= 4 && !channel(candidate.specularStrength)) { return false; }
    for (float& value : candidate.baseColor) { if (!channel(value)) { return false; } }
    if (!channel(candidate.metallic) || !channel(candidate.roughness)) { return false; }
    for (float& value : candidate.emissive) { if (!channel(value)) { return false; } }
    if (version >= 7 ? !finite(candidate.normalScale) : !channel(candidate.normalScale)) { return false; }

    const auto texturePath = [&](std::string& path) {
        in >> std::ws;
        return in.peek() == '"' && (in >> std::quoted(path)) &&
            (path.empty() || isTextureAssetPath(path));
    };
    if (!texturePath(candidate.baseColorTexture) || !texturePath(candidate.metallicRoughnessTexture) ||
        !texturePath(candidate.normalTexture) || !texturePath(candidate.emissiveTexture)) { return false; }
    if (version >= 6 && (!channel(candidate.occlusionStrength) || !texturePath(candidate.occlusionTexture))) {
        return false;
    }
    if (version >= 7) {
        if (!(in >> candidate.doubleSided)) { return false; }
        const std::size_t count = version >= 9 ? candidate.textureSampling.size() : version >= 8 ? 7 : 5;
        for (std::size_t i = 0; i < count; ++i) {
            auto& sampling = candidate.textureSampling[i];
            unsigned wrapU = 0, wrapV = 0;
            if (!(in >> sampling.texCoord) || sampling.texCoord > 1 ||
                !finite(sampling.offset[0]) || !finite(sampling.offset[1]) ||
                !finite(sampling.scale[0]) || !finite(sampling.scale[1]) || !finite(sampling.rotation) ||
                !(in >> sampling.useTextureSampler >> wrapU >> wrapV >> sampling.linearMin >>
                    sampling.linearMag >> sampling.linearMip >> sampling.mipmaps) ||
                wrapU > static_cast<unsigned>(MaterialWrap::Mirror) || wrapV > static_cast<unsigned>(MaterialWrap::Mirror)) {
                return false;
            }
            sampling.wrapU = static_cast<MaterialWrap>(wrapU);
            sampling.wrapV = static_cast<MaterialWrap>(wrapV);
        }
    }
    if (version >= 8) {
        if (!channel(candidate.transmission) || !finite(candidate.ior) ||
            (candidate.ior != 0.0f && candidate.ior < 1.0f) ||
            !finite(candidate.thickness) || candidate.thickness < 0.0f ||
            !finite(candidate.attenuationDistance) || candidate.attenuationDistance < 0.0f) { return false; }
        for (float& value : candidate.attenuationColor) { if (!channel(value)) { return false; } }
        if (!texturePath(candidate.transmissionTexture) || !texturePath(candidate.thicknessTexture)) { return false; }
    }
    if (version >= 9 && (!channel(candidate.clearcoat) || !channel(candidate.clearcoatRoughness) ||
        !finite(candidate.clearcoatNormalScale) || !texturePath(candidate.clearcoatTexture) ||
        !texturePath(candidate.clearcoatRoughnessTexture) || !texturePath(candidate.clearcoatNormalTexture))) { return false; }
    if (version >= 10) {
        if (!finite(candidate.transmissionMip)) { return false; }
        candidate.transmissionMip = std::clamp(candidate.transmissionMip, 0.0f, kMaxTransmissionMip);
    }
    if (version >= 11 && !channel(candidate.transmissionRoughnessStrength)) { return false; }
    in >> std::ws;
    if (!in.eof()) { return false; }
    material = std::move(candidate);
    error.clear();
    return true;
}

std::string encodeMaterial(const MaterialData& material) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const bool hasOcclusion = !material.occlusionTexture.empty() || material.occlusionStrength != 1.0f;
    const bool hasSampling = material.doubleSided || material.textureSampling != decltype(material.textureSampling){} ||
        material.alphaCutoff > 1.0f || material.normalScale < 0.0f || material.normalScale > 1.0f;
    const bool hasGlass = material.transmission != 0.0f || material.ior != 1.5f || material.thickness != 0.0f ||
        material.attenuationDistance != 0.0f || !material.transmissionTexture.empty() || !material.thicknessTexture.empty() ||
        std::any_of(std::begin(material.attenuationColor), std::end(material.attenuationColor),
            [](float value) { return value != 1.0f; }) ||
        material.textureSampling[5] != MaterialTextureSampling{} || material.textureSampling[6] != MaterialTextureSampling{};
    const bool hasCoat = material.clearcoat != 0.0f || material.clearcoatRoughness != 0.0f ||
        material.clearcoatNormalScale != 1.0f || !material.clearcoatTexture.empty() ||
        !material.clearcoatRoughnessTexture.empty() || !material.clearcoatNormalTexture.empty() ||
        std::any_of(material.textureSampling.begin() + 7, material.textureSampling.end(),
            [](const auto& sampling) { return sampling != MaterialTextureSampling{}; });
    const int version = material.transmissionRoughnessStrength != 1.0f ? 11 :
        material.transmissionMip != 1.0f ? 10 : hasCoat ? 9 : hasGlass ? 8 : hasSampling ? 7 : hasOcclusion ? 6 : 5;
    out << "SatMaterial " << version << '\n'
        << std::setprecision(std::numeric_limits<float>::max_digits10);
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
    if (version >= 6) {
        out << material.occlusionStrength << '\n' << std::quoted(material.occlusionTexture) << '\n';
    }
    if (version >= 7) {
        out << material.doubleSided << '\n';
        const std::size_t count = version >= 9 ? material.textureSampling.size() : version >= 8 ? 7 : 5;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& sampling = material.textureSampling[i];
            out << sampling.texCoord << ' ' << sampling.offset[0] << ' ' << sampling.offset[1] << ' '
                << sampling.scale[0] << ' ' << sampling.scale[1] << ' ' << sampling.rotation << ' '
                << sampling.useTextureSampler << ' ' << static_cast<unsigned>(sampling.wrapU) << ' '
                << static_cast<unsigned>(sampling.wrapV) << ' ' << sampling.linearMin << ' ' << sampling.linearMag << ' '
                << sampling.linearMip << ' ' << sampling.mipmaps << '\n';
        }
    }
    if (version >= 8) {
        out << material.transmission << ' ' << material.ior << ' ' << material.thickness << ' '
            << material.attenuationDistance << ' ' << material.attenuationColor[0] << ' '
            << material.attenuationColor[1] << ' ' << material.attenuationColor[2] << '\n'
            << std::quoted(material.transmissionTexture) << '\n' << std::quoted(material.thicknessTexture) << '\n';
    }
    if (version >= 9) {
        out << material.clearcoat << ' ' << material.clearcoatRoughness << ' ' << material.clearcoatNormalScale << '\n'
            << std::quoted(material.clearcoatTexture) << '\n' << std::quoted(material.clearcoatRoughnessTexture) << '\n'
            << std::quoted(material.clearcoatNormalTexture) << '\n';
    }
    if (version >= 10) { out << material.transmissionMip << '\n'; }
    if (version >= 11) { out << material.transmissionRoughnessStrength << '\n'; }
    return out.str();
}

} // namespace sat
