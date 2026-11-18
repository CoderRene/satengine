#include "AssetImporter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace sat {
namespace {
bool valid(const AssetImportSettings& settings, std::string& error) {
    AssetImportSettings checked;
    return parseImportSettings(serializeImportSettings(settings), checked, error);
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

std::vector<unsigned char> resize(const std::vector<unsigned char>& source, int width, int height, int outWidth, int outHeight) {
    std::vector<unsigned char> output(static_cast<std::size_t>(outWidth) * outHeight * 4);
    // Area-weighted box filter includes the full image, including odd edges.
    const double scaleX = static_cast<double>(width) / outWidth;
    const double scaleY = static_cast<double>(height) / outHeight;
    for (int y = 0; y < outHeight; ++y) {
        const double top = y * scaleY;
        const double bottom = (y + 1) * scaleY;
        for (int x = 0; x < outWidth; ++x) {
            const double left = x * scaleX;
            const double right = (x + 1) * scaleX;
            double channels[4]{};
            for (int sy = static_cast<int>(top); sy < std::min(height, static_cast<int>(std::ceil(bottom))); ++sy) {
                const double weightY = std::min(bottom, static_cast<double>(sy + 1)) - std::max(top, static_cast<double>(sy));
                for (int sx = static_cast<int>(left); sx < std::min(width, static_cast<int>(std::ceil(right))); ++sx) {
                    const double weight = weightY * (std::min(right, static_cast<double>(sx + 1)) - std::max(left, static_cast<double>(sx)));
                    const auto offset = (static_cast<std::size_t>(sy) * width + sx) * 4;
                    for (int channel = 0; channel < 4; ++channel) { channels[channel] += source[offset + channel] * weight; }
                }
            }
            const auto offset = (static_cast<std::size_t>(y) * outWidth + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                output[offset + channel] = static_cast<unsigned char>(std::clamp(std::round(channels[channel] / (scaleX * scaleY)), 0.0, 255.0));
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
    if (isGltfAssetPath(path) ? !decodeGltf(bytes, path, reader, candidate, error) :
        !decodeObj(bytes, candidate, error, settings.rightHanded, settings.flipV)) { return false; }
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
    if (candidate.width != info.width || candidate.height != info.height) {
        candidate.pixels = resize(candidate.pixels, candidate.width, candidate.height, info.width, info.height);
        candidate.width = info.width;
        candidate.height = info.height;
    }
    candidate.filter = settings.filter;
    candidate.wrap = settings.wrap;
    if (settings.mipmaps) {
        int width = candidate.width;
        int height = candidate.height;
        const std::vector<unsigned char>* pixels = &candidate.pixels;
        // Reserve so pointers to previously generated levels remain stable.
        candidate.mips.reserve(static_cast<std::size_t>(info.mipLevels - 1));
        while (width > 1 || height > 1) {
            const int nextWidth = std::max(1, width / 2);
            const int nextHeight = std::max(1, height / 2);
            auto next = resize(*pixels, width, height, nextWidth, nextHeight);
            candidate.mips.push_back({nextWidth, nextHeight, std::move(next)});
            pixels = &candidate.mips.back().pixels;
            width = nextWidth;
            height = nextHeight;
        }
    }
    texture = std::move(candidate);
    return true;
}
}
