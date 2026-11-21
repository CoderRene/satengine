#include "AssetImporter.hpp"

#include "module/FbxImport.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sat {
namespace {
bool valid(const AssetImportSettings& settings, std::string& error) {
    AssetImportSettings checked;
    return parseImportSettings(serializeImportSettings(settings), checked, error);
}
void applyMaterialLinks(const AssetReader& reader, const AssetImportSettings& settings,
    const std::vector<std::uint32_t>& sourceMaterialIndices, MeshData& mesh) {
    if (!reader || settings.materialLinks.empty()) { return; }

    std::string bytes, error;
    for (std::size_t i = 0; i < mesh.materials.size(); ++i) {
        const auto sourceIndex = sourceMaterialIndices.empty() ? static_cast<std::uint32_t>(i) : sourceMaterialIndices[i];
        const auto link = settings.materialLinks.find(sourceIndex);
        if (link == settings.materialLinks.end()) { continue; }
        MaterialData material;
        if (reader(link->second, bytes, error) != AssetReadResult::Found || !decodeMaterial(bytes, material, error)) {
            if (!mesh.materialWarning.empty()) { mesh.materialWarning += '\n'; }
            mesh.materialWarning += "Cannot load linked material " + link->second;
            continue;
        }
        if (mesh.materialAssetPaths.empty()) { mesh.materialAssetPaths.resize(mesh.materials.size()); }
        mesh.materialAssetPaths[i] = link->second;
        mesh.materials[i] = std::move(material);
    }
}
// Group opaque and alpha-masked ranges in first-use order. Retain blended
// ranges for sorting and volume ranges for their baked thickness scales.
void mergeMeshSections(MeshData& mesh) {
    if (mesh.sections.size() < 2) { return; }

    constexpr auto kUnassigned = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> materialGroups(mesh.materials.size() + 1, kUnassigned);
    std::vector<std::size_t> sectionGroups;
    sectionGroups.reserve(mesh.sections.size());
    std::vector<MeshSection> merged;
    merged.reserve(mesh.sections.size());
    for (const auto& section : mesh.sections) {
        const bool hasMaterial = section.materialIndex < mesh.materials.size();
        const bool separate = hasMaterial && (mesh.materials[section.materialIndex].alphaMode == AlphaMode::Blend ||
            mesh.materials[section.materialIndex].thickness > 0.0f);
        auto& materialGroup = materialGroups[hasMaterial ? section.materialIndex : mesh.materials.size()];
        if (separate || materialGroup == kUnassigned) {
            if (!separate) { materialGroup = merged.size(); }
            sectionGroups.push_back(merged.size());
            merged.push_back(section);
        } else {
            sectionGroups.push_back(materialGroup);
            merged[materialGroup].indexCount += section.indexCount;
        }
    }
    if (merged.size() == mesh.sections.size()) { return; }

    std::vector<std::uint32_t> nextIndex;
    nextIndex.reserve(merged.size());
    std::uint32_t firstIndex = 0;
    for (auto& section : merged) {
        section.firstIndex = firstIndex;
        nextIndex.push_back(firstIndex);
        firstIndex += section.indexCount;
    }

    std::vector<std::uint32_t> indices(mesh.indices.size());
    for (std::size_t i = 0; i < mesh.sections.size(); ++i) {
        const auto& section = mesh.sections[i];
        auto& next = nextIndex[sectionGroups[i]];
        std::copy_n(mesh.indices.data() + section.firstIndex, section.indexCount, indices.data() + next);
        next += section.indexCount;
    }
    mesh.nodeIndices = std::move(mesh.indices);
    mesh.nodeSections = std::move(mesh.sections);
    mesh.indices = std::move(indices);
    mesh.sections = std::move(merged);
}

template <typename Pixel>
std::vector<Pixel> resize(const std::vector<Pixel>& source, int width, int height, int outWidth, int outHeight) {
    std::vector<Pixel> output(static_cast<std::size_t>(outWidth) * outHeight * 4);
    // Whole-pixel boxes are common for power-of-two textures and mip levels.
    if (width % outWidth == 0 && height % outHeight == 0) {
        const int stepX = width / outWidth;
        const int stepY = height / outHeight;
        const auto area = static_cast<unsigned>(stepX * stepY);
        using Sum = std::conditional_t<std::is_same_v<Pixel, unsigned char>, std::uint32_t, double>;
        for (int y = 0; y < outHeight; ++y) {
            for (int x = 0; x < outWidth; ++x) {
                Sum channels[4]{};
                for (int sy = 0; sy < stepY; ++sy) {
                    const auto offset = (static_cast<std::size_t>(y * stepY + sy) * width + x * stepX) * 4;
                    const auto* row = source.data() + offset;
                    for (int sx = 0; sx < stepX * 4; sx += 4) {
                        for (int channel = 0; channel < 4; ++channel) { channels[channel] += row[sx + channel]; }
                    }
                }
                const auto offset = (static_cast<std::size_t>(y) * outWidth + x) * 4;
                for (int channel = 0; channel < 4; ++channel) {
                    if constexpr (std::is_same_v<Pixel, unsigned char>) {
                        output[offset + channel] = static_cast<Pixel>((channels[channel] + area / 2) / area);
                    } else {
                        output[offset + channel] = static_cast<Pixel>(channels[channel] / area);
                    }
                }
            }
        }
        return output;
    }

    // Area-weighted box filter includes the full image, including odd edges.
    const double scaleX = static_cast<double>(width) / outWidth;
    const double scaleY = static_cast<double>(height) / outHeight;
    for (int y = 0; y < outHeight; ++y) {
        const double top = y * scaleY;
        const double bottom = (y + 1) * scaleY;
        const int lastY = std::min(height, static_cast<int>(std::ceil(bottom)));
        for (int x = 0; x < outWidth; ++x) {
            const double left = x * scaleX;
            const double right = (x + 1) * scaleX;
            const int lastX = std::min(width, static_cast<int>(std::ceil(right)));
            double channels[4]{};
            for (int sy = static_cast<int>(top); sy < lastY; ++sy) {
                const double weightY = std::min(bottom, static_cast<double>(sy + 1)) - std::max(top, static_cast<double>(sy));
                for (int sx = static_cast<int>(left); sx < lastX; ++sx) {
                    const double weight = weightY * (std::min(right, static_cast<double>(sx + 1)) - std::max(left, static_cast<double>(sx)));
                    const auto offset = (static_cast<std::size_t>(sy) * width + sx) * 4;
                    for (int channel = 0; channel < 4; ++channel) { channels[channel] += source[offset + channel] * weight; }
                }
            }
            const auto offset = (static_cast<std::size_t>(y) * outWidth + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                const double average = channels[channel] / (scaleX * scaleY);
                if constexpr (std::is_same_v<Pixel, unsigned char>) {
                    output[offset + channel] = static_cast<Pixel>(std::clamp(std::round(average), 0.0, 255.0));
                } else {
                    output[offset + channel] = static_cast<Pixel>(average);
                }
            }
        }
    }
    return output;
}
}
bool importMesh(std::string_view path, std::string_view bytes, const AssetReader& reader,
    const AssetImportSettings& settings, MeshData& mesh, std::string& error) {
    if (!valid(settings, error)) { return false; }
    MeshData candidate;
    std::vector<std::uint32_t> sourceMaterialIndices;
    if (isFbxAssetPath(path)) {
        if (!decodeFbxMesh(path, bytes, reader, settings.flipV, candidate, error)) { return false; }
    } else if (isGltfAssetPath(path)) {
        auto* indices = settings.materialLinks.empty() ? nullptr : &sourceMaterialIndices;
        if (!decodeGltf(bytes, path, reader, candidate, error, indices)) { return false; }
    } else {
        std::vector<ObjMaterial> materials;
        if (!settings.materialLinks.empty()) {
            // Retain geometry if a material library has gone missing.
            (void)decodeObjMaterials(bytes, path, reader, materials, candidate.materialWarning);
        }
        auto warning = std::move(candidate.materialWarning);
        if (!decodeObj(bytes, candidate, error, settings.rightHanded, settings.flipV, materials)) { return false; }
        candidate.materialWarning = std::move(warning);
    }
    applyMaterialLinks(reader, settings, sourceMaterialIndices, candidate);
    for (auto& vertex : candidate.vertices) {
        vertex.x *= settings.scale;
        vertex.y *= settings.scale;
        vertex.z *= settings.scale;
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) || !std::isfinite(vertex.v)) {
            error = "Mesh import transform produced nonfinite coordinates";
            return false;
        }
    }
    for (auto& node : candidate.nodes) {
        for (int axis = 0; axis < 3; ++axis) {
            node.origin[axis] *= settings.scale;
            node.minimum[axis] *= settings.scale;
            node.maximum[axis] *= settings.scale;
            if (!std::isfinite(node.origin[axis])) {
                error = "Mesh import produced a nonfinite node origin";
                return false;
            }
        }
    }
    for (auto& imported : candidate.lights) {
        if (imported.light.type == LightType::Directional) { continue; }
        imported.light.range *= settings.scale;
        if (!std::isfinite(imported.light.range) || imported.light.range <= 0) {
            error = "Mesh import produced an invalid light range";
            return false;
        }
    }
    if (candidate.vertices.empty()) {
        mesh = std::move(candidate);
        return true;
    }
    for (auto& palette : candidate.restPalette) {
        palette.m[12] *= settings.scale; palette.m[13] *= settings.scale; palette.m[14] *= settings.scale;
    }
    for (auto& bounds : candidate.jointBounds) {
        bounds.minimum = bounds.minimum * settings.scale;
        bounds.maximum = bounds.maximum * settings.scale;
    }
    for (auto& section : candidate.sections) { section.thicknessScale = section.thicknessScale * settings.scale; }
    if (settings.mergeGeometry && candidate.skinVertices.empty()) {
        mergeMeshSections(candidate);
        candidate.mergeNodeDraws = !candidate.nodes.empty();
    }
    const auto& first = candidate.vertices.front();
    candidate.minimum[0] = candidate.maximum[0] = first.x;
    candidate.minimum[1] = candidate.maximum[1] = first.y;
    candidate.minimum[2] = candidate.maximum[2] = first.z;
    for (const auto& vertex : candidate.vertices) {
        const float values[3] = {vertex.x, vertex.y, vertex.z};
        for (int axis = 0; axis < 3; ++axis) {
            candidate.minimum[axis] = std::min(candidate.minimum[axis], values[axis]);
            candidate.maximum[axis] = std::max(candidate.maximum[axis], values[axis]);
        }
    }
    mesh = std::move(candidate);
    return true;
}
bool importModelAnimation(std::string_view path, std::string_view bytes, const AssetReader& reader,
    AnimationData& animation, std::string& error) {
    if (isFbxAssetPath(path)) { return decodeFbxAnimation(bytes, animation, error); }
    return decodeGltfAnimation(bytes, path, reader, animation, error);
}

TextureInfo textureImportInfo(int width, int height, const AssetImportSettings& settings) {
    const int largest = std::max(width, height);
    if (largest > settings.maximumSize) {
        width = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * settings.maximumSize / largest)));
        height = std::max(1, static_cast<int>(std::lround(static_cast<double>(height) * settings.maximumSize / largest)));
    }
    const int mipLevels = settings.mipmaps ? static_cast<int>(std::bit_width(static_cast<unsigned>(std::max(width, height)))) : 1;
    return {width, height, mipLevels};
}

bool importTexture(std::string_view bytes, const AssetImportSettings& settings, TextureData& texture, std::string& error) {
    if (!valid(settings, error)) { return false; }
    TextureData candidate;
    if (!decodeTexture(bytes, candidate, error)) { return false; }
    const auto info = textureImportInfo(candidate.width, candidate.height, settings);
    const auto buildLevels = [&](auto TextureData::* basePixels, auto TextureMip::* mipPixels) {
        auto& base = candidate.*basePixels;
        if (candidate.width != info.width || candidate.height != info.height) {
            base = resize(base, candidate.width, candidate.height, info.width, info.height);
        }
        int width = info.width;
        int height = info.height;
        const auto* pixels = &base;
        candidate.mips.reserve(static_cast<std::size_t>(info.mipLevels - 1));
        for (int level = 1; level < info.mipLevels; ++level) {
            const int nextWidth = std::max(1, width / 2);
            const int nextHeight = std::max(1, height / 2);
            auto next = resize(*pixels, width, height, nextWidth, nextHeight);
            auto& mip = candidate.mips.emplace_back();
            mip.width = nextWidth;
            mip.height = nextHeight;
            mip.*mipPixels = std::move(next);
            pixels = &(mip.*mipPixels);
            width = nextWidth;
            height = nextHeight;
        }
    };
    if (candidate.hdrPixels.empty()) {
        buildLevels(&TextureData::pixels, &TextureMip::pixels);
    } else {
        buildLevels(&TextureData::hdrPixels, &TextureMip::hdrPixels);
    }
    candidate.width = info.width;
    candidate.height = info.height;
    candidate.filter = settings.filter;
    candidate.wrap = settings.wrap;
    texture = std::move(candidate);
    return true;
}
}
