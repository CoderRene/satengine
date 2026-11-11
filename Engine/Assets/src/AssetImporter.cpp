#include "AssetImporter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace sat {
namespace {
bool valid(const AssetImportSettings& settings, std::string& error) {
    AssetImportSettings checked;
    return parseImportSettings(serializeImportSettings(settings), checked, error);
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
bool importMesh(std::string_view bytes, const AssetImportSettings& settings, MeshData& mesh, std::string& error) {
    if (!valid(settings, error)) { return false; }
    MeshData candidate;
    if (!decodeObj(bytes, candidate, error, settings.rightHanded, settings.flipV)) { return false; }
    for (auto& vertex : candidate.vertices) {
        vertex.x *= settings.scale;
        vertex.y *= settings.scale;
        vertex.z *= settings.scale;
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) || !std::isfinite(vertex.v)) {
            error = "Mesh import transform produced nonfinite coordinates";
            return false;
        }
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
bool importTexture(std::string_view bytes, const AssetImportSettings& settings, TextureData& texture, std::string& error) {
    if (!valid(settings, error)) { return false; }
    TextureData candidate;
    if (!decodeTexture(bytes, candidate, error)) { return false; }
    const int largest = std::max(candidate.width, candidate.height);
    if (largest > settings.maximumSize) {
        const int width = std::max(1, static_cast<int>(std::lround(static_cast<double>(candidate.width) * settings.maximumSize / largest)));
        const int height = std::max(1, static_cast<int>(std::lround(static_cast<double>(candidate.height) * settings.maximumSize / largest)));
        candidate.pixels = resize(candidate.pixels, candidate.width, candidate.height, width, height);
        candidate.width = width;
        candidate.height = height;
    }
    candidate.filter = settings.filter;
    candidate.wrap = settings.wrap;
    if (settings.mipmaps) {
        int width = candidate.width;
        int height = candidate.height;
        const std::vector<unsigned char>* pixels = &candidate.pixels;
        // Reserve so pointers to previously generated levels remain stable.
        candidate.mips.reserve(12);
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
